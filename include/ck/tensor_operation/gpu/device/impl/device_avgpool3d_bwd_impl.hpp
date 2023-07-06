// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2023, Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include <iostream>
#include <sstream>

#include "ck/tensor_description/tensor_descriptor.hpp"
#include "ck/tensor_description/tensor_descriptor_helper.hpp"
#include "ck/tensor_operation/gpu/device/reduction_operator_mapping.hpp"
#include "ck/tensor_operation/gpu/device/device_avgpool_bwd.hpp"
#include "ck/host_utility/device_prop.hpp"
#include "ck/host_utility/kernel_launch.hpp"

namespace ck {
namespace tensor_operation {
namespace device {

template <typename DOutDataType,
          typename DInDataType,
          typename ComputeDataType,
          ck::index_t BlockSize,
          ck::index_t MThreadClusterSize,
          ck::index_t KThreadClusterSize,
          ck::index_t MThreadSliceSize,
          ck::index_t KThreadSliceSize,
          ck::index_t InSrcOutDstVectorSize>
struct DeviceAvgPool3dBwdImpl : public DeviceAvgPoolBwd<DOutDataType, DInDataType>
{
    static constexpr index_t NDimSpatial = 3;

    static constexpr auto I0 = Number<0>{};
    static constexpr auto I1 = Number<1>{};

    static constexpr ck::index_t M_BlockTileSize = MThreadClusterSize * MThreadSliceSize;
    static constexpr ck::index_t K_BlockTileSize = KThreadClusterSize * KThreadSliceSize;

    static auto
    Make3DGridDescriptor_Out_M_K_In_M(const std::vector<ck::index_t>& dout_n_c_wos_lengths,
                                      const std::vector<ck::index_t>& din_n_c_wos_length,
                                      const std::vector<ck::index_t>& dout_n_c_wos_strides,
                                      const std::vector<ck::index_t>& din_n_c_wos_strides,
                                      const std::vector<ck::index_t>& window_lengths,
                                      const std::vector<ck::index_t>& window_strides,
                                      const std::vector<ck::index_t>& window_dilations,
                                      const std::vector<ck::index_t>& input_left_pads,
                                      const std::vector<ck::index_t>& input_right_pads,
                                      const std::vector<ck::index_t>& tildes)
    {
        index_t i_ztilde = tildes[0];
        index_t i_ytilde = tildes[1];
        index_t i_xtilde = tildes[2];

        const index_t N = dout_n_c_wos_lengths[0];
        const index_t C = dout_n_c_wos_lengths[1];

        const index_t Di = din_n_c_wos_length[2];
        const index_t Hi = din_n_c_wos_length[3];
        const index_t Wi = din_n_c_wos_length[4];

        const index_t Do = dout_n_c_wos_lengths[2];
        const index_t Ho = dout_n_c_wos_lengths[3];
        const index_t Wo = dout_n_c_wos_lengths[4];

        const index_t Z = window_lengths[0];
        const index_t Y = window_lengths[1];
        const index_t X = window_lengths[2];

        const index_t InLeftPadD = input_left_pads[0];
        const index_t InLeftPadH = input_left_pads[1];
        const index_t InLeftPadW = input_left_pads[2];

        const index_t InRightPadD = input_right_pads[0];
        const index_t InRightPadH = input_right_pads[1];
        const index_t InRightPadW = input_right_pads[2];

        const index_t ConvStrideD = window_strides[0];
        const index_t ConvStrideH = window_strides[1];
        const index_t ConvStrideW = window_strides[2];

        const index_t ConvDilationD = window_dilations[0];
        const index_t ConvDilationH = window_dilations[1];
        const index_t ConvDilationW = window_dilations[2];

        const auto out_n_do_ho_wo_c_grid_desc =
            make_naive_tensor_descriptor(make_tuple(N, Do, Ho, Wo, C),
                                         make_tuple(dout_n_c_wos_strides[0],
                                                    dout_n_c_wos_strides[2],
                                                    dout_n_c_wos_strides[3],
                                                    dout_n_c_wos_strides[4],
                                                    dout_n_c_wos_strides[1]));

        const auto GcdStrideDilationD = math::gcd(ConvStrideD, ConvDilationD);
        const auto GcdStrideDilationH = math::gcd(ConvStrideH, ConvDilationH);
        const auto GcdStrideDilationW = math::gcd(ConvStrideW, ConvDilationW);

        const auto ZTilde = ConvStrideD / GcdStrideDilationD;
        const auto YTilde = ConvStrideH / GcdStrideDilationH;
        const auto XTilde = ConvStrideW / GcdStrideDilationW;

        const auto ZDot = math::integer_divide_ceil(Z, ZTilde);
        const auto YDot = math::integer_divide_ceil(Y, YTilde);
        const auto XDot = math::integer_divide_ceil(X, XTilde);

        const auto DTilde = Do + math::integer_divide_ceil(ConvDilationD * (Z - I1), ConvStrideD);
        const auto HTilde = Ho + math::integer_divide_ceil(ConvDilationH * (Y - I1), ConvStrideH);
        const auto WTilde = Wo + math::integer_divide_ceil(ConvDilationW * (X - I1), ConvStrideW);

        // only work on Tildes that contribute to non-padding area of input tensor
        const auto IDTildeSliceBegin = math::integer_divide_floor(
            math::max(I0, InLeftPadD - ConvDilationD * (ZTilde - I1)), ConvStrideD);
        const auto IHTildeSliceBegin = math::integer_divide_floor(
            math::max(I0, InLeftPadH - ConvDilationH * (YTilde - I1)), ConvStrideH);
        const auto IWTildeSliceBegin = math::integer_divide_floor(
            math::max(I0, InLeftPadW - ConvDilationW * (XTilde - I1)), ConvStrideW);

        const auto IDTildeSliceEnd =
            math::min(DTilde, math::integer_divide_ceil(InLeftPadD + Di - I1, ConvStrideD) + I1);
        const auto IHTildeSliceEnd =
            math::min(HTilde, math::integer_divide_ceil(InLeftPadH + Hi - I1, ConvStrideH) + I1);
        const auto IWTildeSliceEnd =
            math::min(WTilde, math::integer_divide_ceil(InLeftPadW + Wi - I1, ConvStrideW) + I1);

        const auto DTildeSlice = IDTildeSliceEnd - IDTildeSliceBegin;
        const auto HTildeSlice = IHTildeSliceEnd - IHTildeSliceBegin;
        const auto WTildeSlice = IWTildeSliceEnd - IWTildeSliceBegin;

        // ReduceK is different for each Reduce
        const auto ZDotSlice = math::integer_divide_ceil(Z - i_ztilde, ZTilde);
        const auto YDotSlice = math::integer_divide_ceil(Y - i_ytilde, YTilde);
        const auto XDotSlice = math::integer_divide_ceil(X - i_xtilde, XTilde);

        // Out[ReduceM, ReduceK]
        const auto out_n_dop_hop_wop_c_grid_desc = transform_tensor_descriptor(
            out_n_do_ho_wo_c_grid_desc,
            make_tuple(make_pass_through_transform(N),
                       make_pad_transform(Do, I0, I0),
                       make_pad_transform(Ho, I0, I0),
                       make_pad_transform(Wo, I0, I0),
                       make_pass_through_transform(C)),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}));

        const auto out_n_zdot_dtilde_ydot_htilde_xdot_wtilde_c_grid_desc =
            transform_tensor_descriptor(
                out_n_dop_hop_wop_c_grid_desc,
                make_tuple(
                    make_pass_through_transform(N),
                    make_embed_transform(make_tuple(ZDot, DTilde),
                                         make_tuple(-ConvDilationD / GcdStrideDilationD, I1)),
                    make_embed_transform(make_tuple(YDot, HTilde),
                                         make_tuple(-ConvDilationH / GcdStrideDilationH, I1)),
                    make_embed_transform(make_tuple(XDot, WTilde),
                                         make_tuple(-ConvDilationW / GcdStrideDilationW, I1)),
                    make_pass_through_transform(C)),
                make_tuple(
                    Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
                make_tuple(Sequence<0>{},
                           Sequence<1, 2>{},
                           Sequence<3, 4>{},
                           Sequence<5, 6>{},
                           Sequence<7>{}));

        const auto
            out_n_zdotslice_dtildeslice_ydotslice_htildeslice_xdotslice_wtildeslice_c_grid_desc =
                transform_tensor_descriptor(
                    out_n_zdot_dtilde_ydot_htilde_xdot_wtilde_c_grid_desc,
                    make_tuple(make_pass_through_transform(N),
                               make_slice_transform(ZDot, I0, ZDotSlice),
                               make_slice_transform(DTilde, IDTildeSliceBegin, DTildeSlice),
                               make_slice_transform(YDot, I0, YDotSlice),
                               make_slice_transform(HTilde, IHTildeSliceBegin, HTildeSlice),
                               make_slice_transform(XDot, I0, XDotSlice),
                               make_slice_transform(WTilde, IWTildeSliceBegin, WTildeSlice),
                               make_pass_through_transform(C)),
                    make_tuple(Sequence<0>{},
                               Sequence<1>{},
                               Sequence<2>{},
                               Sequence<3>{},
                               Sequence<4>{},
                               Sequence<5>{},
                               Sequence<6>{},
                               Sequence<7>{}),
                    make_tuple(Sequence<0>{},
                               Sequence<1>{},
                               Sequence<2>{},
                               Sequence<3>{},
                               Sequence<4>{},
                               Sequence<5>{},
                               Sequence<6>{},
                               Sequence<7>{}));

        const auto out_grid_desc_reducemraw_reducekraw = transform_tensor_descriptor(
            out_n_zdotslice_dtildeslice_ydotslice_htildeslice_xdotslice_wtildeslice_c_grid_desc,
            make_tuple(
                make_merge_transform(make_tuple(N, DTildeSlice, HTildeSlice, WTildeSlice, C)),
                make_merge_transform(make_tuple(ZDotSlice, YDotSlice, XDotSlice))),
            make_tuple(Sequence<0, 2, 4, 6, 7>{}, Sequence<1, 3, 5>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}));

        const index_t MRaw = N * DTildeSlice * HTildeSlice * WTildeSlice * C;
        const index_t MPad = math::integer_least_multiple(MRaw, M_BlockTileSize) - MRaw;

        const index_t KRaw = ZDotSlice * YDotSlice * XDotSlice;
        const index_t KPad = math::integer_least_multiple(KRaw, K_BlockTileSize) - KRaw;

        const auto out_grid_desc_reducem_reducek = transform_tensor_descriptor(
            out_grid_desc_reducemraw_reducekraw,
            make_tuple(make_right_pad_transform(MRaw, MPad), make_right_pad_transform(KRaw, KPad)),
            make_tuple(Sequence<0>{}, Sequence<1>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}));

        // In[ReduceM]
        const auto in_n_di_hi_wi_c_grid_desc =
            make_naive_tensor_descriptor(make_tuple(N, Di, Hi, Wi, C),
                                         make_tuple(din_n_c_wos_strides[0],
                                                    din_n_c_wos_strides[2],
                                                    din_n_c_wos_strides[3],
                                                    din_n_c_wos_strides[4],
                                                    din_n_c_wos_strides[1]));

        const auto in_n_dip_hip_wip_c_grid_desc = transform_tensor_descriptor(
            in_n_di_hi_wi_c_grid_desc,
            make_tuple(make_pass_through_transform(N),
                       make_pad_transform(Di, InLeftPadD, InRightPadD),
                       make_pad_transform(Hi, InLeftPadH, InRightPadH),
                       make_pad_transform(Wi, InLeftPadW, InRightPadW),
                       make_pass_through_transform(C)),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}));

        const auto in_n_ztilde_dtilde_ytilde_htilde_xtilde_wtilde_c_grid_desc =
            transform_tensor_descriptor(
                in_n_dip_hip_wip_c_grid_desc,
                make_tuple(make_pass_through_transform(N),
                           make_embed_transform(make_tuple(XTilde, DTilde),
                                                make_tuple(ConvDilationD, ConvStrideD)),
                           make_embed_transform(make_tuple(YTilde, HTilde),
                                                make_tuple(ConvDilationH, ConvStrideH)),
                           make_embed_transform(make_tuple(XTilde, WTilde),
                                                make_tuple(ConvDilationW, ConvStrideW)),
                           make_pass_through_transform(C)),
                make_tuple(
                    Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
                make_tuple(Sequence<0>{},
                           Sequence<1, 2>{},
                           Sequence<3, 4>{},
                           Sequence<5, 6>{},
                           Sequence<7>{}));

        const auto in_n_dtildeslice_htildeslice_wtildeslice_c_grid_desc =
            transform_tensor_descriptor(
                in_n_ztilde_dtilde_ytilde_htilde_xtilde_wtilde_c_grid_desc,
                make_tuple(make_pass_through_transform(N),
                           make_freeze_transform(i_ztilde),
                           make_slice_transform(DTilde, IDTildeSliceBegin, DTildeSlice),
                           make_freeze_transform(i_ytilde),
                           make_slice_transform(HTilde, IHTildeSliceBegin, HTildeSlice),
                           make_freeze_transform(i_xtilde),
                           make_slice_transform(WTilde, IWTildeSliceBegin, WTildeSlice),
                           make_pass_through_transform(C)),
                make_tuple(Sequence<0>{},
                           Sequence<1>{},
                           Sequence<2>{},
                           Sequence<3>{},
                           Sequence<4>{},
                           Sequence<5>{},
                           Sequence<6>{},
                           Sequence<7>{}),
                make_tuple(Sequence<0>{},
                           Sequence<>{},
                           Sequence<1>{},
                           Sequence<>{},
                           Sequence<2>{},
                           Sequence<>{},
                           Sequence<3>{},
                           Sequence<4>{}));

        const auto in_grid_desc_reducemraw = transform_tensor_descriptor(
            in_n_dtildeslice_htildeslice_wtildeslice_c_grid_desc,
            make_tuple(
                make_merge_transform(make_tuple(N, DTildeSlice, HTildeSlice, WTildeSlice, C))),
            make_tuple(Sequence<0, 1, 2, 3, 4>{}),
            make_tuple(Sequence<0>{}));

        const auto in_grid_desc_reducem =
            transform_tensor_descriptor(in_grid_desc_reducemraw,
                                        make_tuple(make_right_pad_transform(MRaw, MPad)),
                                        make_tuple(Sequence<0>{}),
                                        make_tuple(Sequence<0>{}));

        return make_tuple(out_grid_desc_reducem_reducek, in_grid_desc_reducem);
    }

    struct Argument : public BaseArgument
    {
        Argument(const DOutDataType* p_dout,
                 DInDataType* p_din,
                 std::vector<ck::index_t> dout_n_c_wos_lengths,
                 std::vector<ck::index_t> din_n_c_wos_length,
                 std::vector<ck::index_t> dout_n_c_wos_strides,
                 std::vector<ck::index_t> din_n_c_wos_strides,
                 std::vector<ck::index_t> window_lengths,
                 std::vector<ck::index_t> window_strides,
                 std::vector<ck::index_t> window_dilations,
                 std::vector<ck::index_t> input_left_pads,
                 std::vector<ck::index_t> input_right_pads)
            : p_dout_grid_{p_dout}, p_din_grid_{p_din}, num_reduce_{1}
        {
            ignore = p_dout;
            ignore = p_din;
            ignore = dout_n_c_wos_lengths;
            ignore = dout_n_c_wos_strides;
            ignore = din_n_c_wos_length;
            ignore = din_n_c_wos_strides;
            ignore = window_lengths;
            ignore = window_strides;
            ignore = window_dilations;
            ignore = input_left_pads;
            ignore = input_right_pads;

            std::vector<ck::index_t> Tildes(NDimSpatial);
            for(int i = 0; i < NDimSpatial; ++i)
            {
                int GcdStrideDilation = math::gcd(window_strides[i], window_dilations[i]);
                Tildes[i]             = window_strides[i] / GcdStrideDilation;
                num_reduce_ *= Tildes[i];
            }

            for(index_t i_ztilde = 0; i_ztilde < Tildes[0]; ++i_ztilde)
            {
                for(index_t i_ytilde = 0; i_ytilde < Tildes[1]; ++i_ytilde)
                {
                    for(index_t i_xtilde = 0; i_xtilde < Tildes[2]; ++i_xtilde)
                    {
                        // check slice is valid
                        const auto ZDotSlice =
                            math::integer_divide_ceil(window_lengths[0] - i_ztilde, Tildes[0]);
                        const auto YDotSlice =
                            math::integer_divide_ceil(window_lengths[1] - i_ytilde, Tildes[1]);
                        const auto XDotSlice =
                            math::integer_divide_ceil(window_lengths[2] - i_xtilde, Tildes[2]);

                        if(ZDotSlice * YDotSlice * XDotSlice <= 0)
                        {
                            continue;
                        }
                    }
                }
            }
        }

        // pointer
        const DOutDataType* p_dout_grid_;
        DInDataType* p_din_grid_;

        int num_reduce_;
    };

    struct Invoker : public BaseInvoker
    {
        float Run(const BaseArgument* p_arg,
                  const StreamConfig& stream_config = StreamConfig{}) override
        {
            ignore = p_arg;
            ignore = stream_config;
            return 0;
        }
    };

    static bool IsSupportedArgument(const Argument& arg)
    {
        ignore = arg;
        return true;
    }

    bool IsSupportedArgument(const BaseArgument* p_arg) override
    {
        return IsSupportedArgument(*dynamic_cast<const Argument*>(p_arg));
    }

    std::unique_ptr<BaseArgument>
    MakeArgumentPointer(const void* p_dout,
                        void* p_din,
                        std::vector<ck::index_t> dout_n_c_wos_lengths,
                        std::vector<ck::index_t> din_n_c_wos_length,
                        std::vector<ck::index_t> dout_n_c_wos_strides,
                        std::vector<ck::index_t> din_n_c_wos_strides,
                        std::vector<ck::index_t> window_lengths,
                        std::vector<ck::index_t> window_strides,
                        std::vector<ck::index_t> window_dilations,
                        std::vector<ck::index_t> input_left_pads,
                        std::vector<ck::index_t> input_right_pads) override
    {
        return std::make_unique<Argument>(static_cast<const DOutDataType*>(p_dout),
                                          static_cast<DInDataType*>(p_din),
                                          dout_n_c_wos_lengths,
                                          din_n_c_wos_length,
                                          dout_n_c_wos_strides,
                                          din_n_c_wos_strides,
                                          window_lengths,
                                          window_strides,
                                          window_dilations,
                                          input_left_pads,
                                          input_right_pads);
    }

    std::unique_ptr<BaseInvoker> MakeInvokerPointer() override
    {
        return std::make_unique<Invoker>(Invoker{});
    }

    std::string GetTypeString() const override
    {
        auto str = std::stringstream();

        // clang-format off
        str << "DeviceAvgPool3dBwd<" << BlockSize << ",";
        str << "M_C" << MThreadClusterSize << "_S" << MThreadSliceSize << ",";
        str << "K_C" << KThreadClusterSize << "_S" << KThreadSliceSize << ",";
        str <<"InSrcOutDstVectorSize_" << InSrcOutDstVectorSize << ">";
        // clang-format on

        return str.str();
    }
};

} // namespace device
} // namespace tensor_operation
} // namespace ck
