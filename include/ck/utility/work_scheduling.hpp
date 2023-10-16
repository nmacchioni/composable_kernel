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

    __device__ bool HasTile() const
    {
        return tile_id_ < tile_count_ && block_tile_idx_ < tiles_per_block_;
    }

    __device__ bool GetNextTile()
    {
        tile_id_++;
        block_tile_idx_++;
        return tile_id_ < tile_count_ && block_tile_idx_ < tiles_per_block_;
    }

    __device__ index_t GetFlagCount(index_t k_tiles) const
    {
        // This is the number of MN-output tiles which we cover with workgroups.
        // We launch k_tiles (k_batch) / tiles_per_block workgroups for each output tile.
        return (get_grid_size() * tiles_per_block_ + k_tiles - 1) / k_tiles;
    }

    ///
    /// @brief      Calculate this workgroup flag index.
    ///
    /// @note       Note this scheduler intentionaly does not have flag index as its member, since
    ///             the number of `k_tiles` may change when iterating (ie. in grouped gemm,
    ///             different groups may have different `k_tiles` in K dimension).
    ///
    /// @param[in]  k_tiles               The number of data tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index (of current GEMM).
    /// @param[in]  output_tile_idx_offset  The output tile index offset.
    ///
    /// @return     The workgroup flag index.
    ///
    __device__ index_t GetWorkgroupFlagIdx(index_t k_tiles,
                                           index_t output_tile_idx,
                                           index_t output_tile_idx_offset) const
    {
        return (output_tile_idx + output_tile_idx_offset) % GetFlagCount(k_tiles);
    }

    ///
    /// @brief      Flag each workgroup that has finished its work.
    ///
    /// @param[in]  k_tiles               The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index
    /// @param[in]  output_tile_idx_offset  The output tile index offset
    ///
    __device__ void
    FlagFinished(index_t k_tiles, index_t output_tile_idx, index_t output_tile_idx_offset)
    {
        finished_block_flags_.inc(
            GetWorkgroupFlagIdx(k_tiles, output_tile_idx, output_tile_idx_offset));
    }

    ///
    /// @brief      Wait until each workgroup has finished its work.
    ///
    /// @param[in]  k_tiles                 The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index
    /// @param[in]  output_tile_idx_offset  The output tile index offset
    ///
    __device__ void
    WaitForNeighbours(index_t k_tiles, index_t output_tile_idx, index_t output_tile_idx_offset)
    {
        // Wait untill all workgroups finish
        const index_t workgroups_per_dim = (k_tiles + tiles_per_block_ - 1) / tiles_per_block_;
        // We use < because for some cases we may have +1 more workgroups per dim.
        // Ie when k_tiles = 5, tiles_per_block = 3.
        finished_block_flags_.wait_lt(
            GetWorkgroupFlagIdx(k_tiles, output_tile_idx, output_tile_idx_offset),
            workgroups_per_dim);
    }

    ///
    /// @brief      Wait until each workgroup has finished its work.
    ///
    /// @param[in]  k_tiles                 The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index
    /// @param[in]  output_tile_idx_offset  The output tile index offset
    ///
    __device__ void
    WaitForReduction(index_t k_tiles, index_t output_tile_idx, index_t output_tile_idx_offset)
    {
        // Wait untill the counter has been reset.
        finished_block_flags_.wait_eq(
            GetWorkgroupFlagIdx(k_tiles, output_tile_idx, output_tile_idx_offset), 0);
    }

    ///
    /// @brief      Reset flag counter to zero.
    ///
    /// @param[in]  k_tiles                 The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index.
    /// @param[in]  output_tile_idx_offset  The output tile index offset.
    ///
    __device__ void Reset(index_t k_tiles, index_t output_tile_idx, index_t output_tile_idx_offset)
    {
        finished_block_flags_.reset(
            GetWorkgroupFlagIdx(k_tiles, output_tile_idx, output_tile_idx_offset));
    }

    ///
    /// @brief      Gets the flag value.
    ///
    /// @param[in]  k_tiles                 The number of tiles in the reduced dimension.
    /// @param[in]  output_tile_idx         The output (MN) tile index.
    /// @param[in]  output_tile_idx_offset  The output tile index offset.
    ///
    __device__ index_t GetFlagValue(index_t k_tiles,
                                    index_t output_tile_idx,
                                    index_t output_tile_idx_offset) const
    {
        return static_cast<index_t>(finished_block_flags_.ld(
            GetWorkgroupFlagIdx(k_tiles, output_tile_idx, output_tile_idx_offset)));
    }

    const index_t tile_count_;
    const index_t tiles_per_block_;
    index_t tile_id_;
    index_t block_tile_idx_;
    workgroup_barrier finished_block_flags_;
};

} // namespace ck
