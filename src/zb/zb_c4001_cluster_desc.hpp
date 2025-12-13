#ifndef ZB_C4001_CLUSTER_DESC_HPP_
#define ZB_C4001_CLUSTER_DESC_HPP_

#include <nrfzbcpp/zb_main.hpp>

extern "C"
{
#include <zboss_api_addons.h>
#include <zb_mem_config_med.h>
#include <zb_nrf_platform.h>
}

namespace zb
{
    static constexpr uint16_t kZB_ZCL_CLUSTER_ID_C4001 = 0xfc81;
    struct zb_zcl_c4001_t
    {
        float range_min = 0.6f;
        float range_max = 25;
        float range_trig = 8;
        float inhibit_duration = 0;
        float detect_delay = 0.f;
        float clear_delay = 0.f;
        uint8_t sensitivity_detect = 5;
        uint8_t sensitivity_hold = 5;
        ZigbeeStr<32> sw_ver;
        ZigbeeStr<32> hw_ver;
        cmd_in_t<1> cmd_restart;
    };

    template<> struct zcl_description_t<zb_zcl_c4001_t> {
        static constexpr auto get()
        {
            using T = zb_zcl_c4001_t;
            return cluster_t<
                cluster_info_t{.id = kZB_ZCL_CLUSTER_ID_C4001},
                attributes_t<
                     attribute_t{.m = &T::range_min,          .id = 0x0000, .a=Access::RW}
                    ,attribute_t{.m = &T::range_max,          .id = 0x0001, .a=Access::RW}
                    ,attribute_t{.m = &T::range_trig,         .id = 0x0002, .a=Access::RW}
                    ,attribute_t{.m = &T::inhibit_duration,   .id = 0x0003, .a=Access::RW}
                    ,attribute_t{.m = &T::sensitivity_detect, .id = 0x0004, .a=Access::RW}
                    ,attribute_t{.m = &T::sensitivity_hold,   .id = 0x0005, .a=Access::RW}
                    ,attribute_t{.m = &T::sw_ver,             .id = 0x0006, .a=Access::Read}
                    ,attribute_t{.m = &T::hw_ver,             .id = 0x0007, .a=Access::Read}
                    ,attribute_t{.m = &T::detect_delay,       .id = 0x0008, .a=Access::RW}
                    ,attribute_t{.m = &T::clear_delay,        .id = 0x0009, .a=Access::RW}
                >{},
                commands_t<
                    &T::cmd_restart
                >{}
            >{};
        }
    };
}
#endif
