#pragma once
#include <iostream>
#include <vector>

#include "device_grouped_gemm.hpp"

namespace ck {
namespace tensor_operation {
namespace device {

struct GemmKernelArgument
{
    const void* p_a_grid;
    const void* p_b_grid;
    void* p_c_grid;

    index_t M;
    index_t N;
    index_t K;
    index_t StrideA;
    index_t StrideB;
    index_t StrideC;
};

template <typename ALayout,
          typename BLayout,
          typename DsLayout,
          typename ELayout,
          typename ADataType,
          typename BDataType,
          typename DsDataType,
          typename EDataType,
          typename AElementwiseOperation,
          typename BElementwiseOperation,
          typename CElementwiseOperation>
struct DeviceGroupedGemmSplitK : public DeviceGroupedGemm<ALayout,
                                                          BLayout,
                                                          DsLayout,
                                                          ELayout,
                                                          ADataType,
                                                          BDataType,
                                                          DsDataType,
                                                          EDataType,
                                                          AElementwiseOperation,
                                                          BElementwiseOperation,
                                                          CElementwiseOperation>
{
    virtual void SetKBatchSize(BaseArgument* p_arg, index_t kbatch) const = 0;
};

} // namespace device
} // namespace tensor_operation
} // namespace ck
