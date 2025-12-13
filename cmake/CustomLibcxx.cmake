# cmake/CustomLibcxx.cmake

# 1. Check for the Environment Variable
# We only define the function/logic if the env var is present and set to "1" or "ON"
if(NOT DEFINED ENV{USE_CUSTOM_LIBCXX} OR NOT "$ENV{USE_CUSTOM_LIBCXX}" MATCHES "^(1|ON|TRUE|YES)$")
    message(STATUS "Custom Libc++: Disabled (Env var USE_CUSTOM_LIBCXX not set)")
    return()
endif()

message(STATUS "Custom Libc++: Enabled via Environment Variable")

# 2. Define the configuration function
function(setup_custom_libcxx TARGET_NAME)
    # --- B. Define the content of the Override File ---
    # We use Zephyr's printk so it actually outputs to the console
    set(OVERRIDE_CONTENT "
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <cstdarg>

namespace std {

	inline namespace __1{
// The signature must match libc++'s expectation
[[noreturn]] void __libcpp_verbose_abort(char const* format, ...) {
			{
				va_list list;
				va_start(list, format);
				vprintk(format, list);
				va_end(list);
			}
            k_oops();
			// k_sleep(K_FOREVER);
}
}

} // namespace std
")

    # --- C. Generate the file ---
    # We put this in the binary directory so it doesn't clutter source control
    set(GENERATED_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/libcxx_verbose_abort_override.cpp")
    
    # Create directory if it doesn't exist
    get_filename_component(GEN_DIR ${GENERATED_FILE} DIRECTORY)
    file(MAKE_DIRECTORY ${GEN_DIR})

    # Write the file
    file(WRITE ${GENERATED_FILE} "${OVERRIDE_CONTENT}")
    
    message(STATUS "Custom Libc++: Generated override at ${GENERATED_FILE}")

    # --- D. Update the Target ---

    # 1. Add the generated file to the build
    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE c c++ c++abi)

endfunction()
