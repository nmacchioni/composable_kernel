// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2023, Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <iostream>
#include <sstream>
#include <tuple>

#include "ck/ck.hpp"
#include "ck/host_utility/device_prop.hpp"
#include "ck/host_utility/kernel_launch.hpp"
#include "ck/host_utility/hip_check_error.hpp"
#include "ck/host_utility/stream_utility.hpp"
#include "ck/utility/common_header.hpp"
#include "ck/utility/tuple.hpp"
#include <ck/utility/work_scheduling.hpp>
#include "ck/tensor_description/tensor_descriptor.hpp"
#include "ck/tensor_description/tensor_descriptor_helper.hpp"
#include "ck/tensor_operation/gpu/device/tensor_layout.hpp"
#include "ck/tensor_operation/gpu/device/device_grouped_gemm_multiple_d_splitk.hpp"
#include "ck/tensor_operation/gpu/device/gemm_specialization.hpp"
#include <ck/tensor_operation/gpu/grid/block_to_ctile_map.hpp>
#include "ck/tensor_operation/gpu/grid/gridwise_gemm_multiple_d_xdl_splitk_cshuffle_v2.hpp"

namespace ck {
namespace tensor_operation {
namespace device {

///
/// @brief      Entry point kernel for device-wide Grouped GEMM operation.
///
/// @param[in]  gemm_descs_const  The pointer to the array of GEMM descriptor structures.
/// @param[in]  p_workspace       Pointer to the auxilliary workgroup workspace used to store
///                               partial results.
/// @param[in]  tile_count        The overall number of output tiles we divided all groups into.
/// @param[in]  k_batch           The number of batches we split the K dimension into.
///
/// @tparam     GridwiseGemm                The specific GridwiseGEMM algorithm implementation.
/// @tparam     GemmDesc                    The structure holding all necessary descriptors and
///                                         other data needed for grouped gemm calculation and work
///                                         distribution.
/// @tparam     FloatA                      Input tensor A elements' data type.
/// @tparam     FloatB                      Input tensor B elements' data type.
/// @tparam     FloatC                      Input tensor C elements' data type.
/// @tparam     Block2ETileMapKSplit        The structure providing mapping between workgroup ids,
///                                         the data tiles to process and the output tiles.
/// @tparam     HasMainKBlockLoop           Flag indicating whether all GEMM problem configurations
///                                         need to loop over tiles in K dimension.
///
template <typename GridwiseGemm,
          typename GemmDesc,
          typename FloatA,
          typename FloatB,
          typename FloatC,
          typename Block2ETileMapKSplit,
          typename AElementwiseOperation,
          typename BElementwiseOperation,
          typename CDEElementwiseOperation,
          bool HasMainKBlockLoop>
__global__ void
#if CK_USE_LAUNCH_BOUNDS
    __launch_bounds__(CK_MAX_THREAD_PER_BLOCK, CK_MIN_BLOCK_PER_CU)
#endif
        kernel_grouped_gemm_xdl_splitk_v2(
            const void CK_CONSTANT_ADDRESS_SPACE* gemm_descs_const,
            void* const __restrict__ p_workspace,
            const index_t tile_count,
            const index_t k_batch,
            const AElementwiseOperation a_element_op,
            const BElementwiseOperation b_element_op,
            [[maybe_unused]] const CDEElementwiseOperation cde_element_op)
{
#if(!defined(__HIP_DEVICE_COMPILE__) || defined(__gfx908__) || defined(__gfx90a__) || \
    defined(__gfx940__) || defined(__gfx941__) || defined(__gfx942__))

    constexpr index_t shared_size = GridwiseGemm::GetSharedMemoryNumberOfByte();
    __shared__ uint8_t p_shared[shared_size];

    const auto gemm_desc_ptr =
        reinterpret_cast<const GemmDesc*>(cast_pointer_to_generic_address_space(gemm_descs_const));

    uint32_t* const __restrict__ p_flags = reinterpret_cast<uint32_t* const __restrict__>(
        reinterpret_cast<char*>(p_workspace) +
        Block2ETileMapKSplit::GetAccWorkspaceSize(sizeof(typename GridwiseGemm::AccType)));

    StridedReductionTileLoop work_scheduler{tile_count, p_flags};

    // early exit if no work.
    if(work_scheduler.tile_id_ >= tile_count)
        return;

    if(get_thread_global_1d_id() < work_scheduler.GetFlagCount(k_batch))
        p_flags[get_thread_global_1d_id()] = 0;

    index_t group_id = 0;
    index_t offset   = 0;

    auto M                = gemm_desc_ptr[group_id].M;
    auto N                = gemm_desc_ptr[group_id].N;
    auto b2c_tile_map     = Block2ETileMapKSplit(M, N, k_batch);
    index_t grid_size_grp = b2c_tile_map.CalculateGridSize(M, N);

    index_t gemm_tile_id_start = 0;
    index_t gemm_tile_id_end   = grid_size_grp;

    do
    {
        // Find corresponding GEMM group for our tile
        while(!(work_scheduler.tile_id_ >= gemm_tile_id_start &&
                work_scheduler.tile_id_ < gemm_tile_id_end))
        {
            offset += grid_size_grp;
            group_id++;

            M             = gemm_desc_ptr[group_id].M;
            N             = gemm_desc_ptr[group_id].N;
            b2c_tile_map  = Block2ETileMapKSplit(M, N, k_batch);
            grid_size_grp = b2c_tile_map.CalculateGridSize(M, N);

            gemm_tile_id_start = offset;
            gemm_tile_id_end   = offset + grid_size_grp;
        }

        const auto p_a_grid = reinterpret_cast<const FloatA*>(gemm_desc_ptr[group_id].p_a_grid);
        const auto p_b_grid = reinterpret_cast<const FloatB*>(gemm_desc_ptr[group_id].p_b_grid);
        // const auto p_c_grid = reinterpret_cast<FloatC*>(gemm_desc_ptr[group_id].p_c_grid);

        const auto K       = gemm_desc_ptr[group_id].K;
        const auto StrideA = gemm_desc_ptr[group_id].StrideA;
        const auto StrideB = gemm_desc_ptr[group_id].StrideB;
        // const auto StrideC = gemm_desc_ptr[group_id].StrideC;

        auto gridwise_gemm   = GridwiseGemm();
        auto& results_buffer = gridwise_gemm.GetCThreadBuffer();

        b2c_tile_map.CalculateBottomIndex(work_scheduler.tile_id_ - offset);

        // Iterate over K dimension for this [M,N] tile
        // still in the same GEMM && the same [M,N] tile
        do
        {
            // just accumulate results in registers!
            gridwise_gemm.template RunGEMM<HasMainKBlockLoop>(p_a_grid,
                                                              p_b_grid,
                                                              static_cast<void*>(p_shared),
                                                              a_element_op,
                                                              b_element_op,
                                                              M,
                                                              N,
                                                              K,
                                                              StrideA,
                                                              StrideB,
                                                              k_batch,
                                                              b2c_tile_map);

        } while(work_scheduler.GetNextTile() && b2c_tile_map.GetNextKTileIdx());

        // if (changed group_id || next [M,N] tile)
        if(!b2c_tile_map.IsFirstKSplitBlock())
        {
            // Store partial results to auxilliary workspace.
            gridwise_gemm.StorePartials(p_workspace);
        }

        const index_t output_tile_idx =
            __builtin_amdgcn_readfirstlane(b2c_tile_map.GetOutputTileIdx());
        const index_t output_tile_idx_offset = __builtin_amdgcn_readfirstlane(offset / k_batch);

        work_scheduler.FlagFinished(k_batch, output_tile_idx, output_tile_idx_offset);

        // The workgroup which processed first K tile accumulates results and stores to GMEM
        if(b2c_tile_map.IsFirstKSplitBlock())
        {
            // Wait untill all other blocks for this [M,N] tile store their results.
            work_scheduler.WaitForNeighbours(k_batch, output_tile_idx, output_tile_idx_offset);

            // Accumulate partial results. We can have different # of workgroups to reduce, thus we
            // read actual flag value.
            const index_t flag_v = __builtin_amdgcn_readfirstlane(
                work_scheduler.GetFlagValue(k_batch, output_tile_idx, output_tile_idx_offset));

            gridwise_gemm.AccumulatePartials(p_workspace, flag_v);

            // TODO: do blockwise reduction from workspace (GMEM) to results_buffer (registers)

            // Signal waiting blocks that they can start use their workspace.
            work_scheduler.Reset(k_batch, output_tile_idx, output_tile_idx_offset);

            // TODO do fusion, cshuffle and store results to GMEM
            // gridwise_gemm.RunWrite(results_buffer,
            //                        p_c_grid,
            //                        M,
            //                        N,
            //                        K,
            //                        StrideA,
            //                        StrideB,
            //                        StrideC,
            //                        MPadded,
            //                        NPadded,
            //                        KPadded,
            //                        K0,
            //                        k_batch,
            //                        static_cast<void*>(p_shared),
            //                        b2c_tile_map);
        }
        else
        {
            // TODO: double buffering in order to not wait for this.
            work_scheduler.WaitForReduction(k_batch, output_tile_idx, output_tile_idx_offset);
        }
    } while(work_scheduler.HasTile());
#else
    ignore = gemm_descs_const;
    ignore = p_workspace;
    ignore = tile_count;
    ignore = k_batch;
#endif // end of if (defined(__gfx908__) || defined(__gfx90a__))
}

template <typename ALayout,
          typename BLayout,
          typename DsLayout,
          typename ELayout,
          typename ADataType,
          typename BDataType,
          typename AccDataType,
          typename CShuffleDataType,
          typename DsDataType,
          typename EDataType,
          typename AElementwiseOperation,
          typename BElementwiseOperation,
          typename CDEElementwiseOperation,
          GemmSpecialization GemmSpec,
          ck::index_t NumGemmKPrefetchStage,
          ck::index_t BlockSize,
          ck::index_t MPerBlock,
          ck::index_t NPerBlock,
          ck::index_t KPerBlock,
          ck::index_t AK1,
          ck::index_t BK1,
          ck::index_t MPerXDL,
          ck::index_t NPerXDL,
          ck::index_t MXdlPerWave,
          ck::index_t NXdlPerWave,
          typename ABlockTransferThreadClusterLengths_KBatch_AK0_M_AK1,
          typename ABlockTransferThreadClusterArrangeOrder,
          typename ABlockTransferSrcAccessOrder,
          index_t ABlockTransferSrcVectorDim,
          index_t ABlockTransferSrcScalarPerVector,
          index_t ABlockTransferDstScalarPerVector_AK1,
          bool AThreadTransferSrcResetCoordinateAfterRun,
          index_t ABlockLdsExtraM,
          typename BBlockTransferThreadClusterLengths_KBatch_BK0_N_BK1,
          typename BBlockTransferThreadClusterArrangeOrder,
          typename BBlockTransferSrcAccessOrder,
          index_t BBlockTransferSrcVectorDim,
          index_t BBlockTransferSrcScalarPerVector,
          index_t BBlockTransferDstScalarPerVector_BK1,
          bool BThreadTransferSrcResetCoordinateAfterRun,
          index_t BBlockLdsExtraN,
          index_t CShuffleMXdlPerWavePerShuffle,
          index_t CShuffleNXdlPerWavePerShuffle,
          typename CDEBlockTransferClusterLengths_MBlock_MPerBlock_NBlock_NPerBlock,
          index_t CDEShuffleBlockTransferScalarPerVector_NPerBlock,
          LoopScheduler LoopSched     = make_default_loop_scheduler(),
          PipelineVersion PipelineVer = PipelineVersion::v1,
          typename ComputeDataType    = EDataType>
struct DeviceGroupedGemmMultipleDSplitKXdlCShuffle
    : public DeviceGroupedGemmMultipleDSplitK<ALayout,
                                              BLayout,
                                              DsLayout,
                                              ELayout,
                                              ADataType,
                                              BDataType,
                                              DsDataType,
                                              EDataType,
                                              AElementwiseOperation,
                                              BElementwiseOperation,
                                              CDEElementwiseOperation>
{
    using DeviceOp = DeviceGroupedGemmMultipleDSplitKXdlCShuffle;

    static constexpr index_t NumDTensor = DsDataType::Size();

    static constexpr auto I0 = Number<0>{};
    static constexpr auto I1 = Number<1>{};
    static constexpr auto I2 = Number<2>{};
    static constexpr auto I3 = Number<3>{};

    using GridwiseGemm = GridwiseGemmMultipleD_xdl_splitk_cshuffle_v2<
        ADataType,
        BDataType,
        ComputeDataType,
        AccDataType,
        CShuffleDataType,
        DsDataType,
        EDataType,
        ALayout,
        BLayout,
        DsLayout,
        ELayout,
        AElementwiseOperation,
        BElementwiseOperation,
        CDEElementwiseOperation,
        GemmSpec,
        NumGemmKPrefetchStage,
        BlockSize,
        MPerBlock,
        NPerBlock,
        KPerBlock,
        AK1,
        BK1,
        MPerXDL,
        NPerXDL,
        MXdlPerWave,
        NXdlPerWave,
        ABlockTransferThreadClusterLengths_KBatch_AK0_M_AK1,
        ABlockTransferThreadClusterArrangeOrder,
        ABlockTransferSrcAccessOrder,
        ABlockTransferSrcVectorDim,
        ABlockTransferSrcScalarPerVector,
        ABlockTransferDstScalarPerVector_AK1,
        AThreadTransferSrcResetCoordinateAfterRun,
        ABlockLdsExtraM,
        BBlockTransferThreadClusterLengths_KBatch_BK0_N_BK1,
        BBlockTransferThreadClusterArrangeOrder,
        BBlockTransferSrcAccessOrder,
        BBlockTransferSrcVectorDim,
        BBlockTransferSrcScalarPerVector,
        BBlockTransferDstScalarPerVector_BK1,
        BThreadTransferSrcResetCoordinateAfterRun,
        BBlockLdsExtraN,
        CShuffleMXdlPerWavePerShuffle,
        CShuffleNXdlPerWavePerShuffle,
        CDEBlockTransferClusterLengths_MBlock_MPerBlock_NBlock_NPerBlock,
        CDEShuffleBlockTransferScalarPerVector_NPerBlock,
        LoopSched,
        PipelineVer>;

    using KernelArguments                  = GroupedGemmMultipleDKernelArguments<NumDTensor>;
    using Block2ETileMapKSplit             = BlockToCTileMap_LinearKSplit<MPerBlock, NPerBlock>;
    static constexpr index_t DefaultKBatch = 1;

    // Argument
    struct Argument : public BaseArgument
    {

        Argument(std::vector<const void*>& p_As,
                 std::vector<const void*>& p_Bs,
                 std::vector<std::array<const void*, NumDTensor>>& p_Ds,
                 std::vector<void*>& p_Es,
                 std::vector<GemmDesc>& gemm_descs,
                 AElementwiseOperation a_element_op,
                 BElementwiseOperation b_element_op,
                 CDEElementwiseOperation cde_element_op,
                 int occupancy_num_blocks,
                 int gpu_cu_count)
            : Argument(p_As,
                       p_Bs,
                       p_Ds,
                       p_Es,
                       gemm_descs,
                       a_element_op,
                       b_element_op,
                       cde_element_op,
                       DefaultKBatch,
                       occupancy_num_blocks,
                       gpu_cu_count)
        {
        }

        Argument(std::vector<const void*>& p_As,
                 std::vector<const void*>& p_Bs,
                 std::vector<std::array<const void*, NumDTensor>>& p_Ds,
                 std::vector<void*>& p_Es,
                 std::vector<GemmDesc>& gemm_descs,
                 AElementwiseOperation a_element_op,
                 BElementwiseOperation b_element_op,
                 CDEElementwiseOperation cde_element_op,
                 index_t kbatch,
                 int occupancy_num_blocks,
                 int gpu_cu_count)
            : K_BATCH{kbatch},
              group_count_{0},
              skipped_group_count_{0},
              tile_count_{0},
              occupancy_num_blocks_{occupancy_num_blocks},
              gpu_cu_count_{gpu_cu_count},
              a_element_op_{a_element_op},
              b_element_op_{b_element_op},
              cde_element_op_{cde_element_op}
        {
            group_count_ = ck::type_convert<ck::index_t>(gemm_descs.size());

            if(!(group_count_ == ck::type_convert<ck::index_t>(p_As.size()) &&
                 group_count_ == ck::type_convert<ck::index_t>(p_Bs.size()) &&
                 group_count_ == ck::type_convert<ck::index_t>(p_Es.size())))
            {
                throw std::runtime_error("Error! group_count_ != p_As/Bs/Ds/Es size");
            }

            gemm_kernel_args_.reserve(group_count_);

            for(std::size_t i = 0; i < gemm_descs.size(); ++i)
            {
                const index_t M = gemm_descs[i].M_;
                const index_t N = gemm_descs[i].N_;
                const index_t K = gemm_descs[i].K_;

                if(M * N * K == 0)
                {
                    skipped_group_count_++;
                    continue;
                }

                const index_t stride_a = gemm_descs[i].stride_A_;
                const index_t stride_b = gemm_descs[i].stride_B_;
                const index_t stride_e = gemm_descs[i].stride_C_;

                auto b2c_tile_map           = Block2ETileMapKSplit{M, N, K_BATCH};
                const index_t grid_size_grp = b2c_tile_map.CalculateGridSize(M, N);
                tile_count_ += grid_size_grp;

                std::array<index_t, NumDTensor> stride_ds;

                static_for<0, NumDTensor, 1>{}([&](auto j) {
                    if(gemm_descs[i].stride_Ds_.size() != NumDTensor)
                    {
                        throw std::runtime_error(
                            "Error! gemm_descs[i].stride_Ds_.size() does not match NumDTensor");
                    }

                    stride_ds[j] = gemm_descs[i].stride_Ds_[j];
                });

                gemm_kernel_args_.emplace_back(type_convert<const ADataType*>(p_As[i]),
                                               type_convert<const BDataType*>(p_Bs[i]),
                                               p_Ds[i],
                                               type_convert<EDataType*>(p_Es[i]),
                                               M,
                                               N,
                                               K,
                                               stride_a,
                                               stride_b,
                                               stride_ds,
                                               stride_e);
            }
        }

        /**
         * @brief      Set new kbatch value.
         *
         * @param[in]  kbatch  The new splitK parameter value.
         */
        void UpdateKBatch(index_t kbatch)
        {
            K_BATCH     = kbatch;
            tile_count_ = 0;

            for(std::size_t i = 0; i < gemm_kernel_args_.size(); ++i)
            {
                const auto& gemm_arg = gemm_kernel_args_[i];

                const auto b2c_tile_map = Block2ETileMapKSplit{gemm_arg.M, gemm_arg.N, K_BATCH};
                const index_t grid_size_grp =
                    b2c_tile_map.CalculateGridSize(gemm_arg.M, gemm_arg.N);
                tile_count_ += grid_size_grp;
            }
        }

        //  private:
        index_t K_BATCH;
        index_t group_count_;
        index_t skipped_group_count_;
        // The overall number of output tiles to be processed.
        index_t tile_count_;
        const void* p_dev_gemm_args_;

        int occupancy_num_blocks_;
        int gpu_cu_count_;

        AElementwiseOperation a_element_op_;
        BElementwiseOperation b_element_op_;
        CDEElementwiseOperation cde_element_op_;

        std::vector<KernelArguments> gemm_kernel_args_;
    };

    struct KernelConfig
    {
        // The oversubscription factor for the number of blocks that can simultaneously reside on
        // GPU.
        static constexpr int BLOCK_SUBSCRIPTION_FACTOR = 1;
        static constexpr int BLOCK_WAVES               = BlockSize / get_warp_size();
        static constexpr int CU_SIMDS                  = 4;
        // Assume we want to have at most 2 waves per SIMD
        static constexpr int CU_BLOCKS = math::integer_divide_floor(2 * CU_SIMDS, BLOCK_WAVES);
    };

    // Invoker
    struct Invoker : public BaseInvoker
    {
        ///
        /// @brief      Launch Grouped Gemm kernel.
        ///
        /// @note       This function overload is using user provided device buffer for kernel
        ///             arguments.
        ///
        /// @param[in]  arg                 The structure containing kernel arguments (in host
        ///                                 memory).
        /// @param[in]  dev_gemm_args       The pointer to device memory with kernel arguments.
        /// @param[in]  dev_gemm_workspace  The pointer to device memory for kernel auxiliary
        ///                                 workspace.
        /// @param[in]  stream_config       The device stream configuration.
        ///
        /// @return     The average kernel execution time (if time measurement is enabled.)
        ///
        float Run(const Argument& arg,
                  const void* dev_gemm_args,
                  void* dev_gemm_workspace,
                  const StreamConfig& stream_config = StreamConfig{})
        {
            auto [all_have_kbatch_gt_one, all_have_main_k_block_loop] =
                CheckArgument(arg, stream_config);

            if(dev_gemm_args == nullptr)
            {
                std::ostringstream err;
                err << "The gemm arguments device buffer is not allocated!"
                    << " In " << __FILE__ << ":" << __LINE__ << ", in function: " << __func__;
                throw std::runtime_error(err.str());
            }

            if(dev_gemm_workspace == nullptr)
            {
                std::ostringstream err;
                err << "The gemm workspace buffer is not allocated!"
                    << " In " << __FILE__ << ":" << __LINE__ << ", in function: " << __func__;
                throw std::runtime_error(err.str());
            }

            float ave_time = 0;

            if(all_have_main_k_block_loop)
            {
                ave_time =
                    DispatchKernel<true>(arg, dev_gemm_args, dev_gemm_workspace, stream_config);
            }
            else
            {
                ave_time =
                    DispatchKernel<false>(arg, dev_gemm_args, dev_gemm_workspace, stream_config);
            }

            return ave_time;
        }

        ///
        /// @brief      Launch Grouped Gemm kernel.
        ///
        /// @note       This function overload is using device buffers (for kernel arguments and
        ///             for kernel auxiliary workspace) provided with an argument. The user should
        ///             call @see GetDeviceKernelArgSize, @see GetWorkSpaceSize and @see
        ///             SetDeviceKernelArgs, @see SetWorkSpacePointer on arg parameter to properly
        ///             allocate those buffers.
        ///
        /// @param[in]  arg            The structure containing kernel arguments (in host memory).
        /// @param[in]  stream_config  The device stream configuration.
        ///
        /// @return     The average kernel execution time (if time measurement is enabled.)
        ///
        float Run(const Argument& arg, const StreamConfig& stream_config = StreamConfig{})
        {
            if(arg.p_dev_gemm_args_ == nullptr)
            {
                std::ostringstream err;
                err << "The gemm arguments device buffer is not allocated!"
                    << " In " << __FILE__ << ":" << __LINE__ << ", in function: " << __func__;
                throw std::runtime_error(err.str());
            }

            if(arg.p_workspace_ == nullptr)
            {
                std::ostringstream err;
                err << "The gemm workspace buffer is not allocated!"
                    << " In " << __FILE__ << ":" << __LINE__ << ", in function: " << __func__;
                throw std::runtime_error(err.str());
            }

            return Run(arg, arg.p_dev_gemm_args_, arg.p_workspace_, stream_config);
        }

        float Run(const BaseArgument* p_arg,
                  const StreamConfig& stream_config = StreamConfig{}) override
        {
            return Run(*dynamic_cast<const Argument*>(p_arg), stream_config);
        }

        private:
        auto CheckArgument(const Argument& arg, const StreamConfig& stream_config) const
        {
            bool all_have_kbatch_gt_one, all_have_main_k_block_loop;

            {
                const auto a_grid_desc_kbatch_ak0_m_ak1 =
                    GridwiseGemm::MakeAGridDescriptor_KBatch_AK0_M_AK1(
                        arg.gemm_kernel_args_[0].M,
                        arg.gemm_kernel_args_[0].K,
                        arg.gemm_kernel_args_[0].StrideA,
                        arg.K_BATCH);

                all_have_kbatch_gt_one     = arg.K_BATCH > 1;
                all_have_main_k_block_loop = GridwiseGemm::CalculateHasMainKBlockLoop(
                    a_grid_desc_kbatch_ak0_m_ak1.GetLength(I1) *
                    a_grid_desc_kbatch_ak0_m_ak1.GetLength(I3));
            }

            for(std::size_t i = 0; i < arg.gemm_kernel_args_.size(); ++i)
            {
                const auto& gemm_arg = arg.gemm_kernel_args_[i];
                if(stream_config.log_level_ > 0)
                {
                    gemm_arg.Print();
                }

                // Currently all groups use same kbatch value.
                auto kbatch = arg.K_BATCH;

                if(!GridwiseGemm::CheckValidity(gemm_arg.M,
                                                gemm_arg.N,
                                                gemm_arg.K,
                                                gemm_arg.StrideA,
                                                gemm_arg.StrideB,
                                                gemm_arg.StrideDs,
                                                gemm_arg.StrideE,
                                                kbatch))
                {
                    std::ostringstream err;
                    err << "Group id: " << i << " has invalid GridwiseGemm settings!" << __FILE__
                        << ":" << __LINE__ << ", in function: " << __func__;
                    throw std::runtime_error(err.str());
                }

                const auto a_grid_desc_kbatch_ak0_m_ak1 =
                    GridwiseGemm::MakeAGridDescriptor_KBatch_AK0_M_AK1(
                        arg.gemm_kernel_args_[0].M,
                        arg.gemm_kernel_args_[0].K,
                        arg.gemm_kernel_args_[0].StrideA,
                        arg.K_BATCH);

                bool not_all_have_main_k_block_loop_same =
                    all_have_main_k_block_loop xor GridwiseGemm::CalculateHasMainKBlockLoop(
                                                       a_grid_desc_kbatch_ak0_m_ak1.GetLength(I1) *
                                                       a_grid_desc_kbatch_ak0_m_ak1.GetLength(I3));
                bool not_all_have_kbatch_value_same = all_have_kbatch_gt_one xor (kbatch > 1);

                if(not_all_have_main_k_block_loop_same)
                {
                    std::ostringstream err;
                    err << "Not all gemms have same value for main_k0_block_loop! in " << __FILE__
                        << ":" << __LINE__ << ", in function: " << __func__;
                    throw std::runtime_error(err.str());
                }

                if(not_all_have_kbatch_value_same)
                {
                    std::ostringstream err;
                    err << "Not all gemms have same kbatch value (=1 or >1)! "
                        << "group [" << i << "], kbatch: " << kbatch
                        << ", group [0], kbatch: " << arg.K_BATCH << " in " << __FILE__ << ":"
                        << __LINE__ << ", in function: " << __func__;
                    throw std::runtime_error(err.str());
                }
            }
            return std::make_tuple(all_have_kbatch_gt_one, all_have_main_k_block_loop);
        }

        template <bool HasMainKBlockLoop>
        float DispatchKernel(const Argument& arg,
                             const void* dev_gemm_args,
                             void* dev_gemm_workspace,
                             const StreamConfig& stream_config) const
        {
            const auto kernel = kernel_grouped_gemm_xdl_splitk_v2<GridwiseGemm,
                                                                  KernelArguments,
                                                                  ADataType,
                                                                  BDataType,
                                                                  EDataType,
                                                                  Block2ETileMapKSplit,
                                                                  AElementwiseOperation,
                                                                  BElementwiseOperation,
                                                                  CDEElementwiseOperation,
                                                                  HasMainKBlockLoop>;
            return LaunchKernel(kernel, arg, dev_gemm_args, dev_gemm_workspace, stream_config);
        }

        template <typename KernelFunction>
        int CalculateMaxOccupancyGridSize(const KernelFunction& kernel,
                                          const StreamConfig& stream_config) const
        {
            // Calculate max number of workgroups that can simultaneously reside on the CU.
            int occ_num_blocks            = 0;
            size_t dyn_shared_mem_per_blk = 0;
            hip_check_error(hipOccupancyMaxActiveBlocksPerMultiprocessor(
                &occ_num_blocks, kernel, BlockSize, dyn_shared_mem_per_blk));

            int cu_count = getAvailableComputeUnitCount(stream_config);

            if(stream_config.log_level_ > 0)
            {
                std::cout << "MaxActiveBlocksPerCU: " << occ_num_blocks
                          << ", available CUs count: " << cu_count << ", occup. grid size: "
                          << ck::math::min(occ_num_blocks, KernelConfig::CU_BLOCKS) * cu_count
                          << std::endl;
            }

            return cu_count * ck::math::min(occ_num_blocks, KernelConfig::CU_BLOCKS);
        }

        template <typename KernelFunction>
        float LaunchKernel(const KernelFunction& kernel,
                           const Argument& arg,
                           const void* dev_gemm_args,
                           void* dev_gemm_workspace,
                           const StreamConfig& stream_config) const
        {
            int max_occupancy_grid_size = CalculateMaxOccupancyGridSize(kernel, stream_config);

            // We launch the smaller number of workgroups from acutally needed tiles and the
            // number of workgroups that maximize the GPU occupancy. That is because for some tile
            // configuration the first is smaller than the latter. Launching too many workgroups
            // mean some of them will have to iterate through all gemm problem descriptors just to
            // find out they have nothing to do which is of course waste of GPU cycles.
            if(stream_config.log_level_ > 0)
            {
                const index_t grid_size = ck::math::min(arg.tile_count_, max_occupancy_grid_size);
                const index_t tiles_per_block = (arg.tile_count_ + grid_size - 1) / grid_size;
                std::cout << "tile_count: " << arg.tile_count_
                          << ", tiles_per_block: " << tiles_per_block << std::endl;
            }

            return launch_and_time_kernel(
                stream_config,
                kernel,
                dim3(ck::math::min(arg.tile_count_, max_occupancy_grid_size)),
                dim3(BlockSize),
                0,
                cast_pointer_to_constant_address_space(dev_gemm_args),
                dev_gemm_workspace,
                arg.tile_count_,
                arg.K_BATCH,
                arg.a_element_op_,
                arg.b_element_op_,
                arg.cde_element_op_);
        }
    };

    static constexpr bool IsValidCompilationParameter()
    {
        // TODO: properly implement this check
        return true;
    }

    static bool IsSupportedArgument(const Argument& arg)
    {
        if((ck::type_convert<ck::index_t>(arg.gemm_kernel_args_.size()) +
            arg.skipped_group_count_) != arg.group_count_)
        {
#if DEBUG_LOG
            std::cout << "The group count is not equal to sum of skipped groups "
                         "and kernel args size!"
                      << std::endl;
#endif // DEBUG_LOG
            return false;
        }

        bool supported = true;
        for(std::size_t i = 0; i < arg.gemm_kernel_args_.size(); ++i)
        {
            const auto& gemm_arg = arg.gemm_kernel_args_[i];
            bool group_arg_valid = GridwiseGemm::CheckValidity(gemm_arg.M,
                                                               gemm_arg.N,
                                                               gemm_arg.K,
                                                               gemm_arg.StrideA,
                                                               gemm_arg.StrideB,
                                                               gemm_arg.StrideDs,
                                                               gemm_arg.StrideE,
                                                               arg.K_BATCH);
            if(not group_arg_valid)
            {
#if DEBUG_LOG
                std::cout << "[" << __func__ << "] group id: " << i
                          << " has invalid GridwiseGemm settings!" << std::endl;
                gemm_arg.Print();
#endif // DEBUG_LOG
            }
            supported = supported && group_arg_valid;
        }
        return supported;
    }

    bool IsSupportedArgument(const BaseArgument* p_arg) override
    {
        return IsSupportedArgument(*dynamic_cast<const Argument*>(p_arg));
    }

    static auto MakeArgument(std::vector<const void*>& p_As,
                             std::vector<const void*>& p_Bs,
                             std::vector<std::array<const void*, NumDTensor>>& p_Ds,
                             std::vector<void*>& p_Es,
                             std::vector<GemmDesc> gemm_descs,
                             AElementwiseOperation a_elementwise_op,
                             BElementwiseOperation b_elementwise_op,
                             CDEElementwiseOperation cde_elementwise_op)
    {
        const auto kernel = kernel_grouped_gemm_xdl_splitk_v2<GridwiseGemm,
                                                              KernelArguments,
                                                              ADataType,
                                                              BDataType,
                                                              EDataType,
                                                              Block2ETileMapKSplit,
                                                              AElementwiseOperation,
                                                              BElementwiseOperation,
                                                              CDEElementwiseOperation,
                                                              true>;
        int occupancy, num_cu;
        hip_check_error(
            hipOccupancyMaxActiveBlocksPerMultiprocessor(&occupancy, kernel, BlockSize, 0));

        hipDeviceProp_t dev_prop;
        hipDevice_t dev;
        hip_check_error(hipGetDevice(&dev));
        hip_check_error(hipGetDeviceProperties(&dev_prop, dev));
        num_cu = dev_prop.multiProcessorCount;

        return Argument{p_As,
                        p_Bs,
                        p_Ds,
                        p_Es,
                        gemm_descs,
                        a_elementwise_op,
                        b_elementwise_op,
                        cde_elementwise_op,
                        occupancy,
                        num_cu};
    }

    std::unique_ptr<BaseArgument>
    MakeArgumentPointer(std::vector<const void*>& p_As,
                        std::vector<const void*>& p_Bs,
                        std::vector<std::array<const void*, NumDTensor>>& p_Ds,
                        std::vector<void*>& p_Es,
                        std::vector<GemmDesc>& gemm_descs,
                        AElementwiseOperation a_elementwise_op,
                        BElementwiseOperation b_elementwise_op,
                        CDEElementwiseOperation cde_elementwise_op) override
    {
        const auto kernel = kernel_grouped_gemm_xdl_splitk_v2<GridwiseGemm,
                                                              KernelArguments,
                                                              ADataType,
                                                              BDataType,
                                                              EDataType,
                                                              Block2ETileMapKSplit,
                                                              AElementwiseOperation,
                                                              BElementwiseOperation,
                                                              CDEElementwiseOperation,
                                                              true>;
        int occupancy, num_cu;
        hip_check_error(
            hipOccupancyMaxActiveBlocksPerMultiprocessor(&occupancy, kernel, BlockSize, 0));

        hipDeviceProp_t dev_prop;
        hipDevice_t dev;
        hip_check_error(hipGetDevice(&dev));
        hip_check_error(hipGetDeviceProperties(&dev_prop, dev));
        num_cu = dev_prop.multiProcessorCount;

        return std::make_unique<Argument>(p_As,
                                          p_Bs,
                                          p_Ds,
                                          p_Es,
                                          gemm_descs,
                                          a_elementwise_op,
                                          b_elementwise_op,
                                          cde_elementwise_op,
                                          occupancy,
                                          num_cu);
    }

    static auto MakeInvoker() { return Invoker{}; }

    std::unique_ptr<BaseInvoker> MakeInvokerPointer() override
    {
        return std::make_unique<Invoker>(Invoker{});
    }

    std::string GetTypeString() const override
    {
        auto str = std::stringstream();

        // clang-format off
        str << "DeviceGroupedGemm_XdlSplitKTileLoop"
            << "<"
            << std::string(ALayout::name)[0] << ","
            << std::string(BLayout::name)[0] << ","
            << std::string(ELayout::name)[0] << ","
            << BlockSize << ", "
            << MPerBlock << ", "
            << NPerBlock << ", "
            << KPerBlock << ", "
            << AK1 << ", "
            << BK1 << ", "
            << MPerXDL << ", "
            << NPerXDL << ", "
            << MXdlPerWave << ", "
            << NXdlPerWave << ", "
            << ABlockTransferSrcScalarPerVector << ", "
            << BBlockTransferSrcScalarPerVector << ", "
            << CShuffleMXdlPerWavePerShuffle << ", "
            << CShuffleNXdlPerWavePerShuffle << ", "
            << getGemmSpecializationString(GemmSpec)
            << ">";
        // clang-format on

        return str.str();
    }

    static void SetDeviceKernelArgs(Argument& arg, const void* p_dev_kernel_args)
    {
        arg.p_dev_gemm_args_ = p_dev_kernel_args;
    }

    void SetDeviceKernelArgs(BaseArgument* p_arg, const void* p_dev_kernel_args) const override
    {
        return SetDeviceKernelArgs(*dynamic_cast<Argument*>(p_arg), p_dev_kernel_args);
    }

    size_t GetWorkSpaceSize(const BaseArgument* p_arg) const override
    {
        auto arg = *dynamic_cast<const Argument*>(p_arg);

        int occ_grid_size =
            arg.gpu_cu_count_ * std::min(arg.occupancy_num_blocks_, KernelConfig::CU_BLOCKS);
        int grid_size       = std::min(arg.tile_count_, occ_grid_size);
        int tiles_per_block = (arg.tile_count_ + grid_size - 1) / grid_size;
        int flag_count      = (grid_size * tiles_per_block + arg.K_BATCH - 1) / arg.K_BATCH;

        // This would be the maximum needed workspace size. Since actual grid size, which determines
        // the amount of workspace bytes needed, may be less due to the number of available CUs in
        // stream used to launch kernel.
        size_t size_bytes =
            Block2ETileMapKSplit::GetAccWorkspaceSize(sizeof(AccDataType), grid_size) +
            flag_count * sizeof(uint32_t);
        return size_bytes;
    }

    void SetWorkSpacePointer(BaseArgument* p_arg, void* p_workspace) const override
    {
        auto p_arg_          = dynamic_cast<Argument*>(p_arg);
        p_arg_->p_workspace_ = p_workspace;
    }

    static void SetKBatchSize(Argument& arg, index_t kbatch) { arg.UpdateKBatch(kbatch); }

    void SetKBatchSize(BaseArgument* p_arg, index_t kbatch) const override
    {
        return SetKBatchSize(*dynamic_cast<Argument*>(p_arg), kbatch);
    }

    size_t GetDeviceKernelArgSize(const BaseArgument* p_arg) const override
    {
        return dynamic_cast<const Argument*>(p_arg)->gemm_kernel_args_.size() *
               sizeof(KernelArguments);
    }
};

} // namespace device
} // namespace tensor_operation
} // namespace ck
