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
        static constexpr uint8_t kCMD_RESTART            = 1;
        static constexpr uint8_t kCMD_FACTORY_RESET      = 2;
        static constexpr uint8_t kCMD_RUN_BACK_ANALYSIS  = 3;
        static constexpr uint8_t kCMD_TAKE_STAT_SNAPSHOT = 4;

        static bool validate_gates(uint8_t *value)
        {
            zigbee_bin_typed_t<gate_array_t> *pT = (zigbee_bin_typed_t<gate_array_t>*)value;
            if (pT->len_bytes != 14)
                return false;

            for(int i = 0; i < 14; ++i)
                if (pT->data[i] > 100)
                    return false;
            return true;
        }
        static bool validate_stat_sample_count(uint8_t *value)
        {
            return *value <= 255;
        }

        struct [[gnu::packed]] base_cfg_t
        {
            float range_min = 0.2f;
            float range_max = 10;
            uint16_t clear_delay = 0;//seconds
            hlk::LD2412::DistanceRes distance_resolution = hlk::LD2412::DistanceRes::_0_50;

            static bool Validate(const base_cfg_t *pCfg) { 
                return 
                    (pCfg->range_min >= 0.f && pCfg->range_min <= 10.f)
                    && (pCfg->range_max >= 0.f && pCfg->range_max <= 10.f)
                    && (pCfg->range_max >= pCfg->range_min)
                    && (
                            pCfg->distance_resolution == hlk::LD2412::DistanceRes::_0_20
                            || pCfg->distance_resolution == hlk::LD2412::DistanceRes::_0_50
                            || pCfg->distance_resolution == hlk::LD2412::DistanceRes::_0_75
                    );
            }
        };

        struct [[gnu::packed]] light_sense_cfg_t{
            hlk::LD2412::LightSensitivity mode = hlk::LD2412::LightSensitivity::Off; 
            uint8_t threshold = 0;

            static bool Validate(const light_sense_cfg_t *pCfg) { 
                return 
                    (
                            pCfg->mode == hlk::LD2412::LightSensitivity::Off
                            || pCfg->mode == hlk::LD2412::LightSensitivity::DetectWhenBiggerThan
                            || pCfg->mode == hlk::LD2412::LightSensitivity::DetectWhenLessThan
                    );
            }
        };

        /**********************************************************************/
        /* Data members go here                                               */
        /**********************************************************************/
        uint8_t light_level = 0;
        struct flags_t
        {
            uint8_t background_analysis_active  : 1 = 0;
            uint8_t background_analysis_ok      : 1 = 0;
            uint8_t unused                      : 6 = 0;
        } flags = {};

        zigbee_bin_typed_t<base_cfg_t> base_config;
        zigbee_bin_typed_t<gate_array_t> still_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
        zigbee_bin_typed_t<gate_array_t> move_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
        zigbee_bin_t<7> bluetooth_mac;
        zigbee_str_t<32> sw_ver;
        bool bluetooth_state = false;
        uint8_t statistics_sample_count_window = 0;
        zigbee_bin_typed_array_t<energy_stat_t, 14> energy_stat_still;
        zigbee_bin_typed_array_t<energy_stat_t, 14> energy_stat_move;
        zigbee_bin_typed_t<light_sense_cfg_t> light_sense = light_sense_cfg_t{}; 

        /**********************************************************************/
        /* Commands                                                           */
        /**********************************************************************/
        cmd_in_t<kCMD_RESTART>            cmd_restart;
        cmd_in_t<kCMD_FACTORY_RESET>      cmd_factory_reset;
        cmd_in_t<kCMD_RUN_BACK_ANALYSIS>  cmd_run_background_analysis;
        cmd_in_t<kCMD_TAKE_STAT_SNAPSHOT> cmd_take_statistic_snapshot;
    };

    template<> struct zcl_description_t<zb_zcl_ld2412_t> {
        static constexpr auto get()
        {
            using T = zb_zcl_ld2412_t;
            return cluster_t<
                cluster_info_t{.id = kZB_ZCL_CLUSTER_ID_LD2412},
                attributes_t<
                     attribute_t{.m = &T::base_config,                        .id = 0x0000, .a=access_t::RW}
                    ,attribute_t{.m = &T::sw_ver,                             .id = 0x0001, .a=access_t::Read}
                    ,attribute_t{.m = &T::bluetooth_mac,                      .id = 0x0002, .a=access_t::Read}
                    ,attribute_t{.m = &T::still_energy_thresholds,            .id = 0x0003, .a=access_t::RW, .validator = &T::validate_gates}
                    ,attribute_t{.m = &T::move_energy_thresholds,             .id = 0x0004, .a=access_t::RW, .validator = &T::validate_gates}
                    ,attribute_t{.m = &T::light_level,                        .id = 0x0005, .a=access_t::RP}
                    ,attribute_t{.m = &T::flags,                              .id = 0x0006, .a=access_t::RWP, .type=type_t::U8}
                    ,attribute_t{.m = &T::statistics_sample_count_window,     .id = 0x0007, .a=access_t::RW, .validator = &T::validate_stat_sample_count}
                    ,attribute_t{.m = &T::energy_stat_still,                  .id = 0x0008, .a=access_t::Read}
                    ,attribute_t{.m = &T::energy_stat_move,                   .id = 0x0009, .a=access_t::Read}
                    ,attribute_t{.m = &T::light_sense,                        .id = 0x000a, .a=access_t::RW}
                    ,attribute_t{.m = &T::bluetooth_state,                    .id = 0x000b, .a=access_t::RW}
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
