#ifndef ZB_DEV_CTRL_CLUSTER_DESC_HPP_
#define ZB_DEV_CTRL_CLUSTER_DESC_HPP_

#include <nrfzbcpp/zb_main.hpp>

extern "C"
{
#include <zboss_api_addons.h>
#include <zb_nrf_platform.h>
}
#include <nrf_uart/periphery/lib_ld2412.hpp>

namespace zb
{
    static constexpr uint16_t kZB_ZCL_CLUSTER_ID_DEV_CTRL = 0xfc83;
    struct zb_zcl_dev_ctrl_t
    {
        using gate_array_t = hlk::LD2412::gate_array_t;
        using energy_stat_t = hlk::LD2412::energy_stat_t;
        static constexpr uint8_t kCMD_START_ANALYSIS_FOR_PRESENCE = 1;
        static constexpr uint8_t kCMD_START_ANALYSIS_FOR_ABSENCE  = 2;
        static constexpr uint8_t kCMD_STOP_ANALYSIS               = 3;

        /**********************************************************************/
        /* Data members go here                                               */
        /**********************************************************************/
        zigbee_bin_typed_t<gate_array_t> main_still_energy_analysis = gate_array_t{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        zigbee_bin_typed_t<gate_array_t> aux_still_energy_analysis = gate_array_t{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        /**********************************************************************/
        /* Commands                                                           */
        /**********************************************************************/
        cmd_in_t<kCMD_START_ANALYSIS_FOR_PRESENCE> cmd_start_analysis_for_presence;
        cmd_in_t<kCMD_START_ANALYSIS_FOR_ABSENCE>  cmd_start_analysis_for_absense;
        cmd_in_t<kCMD_STOP_ANALYSIS>               cmd_stop_analysis;
    };

    template<> struct zcl_description_t<zb_zcl_dev_ctrl_t> {
        static constexpr auto get()
        {
            using T = zb_zcl_dev_ctrl_t;
            return cluster_t<
                cluster_info_t{.id = kZB_ZCL_CLUSTER_ID_DEV_CTRL},
                attributes_t<
                    attribute_t{.m = &T::main_still_energy_analysis,         .id = 0x0001, .a=access_t::Read}
                    ,attribute_t{.m = &T::aux_still_energy_analysis,         .id = 0x0002, .a=access_t::Read}
                >{},
                commands_t<
                    &T::cmd_start_analysis_for_presence, 
                    &T::cmd_start_analysis_for_absense, 
                    &T::cmd_stop_analysis
                >{}
            >{};
        }
    };
}
#endif
