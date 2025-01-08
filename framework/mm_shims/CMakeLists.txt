# Copyright 2024 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(src "mmosal_shim_freertos_esp32.c"
        "mmhal.c"
        "mmhal_wlan_binaries.c"
        "wlan_hal.c"
        "crypto_mbedtls_mm.c")
set(inc "include")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib spi_flash app_update log driver mbedtls
                       WHOLE_ARCHIVE)

target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mm6108.mbin.o")

# Kconfig variables are used to determine which bcf file to link against
if(CONFIG_MM_BCF_MF16858_US)
    message(STATUS "Using BCF for MM6108_MF16858_US")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf16858_us.mbin.o")
elseif(CONFIG_MM_BCF_MF08651_US)
    message(STATUS "Using BCF for MM6108_MF08651_US")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08651_us.mbin.o")
elseif(CONFIG_MM_BCF_MF08551)
    message(STATUS "Using BCF for MM6108_MF08551")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08551.mbin.o")
elseif(CONFIG_MM_BCF_MF08251)
    message(STATUS "Using BCF for MM6108_MF08251")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf08251.mbin.o")
elseif(CONFIG_MM_BCF_MF03120)
    message(STATUS "Using BCF for MM6108_MF03120")
    target_link_libraries(${COMPONENT_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bcf_mf03120.mbin.o")
else()
    message(FATAL_ERROR "No BCF specified for mm_shims")
endif()
