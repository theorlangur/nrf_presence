#ifndef ZB_LD2412_CLUSTER_DESC_HPP_
#define ZB_LD2412_CLUSTER_DESC_HPP_

#include <nrfzbcpp/zb_main.hpp>

extern "C"
{
#include <zboss_api_addons.h>
#include <zb_nrf_platform.h>
}
#include <nrf_uart/periphery/lib_ld2412.hpp>

namespace zb
{
    static constexpr uint16_t kZB_ZCL_CLUSTER_ID_LD2412 = 0xfc82;
    struct zb_zcl_ld2412_t
    {
        using gate_array_t = hlk::LD2412::gate_array_t;
        using energy_stat_t = hlk::LD2412::energy_stat_t;

        hlk::LD2412::DistanceRes distance_resolution = hlk::LD2412::DistanceRes::_0_50;
        uint8_t light_level = 0;
        struct
        {
            uint8_t bluetooth_state             : 1 = 0;
            uint8_t background_analysis_active  : 1 = 0;
            uint8_t background_analysis_ok      : 1 = 0;
            uint8_t collect_statistics          : 1 = 0;
            uint8_t unused                      : 4 = 0;
        } flags = {};
        float range_min = 0.2f;
        float range_max = 10;
        uint16_t clear_delay = 0;//seconds

        ZigbeeBin<15> still_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
        ZigbeeBin<15> move_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};

        ZigbeeBin<14> bluetooth_mac;
        ZigbeeStr<32> sw_ver;

        uint8_t statistics_sample_count_window = 0;
        ZigbeeBinTyped<energy_stat_t, 14> energy_stat_still;
        ZigbeeBinTyped<energy_stat_t, 14> energy_stat_move;

        hlk::LD2412::LightSensitivity light_sense_mode = hlk::LD2412::LightSensitivity::Off; 
        uint8_t light_sense_threshold = 0;

        cmd_in_t<1> cmd_restart;
        cmd_in_t<2> cmd_factory_reset;
        cmd_in_t<3> cmd_run_background_analysis;
        cmd_in_t<4> cmd_take_statistic_snapshot;
    };

    template<> struct zcl_description_t<zb_zcl_ld2412_t> {
        static constexpr auto get()
        {
            using T = zb_zcl_ld2412_t;
            return cluster_t<
                cluster_info_t{.id = kZB_ZCL_CLUSTER_ID_LD2412},
                attributes_t<
                    attribute_t{.m = &T::distance_resolution,                 .id = 0x0000, .a=Access::RW}
                    ,attribute_t{.m = &T::range_min,                          .id = 0x0001, .a=Access::RW}
                    ,attribute_t{.m = &T::range_max,                          .id = 0x0002, .a=Access::RW}
                    ,attribute_t{.m = &T::sw_ver,                             .id = 0x0003, .a=Access::Read}
                    ,attribute_t{.m = &T::bluetooth_mac,                      .id = 0x0004, .a=Access::Read}
                    ,attribute_t{.m = &T::clear_delay,                        .id = 0x0005, .a=Access::RW}
                    ,attribute_t{.m = &T::still_energy_thresholds,            .id = 0x0006, .a=Access::RW}
                    ,attribute_t{.m = &T::move_energy_thresholds,             .id = 0x0007, .a=Access::RW}
                    ,attribute_t{.m = &T::light_level,                        .id = 0x0008, .a=Access::RP}
                    ,attribute_t{.m = &T::flags,                              .id = 0x0009, .a=Access::RWP, .type=Type::U8}
                    ,attribute_t{.m = &T::statistics_sample_count_window,     .id = 0x000a, .a=Access::RW}
                    ,attribute_t{.m = &T::energy_stat_still,                  .id = 0x000b, .a=Access::Read}
                    ,attribute_t{.m = &T::energy_stat_move,                   .id = 0x000c, .a=Access::Read}
                    ,attribute_t{.m = &T::light_sense_mode,                   .id = 0x000d, .a=Access::RW}
                    ,attribute_t{.m = &T::light_sense_threshold,              .id = 0x000e, .a=Access::RW}
                >{},
                commands_t<
                    &T::cmd_restart, 
                    &T::cmd_factory_reset, 
                    &T::cmd_run_background_analysis, 
                    &T::cmd_take_statistic_snapshot
                >{}
            >{};
        }
    };
}
#endif
