# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED ENV{USE_NRF52})
    set(EXTRA_DTC_OVERLAY_FILE "dts.overlay")
else()
    set(EXTRA_DTC_OVERLAY_FILE "dts.52840.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(presence_dfr_c4001)

target_include_directories(app PRIVATE submodules/esp_generic_lib/include)

target_sources(app PRIVATE src/main.cpp  src/c4001_task.cpp)
add_subdirectory(src/lib)

zephyr_include_directories(submodules/nrf_zb_cpp/include)
zephyr_include_directories(submodules/nrf_general/include)

include(cmake/CustomLibcxx.cmake)

if(COMMAND setup_custom_libcxx)
    setup_custom_libcxx(app)
else()
    target_link_libraries(app PRIVATE c stdc++ supc++)
    target_compile_options(app PRIVATE -fmacro-backtrace-limit=0)
    target_compile_options(app PRIVATE -D__cpp_concepts=202002L -Wno-builtin-macro-redefined -Wno-invalid-offsetof)
endif()

