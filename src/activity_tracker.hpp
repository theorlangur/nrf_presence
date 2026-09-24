#ifndef ACTIVITY_TRACKER_HPP_
#define ACTIVITY_TRACKER_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace zephyr
{

    template<uint8_t kFlag>
    struct activity_t
    {
        std::atomic_ref<volatile uint8_t> f;
        activity_t(volatile uint8_t &d):f(d) { f |= kFlag; }
        ~activity_t() { f &= ~kFlag; }
    };

    struct activity_rt_t
    {
        std::atomic_ref<volatile uint8_t> f;
        uint8_t kFlag;
        activity_rt_t(volatile uint8_t &d, uint8_t flag):f(d), kFlag(flag) { f |= kFlag; }
        ~activity_rt_t() { f &= ~kFlag; }
    };

    constexpr uint8_t kOSIF  = uint8_t(1) << 0;
    constexpr uint8_t kLD2412_Back_Main  = uint8_t(1) << 1;
    constexpr uint8_t kLD2412_Back_Aux   = uint8_t(1) << 2;
    constexpr uint8_t kLD2412_Front_Main = uint8_t(1) << 3;
    constexpr uint8_t kLD2412_Front_Aux  = uint8_t(1) << 4;
    constexpr uint8_t kEnv               = uint8_t(1) << 5;
}

#endif
