if(ZEPHYR_TOOLCHAIN_VARIANT STREQUAL "cross-compile")
    # Tell sysbuild to use the v2 configuration instead of the default mcuboot.conf
    set(mcuboot_EXTRA_CONF_FILE "${APP_DIR}/sysbuild/mcuboot.conf ${APP_DIR}/sysbuild/mcuboot_gcc16.conf" CACHE INTERNAL "Custom MCUboot Kconfig")
    
    # Optional: If you also want to swap or add a specific overlay
    # set(mcuboot_DTC_OVERLAY_FILE "${APP_DIR}/sysbuild/mcuboot.overlay" CACHE INTERNAL "Custom MCUboot Devicetree")
elseif(ZEPHYR_TOOLCHAIN_VARIANT STREQUAL "llvm")
    set(mcuboot_EXTRA_CONF_FILE "${APP_DIR}/sysbuild/mcuboot.conf ${APP_DIR}/sysbuild/mcuboot_llvm.conf" CACHE INTERNAL "Custom MCUboot Kconfig")
    # set(mcuboot_DTC_OVERLAY_FILE "${APP_DIR}/sysbuild/mcuboot.overlay" CACHE INTERNAL "Custom MCUboot Devicetree")
endif()
