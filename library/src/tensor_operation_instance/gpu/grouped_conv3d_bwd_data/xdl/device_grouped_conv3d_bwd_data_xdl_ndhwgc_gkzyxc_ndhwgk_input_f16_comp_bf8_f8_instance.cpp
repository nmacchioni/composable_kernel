// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2023, Advanced Micro Devices, Inc. All rights reserved.

#if defined CK_ENABLE_FP16 && defined CK_ENABLE_FP8 && defined CK_ENABLE_BF8
#include "ck/library/tensor_operation_instance/add_device_operation_instance.hpp"
#include "ck/library/tensor_operation_instance/gpu/grouped_conv_bwd_data/device_grouped_conv_bwd_data_xdl_instance.hpp"

namespace ck {
namespace tensor_operation {
namespace device {
namespace instance {
// Compilation parameters for out[n, di, hi, wi, g, c] * wei[g, k, z, y, x, c] = in[n, do, ho, wo,
// g, k]
void add_device_grouped_conv3d_bwd_data_xdl_ndhwgk_gkzyxc_ndhwgc_input_f16_comp_bf8f8_instances(
    std::vector<std::unique_ptr<DeviceGroupedConvBwdDataMultipleD<3,
                                                                  NDHWGK,
                                                                  GKZYXC,
                                                                  Empty_Tuple,
                                                                  NDHWGC,
                                                                  F16,
                                                                  F16,
                                                                  Empty_Tuple,
                                                                  F16,
                                                                  PassThrough,
                                                                  PassThrough,
                                                                  PassThrough,
                                                                  BF8,
                                                                  F8>>>& instances)
{
    // 1. Default
    add_device_operation_instances(
        instances,
        device_grouped_conv_bwd_data_xdl_input_fp16_comp_bf8f8_instances<3,
                                                                         NDHWGK,
                                                                         GKZYXC,
                                                                         Empty_Tuple,
                                                                         NDHWGC,
                                                                         ConvBwdDataDefault>{});
    // 2. Filter1x1Stride1Pad0
    add_device_operation_instances(instances,
                                   device_grouped_conv_bwd_data_xdl_input_fp16_comp_bf8f8_instances<
                                       3,
                                       NDHWGK,
                                       GKZYXC,
                                       Empty_Tuple,
                                       NDHWGC,
                                       ConvBwdDataFilter1x1Stride1Pad0>{});
}
} // namespace instance
} // namespace device
} // namespace tensor_operation
} // namespace ck
#endif
