#include <stdlib.h>
#include "config.hpp"
#include "device_convnd_bwd_data_xdl_ndhwc_kzyxc_ndhwk.hpp"
#include "element_wise_operation.hpp"
#include "device_operation_instance.hpp"

namespace ck {
namespace tensor_operation {
namespace device {
namespace device_conv2d_bwd_data_instance {

using F32 = float;

template <ck::index_t... Is>
using S = ck::Sequence<Is...>;

using PassThrough = ck::tensor_operation::element_wise::PassThrough;
static constexpr auto ConvBwdDataDefault =
    ck::tensor_operation::device::ConvolutionBackwardDataSpecialization_t::Default;

static constexpr auto ConvBwdDataFilter1x1Stride1Pad0 =
    ck::tensor_operation::device::ConvolutionBackwardDataSpecialization_t::Filter1x1Stride1Pad0;

// Compilation parameters for in[n, hi, wi, c] * wei[k, y, x, c] = out[n, ho, wo, k]
using device_conv1d_bwd_data_xdl_nwc_kxc_nwk_f32_instances =
    std::tuple<
        // clang-format off
        //#############################################################################| InData| WeiData| OutData| AccData|          In|         Wei|         Out|        ConvBackward|    Num| Block|  MPer|  NPer| K0Per| K1| MPer| NPer| MXdl| NXdl|  ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockLds|  BBlockTransfer| BBlockTransfer| BBlockTransfer| BlockTransfer| BBlockTransfer| BBlockTransfer| BBlockLds| CThreadTransfer| CThreadTransfer|
        //#############################################################################|   Type|    Type|    Type|    Type| Elementwise| Elementwise| Elementwise|                Data|    Dim|  Size| Block| Block| Block|   |  XDL|  XDL|  Per|  Per|   ThreadCluster|  ThreadCluster| SrcAccessOrder|   SrcVectorDim|      SrcScalar|      DstScalar| AddExtraM|   ThreadCluster|  ThreadCluster| SrcAccessOrder|  SrcVectorDim|      SrcScalar|      DstScalar| AddExtraN| SrcDstVectorDim|       DstScalar|
        //#############################################################################|       |        |        |        |   Operation|   Operation|   Operation|      Specialization|Spatial|      |      |      |      |   |     |     | Wave| Wave| Lengths_K0_M_K1|   ArrangeOrder|               |               |      PerVector|   PerVector_K1|          | Lengths_K0_N_K1|   ArrangeOrder|               |              |      PerVector|   PerVector_K1|          |                |       PerVector|
        //#############################################################################|       |        |        |        |            |            |            |                    |       |      |      |      |      |   |     |     |     |     |                |               |               |               |               |               |          |                |               |               |              |               |               |          |                |                |
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     256,   256,   128,     4,  4,   32,   32,    4,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     256,   128,   256,     4,  4,   32,   32,    2,    4,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     128,   128,   128,     4,  4,   32,   32,    4,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     256,   128,   128,     4,  4,   32,   32,    2,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     128,   128,    64,     4,  4,   32,   32,    2,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     128,    64,   128,     4,  4,   32,   32,    2,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,      64,    64,    64,     4,  4,   32,   32,    2,    2,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     256,   128,    64,     4,  4,   32,   32,    2,    1,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              1,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     256,    64,   128,     4,  4,   32,   32,    1,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     128,   128,    32,     4,  4,   32,   32,    2,    1,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              1,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,     128,    32,   128,     4,  4,   32,   32,    1,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,      64,    64,    32,     4,  4,   32,   32,    2,    1,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataDefault,   1,      64,    32,    64,     4,  4,   32,   32,    1,    2,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>
        // clang-format on
        >;

using device_conv1d_bwd_data_xdl_nwc_kxc_nwk_1x1_s1_p0_f32_instances =
    std::tuple<
        // clang-format off
        //#############################################################################| InData| WeiData| OutData| AccData|          In|         Wei|         Out|                     ConvBackward|    Num| Block|  MPer|  NPer| K0Per| K1| MPer| NPer| MXdl| NXdl|  ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockTransfer| ABlockLds|  BBlockTransfer| BBlockTransfer| BBlockTransfer| BlockTransfer| BBlockTransfer| BBlockTransfer| BBlockLds| CThreadTransfer| CThreadTransfer|
        //#############################################################################|   Type|    Type|    Type|    Type| Elementwise| Elementwise| Elementwise|                             Data|    Dim|  Size| Block| Block| Block|   |  XDL|  XDL|  Per|  Per|   ThreadCluster|  ThreadCluster| SrcAccessOrder|   SrcVectorDim|      SrcScalar|      DstScalar| AddExtraM|   ThreadCluster|  ThreadCluster| SrcAccessOrder|  SrcVectorDim|      SrcScalar|      DstScalar| AddExtraN| SrcDstVectorDim|       DstScalar|
        //#############################################################################|       |        |        |        |   Operation|   Operation|   Operation|                   Specialization|Spatial|      |      |      |      |   |     |     | Wave| Wave| Lengths_K0_M_K1|   ArrangeOrder|               |               |      PerVector|   PerVector_K1|          | Lengths_K0_N_K1|   ArrangeOrder|               |              |      PerVector|   PerVector_K1|          |                |       PerVector|
        //#############################################################################|       |        |        |        |            |            |            |                                 |       |      |      |      |      |   |     |     |     |     |                |               |               |               |               |               |          |                |               |               |              |               |               |          |                |                |
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     256,   256,   128,     4,  4,   32,   32,    4,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     256,   128,   256,     4,  4,   32,   32,    2,    4,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     128,   128,   128,     4,  4,   32,   32,    4,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     256,   128,   128,     4,  4,   32,   32,    2,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     128,   128,    64,     4,  4,   32,   32,    2,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     128,    64,   128,     4,  4,   32,   32,    2,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,      64,    64,    64,     4,  4,   32,   32,    2,    2,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     256,   128,    64,     4,  4,   32,   32,    2,    1,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              1,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     256,    64,   128,     4,  4,   32,   32,    1,    2,     S<4, 64, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 64, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     128,   128,    32,     4,  4,   32,   32,    2,    1,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              1,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,     128,    32,   128,     4,  4,   32,   32,    1,    2,     S<4, 32, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 32, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,      64,    64,    32,     4,  4,   32,   32,    2,    1,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              2,              4,      true,               7,               1>,
        DeviceConvndBwdDataXdl_Input_N_Di_Hi_Wi_C_Weight_K_Z_Y_X_C_Output_N_Do_Ho_Wo_K<    F32,     F32,     F32,     F32, PassThrough, PassThrough, PassThrough,  ConvBwdDataFilter1x1Stride1Pad0,   1,      64,    32,    64,     4,  4,   32,   32,    1,    2,     S<4, 16, 1>,     S<1, 0, 2>,     S<1, 0, 2>,              2,              4,              4,      true,     S<4, 16, 1>,     S<2, 0, 1>,     S<0, 2, 1>,             1,              4,              4,      true,               7,               1>
        // clang-format on
        >;

void add_device_conv1d_bwd_data_xdl_nwc_kxc_nwk_f32_instances(
    std::vector<DeviceConvBwdDataPtr<PassThrough, PassThrough, PassThrough>>& instances)
{
    add_device_operation_instances(instances,
                                   device_conv1d_bwd_data_xdl_nwc_kxc_nwk_f32_instances{});
    add_device_operation_instances(
        instances, device_conv1d_bwd_data_xdl_nwc_kxc_nwk_1x1_s1_p0_f32_instances{});
}

} // namespace device_conv2d_bwd_data_instance
} // namespace device
} // namespace tensor_operation
} // namespace ck
