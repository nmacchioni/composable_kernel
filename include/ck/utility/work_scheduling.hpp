// SPDX-License-Identifier: MIT
// Copyright (c) 2023, Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include "ck/ck.hpp"
#include "ck/utility/data_type.hpp"
#include "ck/utility/workgroup_barrier.hpp"

namespace ck {

enum struct WorkSchedulingPolicy
{
    StridedTileLoop
};

///
/// @brief      This class describes a strided reduction tile loop work scheduling.
///
///
/// @par Overview
///     This work scheduling policy assume linear mapping (with stride) of workgroups along
///     the reduced dimension. In GEMM problem this mean that consecutive workgroups are mapped
///     to strided data tiles along K dimension. This can be obtained using i.e.
///     @see BlockToCTileMap_ReduceKSplit.
///
/// @par Synchronization
///     All workgroups aligned along particular reduced dimension have to reduce their partial
///     results. In order to do that there's a need to use global flags and atomics to communicate
///     between those workgroups.
///
class StridedReductionTileLoop
{
    public:
    __device__ StridedReductionTileLoop(index_t tile_count,
                                        uint32_t* const __restrict__ p_flag_count)
        : tile_count_{tile_count},
          tiles_per_block_{(tile_count_ + get_grid_size() - 1) / get_grid_size()},
          tile_id_{get_block_1d_id() * tiles_per_block_},
          block_tile_idx_{0},
          finished_block_flags_{p_flag_count}
    {
    }

    __device__ bool GetNextTile()
    {
        tile_id_++;
        block_tile_idx_++;
        return tile_id_ < tile_count_ && block_tile_idx_ < tiles_per_block_;
    }

    ///
    /// @brief      Calculate this workgroup flag index.
    ///
    /// @note       Note this scheduler intentionaly does not have flag index as its member, since
    ///             the number of `dim_tiles` may change when iterating (ie. in grouped gemm,
    ///             different groups may have different `dim_tiles` in K dimension).
    ///
    /// @param[in]  dim_tiles        The number of data tiles in the reduced dimension.
    /// @param[in]  output_tile_idx  The output (MN) tile index.
    ///
    /// @return     The workgroup flag index.
    ///
    __device__ index_t GetWorkgroupFlagIdx(index_t dim_tiles, index_t output_tile_idx) const
    {
        // This is the number of MN-output tiles which we cover with workgroups.
        // We launch dim_tiles (k_batch) / tiles_per_block workgroups for each output tile.
        const index_t flag_count = (get_grid_size() * tiles_per_block_ + dim_tiles - 1) / dim_tiles;
        return output_tile_idx % flag_count;
    }

    ///
    /// @brief      Flag each workgroup that has finished its work.
    ///
    /// @param[in]  dim_tiles        The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx  The output (MN) tile index
    ///
    __device__ void FlagFinished(index_t dim_tiles, index_t output_tile_idx)
    {
        finished_block_flags_.inc(GetWorkgroupFlagIdx(dim_tiles, output_tile_idx));
    }

    ///
    /// @brief      Wait until each workgroup has finished its work.
    ///
    /// @param[in]  dim_tiles        The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx  The output (MN) tile index
    ///
    __device__ void WaitForNeighbours(index_t dim_tiles, index_t output_tile_idx)
    {
        // Wait untill all workgroups finish and reset counter.
        const index_t workgroups_per_dim = (dim_tiles + tiles_per_block_ - 1) / tiles_per_block_;
        finished_block_flags_.wait_set(
            GetWorkgroupFlagIdx(dim_tiles, output_tile_idx), workgroups_per_dim, 0);
    }

    const index_t tile_count_;
    const index_t tiles_per_block_;
    index_t tile_id_;
    index_t block_tile_idx_;
    workgroup_barrier finished_block_flags_;
};

} // namespace ck
