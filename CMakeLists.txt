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

target_sources(app PRIVATE src/main.cpp src/ld2412_task.cpp)

add_subdirectory(submodules/nrf_zb_cpp)
add_subdirectory(submodules/nrf_general)
add_subdirectory(submodules/nrf_uart)

target_include_directories(NrfLibUART PRIVATE submodules/esp_generic_lib/include)

target_link_libraries(app PUBLIC NrfLibGeneral)
target_link_libraries(app PUBLIC NrfZBCpp)
target_link_libraries(app PUBLIC NrfLibUART)

include(cmake/CustomLibcxx.cmake)

target_link_options(app PUBLIC
  -Wl,--wrap=nrf_802154_transmitted_raw
  -Wl,--wrap=nrf_802154_transmit_failed
  -Wl,--wrap=nrf_802154_tx_started
  -Wl,--wrap=nrf_802154_receive_failed
  -Wl,--wrap=nrf_802154_received_raw
)

if(COMMAND setup_custom_libcxx)
    setup_custom_libcxx(app)
elseif(ZEPHYR_TOOLCHAIN_VARIANT STREQUAL "cross-compile")
    target_link_libraries(app PRIVATE c stdc++ supc++)
    # target_compile_options(app PRIVATE -fmacro-backtrace-limit=0)
    target_compile_options(app PRIVATE -D__cpp_concepts=202002L -Wno-builtin-macro-redefined -Wno-invalid-offsetof -Wno-unused-variable -Wno-double-promotion)
    target_compile_options(app PRIVATE -freflection)
else()
    target_link_libraries(app PRIVATE c stdc++ supc++)
    target_compile_options(app PRIVATE -fmacro-backtrace-limit=0)
    target_compile_options(app PRIVATE -D__cpp_concepts=202002L -Wno-builtin-macro-redefined -Wno-invalid-offsetof -Wno-unused-variable)
endif()

if(CONFIG_DEBUG_COREDUMP)
    target_compile_options(app PRIVATE -fno-omit-frame-pointer)
endif()

