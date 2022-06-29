// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2022, Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include "ck/utility/data_type.hpp"
#include "ck/utility/reduction_common.hpp"
#include "ck/utility/reduction_operator.hpp"
#include "ck/utility/reduction_functions_accumulate.hpp"
#include "ck/tensor_operation/gpu/block/reduction_functions_blockwise.hpp"
#include "ck/tensor_operation/gpu/thread/reduction_functions_threadwise.hpp"
#include "ck/tensor_operation/gpu/thread/threadwise_tensor_slice_transfer.hpp"
#include "ck/tensor_operation/gpu/element/element_wise_operation.hpp"

namespace ck {

template <typename GridwiseReduction,
          typename XDataType,
          typename GammaDataType,
          typename BetaDataType,
          typename YDataType,
          typename AccDataType,
          typename GridDesc_M_K>
__global__ void kernel_layernorm(const GridDesc_M_K in_grid_desc_m_k,
                                 const GridDesc_M_K gamma_grid_desc_m_k,
                                 const GridDesc_M_K beta_grid_desc_m_k,
                                 const GridDesc_M_K out_grid_desc_m_k,
                                 index_t block_group_size,
                                 index_t num_k_block_tile_iteration,
                                 AccDataType epsilon,
                                 const XDataType* const __restrict__ p_x_global,
                                 const GammaDataType* const __restrict__ p_gamma_global,
                                 const BetaDataType* const __restrict__ p_beta_global,
                                 YDataType* const __restrict__ p_y_global)
{
    GridwiseReduction::Run(in_grid_desc_m_k,
                           gamma_grid_desc_m_k,
                           beta_grid_desc_m_k,
                           out_grid_desc_m_k,
                           block_group_size,
                           num_k_block_tile_iteration,
                           epsilon,
                           p_x_global,
                           p_gamma_global,
                           p_beta_global,
                           p_y_global);
};

template <typename XDataType,
          typename GammaDataType,
          typename BetaDataType,
          typename YDataType,
          typename AccDataType,
          typename GridDesc_M_K,
          index_t BlockSize,
          index_t MThreadClusterSize,
          index_t KThreadClusterSize,
          index_t MThreadSliceSize,
          index_t KThreadSliceSize,
          index_t InSrcVectorDim,
          index_t InSrcVectorSize,
          index_t AffineSrcVectorDim,
          index_t AffineSrcVectorSize,
          index_t OutDstVectorSize,
          bool SweepOnce>
struct GridwiseLayernorm_mk_to_mk
{
    static_assert(((InSrcVectorDim == 0 && MThreadSliceSize % InSrcVectorSize == 0) ||
                   (InSrcVectorDim == 1 && KThreadSliceSize % InSrcVectorSize == 0)) &&
                      (KThreadSliceSize % OutDstVectorSize == 0),
                  "Invalid thread slice sizes and/or vector sizes configuration, please check!");

    static constexpr bool reorder_thread_cluster = (InSrcVectorDim == 0);

    using ThreadClusterLengths_M_K = Sequence<MThreadClusterSize, KThreadClusterSize>;

    using ThreadBufferDimAccessOrder =
        typename conditional<reorder_thread_cluster, Sequence<1, 0>, Sequence<0, 1>>::type;

    using ThreadClusterArrangeOrder =
        typename conditional<reorder_thread_cluster, Sequence<1, 0>, Sequence<0, 1>>::type;

    static constexpr auto thread_cluster_desc =
        make_cluster_descriptor(ThreadClusterLengths_M_K{}, ThreadClusterArrangeOrder{});

    using ThreadReduceSrcDesc_M_K = decltype(make_naive_tensor_descriptor_packed(
        make_tuple(Number<MThreadSliceSize>{}, Number<KThreadSliceSize>{})));
    using ThreadReduceDstDesc_M =
        decltype(make_naive_tensor_descriptor_packed(make_tuple(Number<MThreadSliceSize>{})));

    using BlockwiseSumReduce =
        PartitionedBlockwiseReduction<AccDataType,
                                      BlockSize,
                                      ThreadClusterLengths_M_K,
                                      ThreadClusterArrangeOrder,
                                      reduce::Add,
                                      false, // ignored
                                      detail::AccumulateWithNanIgnore<reduce::Add, AccDataType>>;

    using ThreadwiseSumReduce =
        ThreadwiseReduction<AccDataType,
                            ThreadReduceSrcDesc_M_K,
                            ThreadReduceDstDesc_M,
                            reduce::Add,
                            false, // ignored
                            detail::AccumulateWithNanIgnore<reduce::Add, AccDataType>>;

    using PassThroughOp = tensor_operation::element_wise::PassThrough;

    static constexpr auto I0 = Number<0>{};
    static constexpr auto I1 = Number<1>{};

    static constexpr index_t M_BlockTileSize = MThreadClusterSize * MThreadSliceSize;
    static constexpr index_t K_BlockTileSize = KThreadClusterSize * KThreadSliceSize;

    __device__ static void Run(const GridDesc_M_K& in_grid_desc_m_k,
                               const GridDesc_M_K& gamma_grid_desc_m_k,
                               const GridDesc_M_K& beta_grid_desc_m_k,
                               const GridDesc_M_K& out_grid_desc_m_k,
                               index_t block_group_size,
                               index_t num_k_block_tile_iteration,
                               AccDataType epsilon,
                               const XDataType* const __restrict__ p_x_global,
                               const GammaDataType* const __restrict__ p_gamma_global,
                               const BetaDataType* const __restrict__ p_beta_global,
                               YDataType* const __restrict__ p_y_global)
    {
        if constexpr(SweepOnce)
        {
            num_k_block_tile_iteration = 1;
        }

        // LDS
        __shared__ AccDataType p_reduce_work_buffer[BlockSize];

        auto out_global_val_buf = make_dynamic_buffer<AddressSpaceEnum::Global>(
            p_y_global, out_grid_desc_m_k.GetElementSpaceSize());

        auto reduce_work_buf =
            make_dynamic_buffer<AddressSpaceEnum::Lds>(p_reduce_work_buffer, BlockSize);

        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize * KThreadSliceSize, true>
            in_thread_buf;

        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize * KThreadSliceSize, true>
            gamma_thread_buf;

        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize * KThreadSliceSize, true>
            beta_thread_buf;

        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize * KThreadSliceSize, true>
            out_thread_buf;

        StaticBuffer<AddressSpaceEnum::Vgpr,
                     AccDataType,
                     MThreadSliceSize * KThreadSliceSize,
                     true>& in_square_thread_buf = out_thread_buf;

        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize, true> mean_thread_buf;
        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize, true>
            mean_square_thread_buf;
        StaticBuffer<AddressSpaceEnum::Vgpr, AccDataType, MThreadSliceSize, true>& var_value_buf =
            mean_square_thread_buf;

        static_for<0, MThreadSliceSize, 1>{}([&](auto I) {
            mean_thread_buf(I)        = reduce::Add::template GetIdentityValue<AccDataType>();
            mean_square_thread_buf(I) = reduce::Add::template GetIdentityValue<AccDataType>();
        });

        const index_t thread_local_id = get_thread_local_1d_id();
        const index_t block_global_id = get_block_1d_id();
        const index_t blkgroup_id     = block_global_id / block_group_size;
        const index_t block_local_id  = block_global_id % block_group_size;

        const auto thread_cluster_idx =
            thread_cluster_desc.CalculateBottomIndex(make_multi_index(thread_local_id));

        const auto thread_m_cluster_id = thread_cluster_idx[I0];
        const auto thread_k_cluster_id = thread_cluster_idx[I1];

        const index_t reduceSizePerBlock = K_BlockTileSize * num_k_block_tile_iteration;

        using ThreadBufferLengths         = Sequence<MThreadSliceSize, KThreadSliceSize>;
        constexpr auto thread_buffer_desc = make_naive_tensor_descriptor_packed(
            make_tuple(Number<MThreadSliceSize>{}, Number<KThreadSliceSize>{}));

        auto threadwise_x_load = ThreadwiseTensorSliceTransfer_v2<XDataType,
                                                                  AccDataType,
                                                                  GridDesc_M_K,
                                                                  decltype(thread_buffer_desc),
                                                                  ThreadBufferLengths,
                                                                  ThreadBufferDimAccessOrder,
                                                                  InSrcVectorDim,
                                                                  InSrcVectorSize,
                                                                  1,
                                                                  true>(
            in_grid_desc_m_k,
            make_multi_index(blkgroup_id * M_BlockTileSize + thread_m_cluster_id * MThreadSliceSize,
                             block_local_id * reduceSizePerBlock +
                                 thread_k_cluster_id * KThreadSliceSize));

        auto threadwise_gamma_load = ThreadwiseTensorSliceTransfer_v2<GammaDataType,
                                                                      AccDataType,
                                                                      GridDesc_M_K,
                                                                      decltype(thread_buffer_desc),
                                                                      ThreadBufferLengths,
                                                                      ThreadBufferDimAccessOrder,
                                                                      AffineSrcVectorDim,
                                                                      AffineSrcVectorSize,
                                                                      1,
                                                                      true>(
            gamma_grid_desc_m_k,
            make_multi_index(blkgroup_id * M_BlockTileSize + thread_m_cluster_id * MThreadSliceSize,
                             block_local_id * reduceSizePerBlock +
                                 thread_k_cluster_id * KThreadSliceSize));

        auto threadwise_beta_load = ThreadwiseTensorSliceTransfer_v2<BetaDataType,
                                                                     AccDataType,
                                                                     GridDesc_M_K,
                                                                     decltype(thread_buffer_desc),
                                                                     ThreadBufferLengths,
                                                                     ThreadBufferDimAccessOrder,
                                                                     AffineSrcVectorDim,
                                                                     AffineSrcVectorSize,
                                                                     1,
                                                                     true>(
            beta_grid_desc_m_k,
            make_multi_index(blkgroup_id * M_BlockTileSize + thread_m_cluster_id * MThreadSliceSize,
                             block_local_id * reduceSizePerBlock +
                                 thread_k_cluster_id * KThreadSliceSize));

        auto threadwise_y_store = ThreadwiseTensorSliceTransfer_v1r3<AccDataType,
                                                                     YDataType,
                                                                     decltype(thread_buffer_desc),
                                                                     GridDesc_M_K,
                                                                     PassThroughOp,
                                                                     ThreadBufferLengths,
                                                                     ThreadBufferDimAccessOrder,
                                                                     InSrcVectorDim,
                                                                     OutDstVectorSize,
                                                                     InMemoryDataOperationEnum::Set,
                                                                     1,
                                                                     true>(
            out_grid_desc_m_k,
            make_multi_index(blkgroup_id * M_BlockTileSize + thread_m_cluster_id * MThreadSliceSize,
                             block_local_id * reduceSizePerBlock +
                                 thread_k_cluster_id * KThreadSliceSize),
            PassThroughOp{});

        constexpr auto in_thread_copy_fwd_step =
            make_multi_index(0, SweepOnce ? 0 : K_BlockTileSize);
        constexpr auto in_thread_copy_bwd_step =
            make_multi_index(0, SweepOnce ? 0 : -K_BlockTileSize);

        const auto in_global_val_buf = make_dynamic_buffer<AddressSpaceEnum::Global>(
            p_x_global, in_grid_desc_m_k.GetElementSpaceSize());

        const auto gamma_global_val_buf = make_dynamic_buffer<AddressSpaceEnum::Global>(
            p_gamma_global, gamma_grid_desc_m_k.GetElementSpaceSize());

        const auto beta_global_val_buf = make_dynamic_buffer<AddressSpaceEnum::Global>(
            p_beta_global, beta_grid_desc_m_k.GetElementSpaceSize());

        // E(x), E[x^2], var(x)
        int reduce_length    = in_grid_desc_m_k.GetLength(I1);
        index_t reducedTiles = 0;
        do
        {
            threadwise_x_load.Run(in_grid_desc_m_k,
                                  in_global_val_buf,
                                  thread_buffer_desc,
                                  make_tuple(I0, I0),
                                  in_thread_buf);

            static_for<0, MThreadSliceSize, 1>{}([&](auto iM) {
                static_for<0, KThreadSliceSize, 1>{}([&](auto iK) {
                    constexpr auto offset = thread_buffer_desc.CalculateOffset(make_tuple(iM, iK));
                    in_square_thread_buf(Number<offset>{}) =
                        in_thread_buf(Number<offset>{}) * in_thread_buf(Number<offset>{});
                });
            });

            ThreadwiseSumReduce::Reduce(in_thread_buf, mean_thread_buf);
            ThreadwiseSumReduce::Reduce(in_square_thread_buf, mean_square_thread_buf);

            threadwise_x_load.MoveSrcSliceWindow(in_grid_desc_m_k, in_thread_copy_fwd_step);

            ++reducedTiles;
        } while(reducedTiles < num_k_block_tile_iteration);

        static_for<0, MThreadSliceSize, 1>{}([&](auto I) {
            BlockwiseSumReduce::Reduce(reduce_work_buf, mean_thread_buf(I));
            mean_thread_buf(I) = mean_thread_buf(I) / reduce_length;

            BlockwiseSumReduce::Reduce(reduce_work_buf, mean_square_thread_buf(I));
            mean_square_thread_buf(I) = mean_square_thread_buf(I) / reduce_length;

            // var(x) = E[x^2] - E[x]^2
            var_value_buf(I) =
                mean_square_thread_buf(I) - (mean_thread_buf(I) * mean_thread_buf(I));
        });

        // y = (x - E[x]) / sqrt(var[x] + epsilon)
        auto thread_copy_tail = (num_k_block_tile_iteration - 1) * in_thread_copy_fwd_step;

        threadwise_x_load.MoveSrcSliceWindow(in_grid_desc_m_k, in_thread_copy_bwd_step);
        threadwise_gamma_load.MoveSrcSliceWindow(in_grid_desc_m_k, thread_copy_tail);
        threadwise_beta_load.MoveSrcSliceWindow(in_grid_desc_m_k, thread_copy_tail);
        threadwise_y_store.MoveDstSliceWindow(out_grid_desc_m_k, thread_copy_tail);

        reducedTiles = 0;
        do
        {
            if constexpr(!SweepOnce)
            {
                threadwise_x_load.Run(in_grid_desc_m_k,
                                      in_global_val_buf,
                                      thread_buffer_desc,
                                      make_tuple(I0, I0),
                                      in_thread_buf);
            }

            threadwise_gamma_load.Run(gamma_grid_desc_m_k,
                                      gamma_global_val_buf,
                                      thread_buffer_desc,
                                      make_tuple(I0, I0),
                                      gamma_thread_buf);

            threadwise_beta_load.Run(beta_grid_desc_m_k,
                                     beta_global_val_buf,
                                     thread_buffer_desc,
                                     make_tuple(I0, I0),
                                     beta_thread_buf);

            static_for<0, MThreadSliceSize, 1>{}([&](auto iM) {
                static_for<0, KThreadSliceSize, 1>{}([&](auto iK) {
                    constexpr auto offset = thread_buffer_desc.CalculateOffset(make_tuple(iM, iK));
                    // normalize
                    out_thread_buf(Number<offset>{}) =
                        (in_thread_buf(Number<offset>{}) - mean_thread_buf(iM)) /
                        sqrt(var_value_buf(iM) + epsilon);

                    // affine
                    out_thread_buf(Number<offset>{}) =
                        out_thread_buf(Number<offset>{}) * gamma_thread_buf(Number<offset>{}) +
                        beta_thread_buf(Number<offset>{});
                });
            });

            threadwise_y_store.Run(thread_buffer_desc,
                                   make_tuple(I0, I0),
                                   out_thread_buf,
                                   out_grid_desc_m_k,
                                   out_global_val_buf);

            threadwise_x_load.MoveSrcSliceWindow(in_grid_desc_m_k, in_thread_copy_bwd_step);
            threadwise_gamma_load.MoveSrcSliceWindow(in_grid_desc_m_k, in_thread_copy_bwd_step);
            threadwise_beta_load.MoveSrcSliceWindow(in_grid_desc_m_k, in_thread_copy_bwd_step);
            threadwise_y_store.MoveDstSliceWindow(out_grid_desc_m_k, in_thread_copy_bwd_step);

            ++reducedTiles;
        } while(reducedTiles < num_k_block_tile_iteration);
    }
};

} // namespace ck
