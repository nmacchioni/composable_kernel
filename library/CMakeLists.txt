# SPDX-License-Identifier: MIT
# Copyright (c) 2018-2022, Advanced Micro Devices, Inc. All rights reserved.

if (CK_BUILD_JIT_LIB)
    add_subdirectory(src/jit_library)
else()
    add_subdirectory(src/tensor_operation_instance/gpu)
    add_subdirectory(src/utility)
endif()
