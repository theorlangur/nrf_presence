#ifndef ZBM_LD2412_CLUSTER_DESC_HPP_
#define ZBM_LD2412_CLUSTER_DESC_HPP_


#include <nrfzbmcpp/zbm.hpp>
#include <nrf_uart/periphery/lib_ld2412.hpp>

namespace zbm
{
    namespace misc_zc{
        static constexpr uint16_t kZB_ZCL_CLUSTER_ID_LD2412 = 0xfc82;
        struct 
            [[=cluster_a{.id = kZB_ZCL_CLUSTER_ID_LD2412}]]
            ld2412_t
        {
            using gate_array_t = hlk::LD2412::gate_array_t;
            using energy_stat_t = hlk::LD2412::energy_stat_t;
            static constexpr uint8_t kCMD_RESTART            = 1;
            static constexpr uint8_t kCMD_FACTORY_RESET      = 2;
            static constexpr uint8_t kCMD_RUN_BACK_ANALYSIS  = 3;
            static constexpr uint8_t kCMD_TAKE_STAT_SNAPSHOT = 4;

            static zb_ret_t validate_gates(uint8_t *value)
            {
                bin_typed_t<gate_array_t> *pT = (bin_typed_t<gate_array_t>*)value;
                if (pT->len_bytes != 14)
                    return RET_ERROR;

                for(int i = 0; i < 14; ++i)
                    if (pT->data[i] > 100)
                        return RET_ERROR;
                return RET_OK;
            }
            static zb_ret_t validate_stat_sample_count(uint8_t *value)
            {
                return *value <= 255 ? RET_OK : RET_ERROR;
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
            [[=attribute_a{.id = 0x0005, .a=access_t::RP}]]
            uint8_t light_level = 0;

            struct flags_t
            {
                uint8_t background_analysis_active  : 1 = 0;
                uint8_t background_analysis_ok      : 1 = 0;
                uint8_t unused                      : 6 = 0;
            };

            [[=attribute_a{.id = 0x0006, .a=access_t::RWP, .type=type_t::U8}]]
            flags_t flags = {};

            [[=attribute_a{.id = 0x0000, .a=access_t::RW}]]
            bin_typed_t<base_cfg_t> base_config;

            [[=attribute_a{.id = 0x0003, .a=access_t::RW, .validator = &validate_gates}]]
            bin_typed_t<gate_array_t> still_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};

            [[=attribute_a{.id = 0x0004, .a=access_t::RW, .validator = &validate_gates}]]
            bin_typed_t<gate_array_t> move_energy_thresholds = gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};

            [[=attribute_a{.id = 0x0002}]]
            bin_t<7> bluetooth_mac;

            [[=attribute_a{.id = 0x0001}]]
            str_t<32> sw_ver;

            [[=attribute_a{.id = 0x000b, .a=access_t::RW}]]
            bool bluetooth_state = false;

            [[=attribute_a{.id = 0x0007, .a=access_t::RW, .validator = &validate_stat_sample_count}]]
            uint8_t statistics_sample_count_window = 0;

            [[=attribute_a{.id = 0x0008}]]
            bin_typed_array_t<energy_stat_t, 14> energy_stat_still;

            [[=attribute_a{.id = 0x0009}]]
            bin_typed_array_t<energy_stat_t, 14> energy_stat_move;

            [[=attribute_a{.id = 0x000a, .a=access_t::RW}]]
            bin_typed_t<light_sense_cfg_t> light_sense = light_sense_cfg_t{}; 

            /**********************************************************************/
            /* Commands                                                           */
            /**********************************************************************/
            [[=cmd_in_a{kCMD_RESTART}]]
            cmd_handling_result_t(*cmd_restart)();

            [[=cmd_in_a{kCMD_FACTORY_RESET}]]
            cmd_handling_result_t(*cmd_factory_reset)();

            [[=cmd_in_a{kCMD_RUN_BACK_ANALYSIS}]]
            cmd_handling_result_t(*cmd_run_background_analysis)();

            [[=cmd_in_a{kCMD_TAKE_STAT_SNAPSHOT}]]
            cmd_handling_result_t(*cmd_take_statistic_snapshot)();
        };
    }
}

#endif
