# ckProfiler
set(PROFILER_SOURCES
    profiler.cpp
    profile_gemm.cpp
    profile_gemm_splitk.cpp
    profile_gemm_bias_add_reduce.cpp
    profile_gemm_add_multiply.cpp
    profile_gemm_multiply_add.cpp
    profile_gemm_reduce.cpp
    profile_batched_gemm.cpp
    profile_batched_gemm_reduce.cpp
    profile_conv_fwd.cpp
    profile_conv_fwd_bias_relu.cpp
    profile_conv_fwd_bias_relu_add.cpp
    profile_conv_bwd_data.cpp
    profile_grouped_conv_fwd.cpp
    profile_grouped_conv_bwd_weight.cpp
    profile_reduce.cpp
    profile_groupnorm_fwd.cpp
    profile_layernorm_fwd.cpp
    profile_max_pool3d_fwd.cpp
    profile_avg_pool3d_bwd.cpp
    profile_max_pool3d_bwd.cpp
    profile_softmax.cpp
    profile_batchnorm_fwd.cpp
    profile_batchnorm_bwd.cpp
    profile_batchnorm_infer.cpp
    profile_grouped_conv_bwd_data.cpp
    profile_conv_tensor_rearrange.cpp
)

if(DL_KERNELS)
  list(APPEND PROFILER_SOURCES profile_batched_gemm_multi_d.cpp)
endif()

if(DTYPES MATCHES "fp16" OR NOT DEFINED DTYPES)
  list(APPEND PROFILER_SOURCES profile_batched_gemm_gemm.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_fastgelu.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_streamk.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_bilinear.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_add_fastgelu.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_add_add_fastgelu.cpp)
  list(APPEND PROFILER_SOURCES profile_gemm_add_relu_add_layernorm.cpp)
  list(APPEND PROFILER_SOURCES profile_batched_gemm_add_relu_gemm_add.cpp)
  list(APPEND PROFILER_SOURCES profile_grouped_gemm.cpp)
  list(APPEND PROFILER_SOURCES profile_grouped_gemm_fastgelu.cpp)
endif()

if(DTYPES MATCHES "fp32" OR DTYPES MATCHES "fp64" OR NOT DEFINED DTYPES)
  list(APPEND PROFILER_SOURCES profile_contraction_bilinear.cpp)
  list(APPEND PROFILER_SOURCES profile_contraction_scale.cpp)
endif()

set(PROFILER_EXECUTABLE ckProfiler)

add_executable(${PROFILER_EXECUTABLE} ${PROFILER_SOURCES})
target_compile_options(${PROFILER_EXECUTABLE} PRIVATE -Wno-global-constructors)

target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE utility)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_splitk_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_add_multiply_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_multiply_add_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_reduce_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_bias_add_reduce_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batched_gemm_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batched_gemm_reduce_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv2d_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv1d_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv2d_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv3d_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv1d_bwd_data_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv2d_bwd_data_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv3d_bwd_data_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv1d_bwd_weight_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv2d_bwd_weight_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv3d_bwd_weight_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv2d_fwd_bias_relu_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_conv2d_fwd_bias_relu_add_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_normalization_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_softmax_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_reduce_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batchnorm_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_pool3d_fwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_avg_pool3d_bwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_max_pool_bwd_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv2d_bwd_data_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_conv3d_bwd_data_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_image_to_column_instance)
target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_column_to_image_instance)

if(DTYPES MATCHES "fp32" OR DTYPES MATCHES "fp64" OR NOT DEFINED DTYPES)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_contraction_bilinear_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_contraction_scale_instance)
endif()



if(DL_KERNELS)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batched_gemm_multi_d_instance)
endif()

if(DTYPES MATCHES "fp16" OR NOT DEFINED DTYPES)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_add_fastgelu_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_add_relu_add_layernorm_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_bilinear_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_add_add_fastgelu_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_streamk_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_gemm_fastgelu_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batched_gemm_gemm_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_batched_gemm_add_relu_gemm_add_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_gemm_instance)
  target_link_libraries(${PROFILER_EXECUTABLE} PRIVATE device_grouped_gemm_fastgelu_instance)
endif()

rocm_install(TARGETS ${PROFILER_EXECUTABLE} COMPONENT profiler)
