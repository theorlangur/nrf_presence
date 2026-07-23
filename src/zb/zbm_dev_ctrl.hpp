#ifndef ZBM_DEV_CTRL_CLUSTER_DESC_HPP_
#define ZBM_DEV_CTRL_CLUSTER_DESC_HPP_

#include <nrfzbmcpp/zbm.hpp>
#include <nrf_uart/periphery/lib_ld2412.hpp>

namespace zbm
{
    namespace misc_zc{
        static constexpr uint16_t kZB_ZCL_CLUSTER_ID_DEV_CTRL = 0xfc83;
        struct 
            [[=cluster_a{.id = kZB_ZCL_CLUSTER_ID_DEV_CTRL}]]
            dev_ctrl_t
        {
            using gate_array_t = hlk::LD2412::gate_array_t;
            using energy_stat_t = hlk::LD2412::energy_stat_t;
            static constexpr uint8_t kCMD_START_ANALYSIS_FOR_PRESENCE = 1;
            static constexpr uint8_t kCMD_START_ANALYSIS_FOR_ABSENCE  = 2;
            static constexpr uint8_t kCMD_STOP_ANALYSIS               = 3;

            /**********************************************************************/
            /* Data members go here                                               */
            /**********************************************************************/
            [[=attribute_a{.id = 0}]]
            bin_typed_t<gate_array_t> main_still_energy_analysis = gate_array_t{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

            [[=attribute_a{.id = 1}]]
            bin_typed_t<gate_array_t> aux_still_energy_analysis = gate_array_t{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

            /**********************************************************************/
            /* Commands                                                           */
            /**********************************************************************/
            [[=cmd_in_a{kCMD_START_ANALYSIS_FOR_PRESENCE}]]
            cmd_handling_result_t(*cmd_start_analysis_for_presence)();

            [[=cmd_in_a{kCMD_START_ANALYSIS_FOR_ABSENCE}]]
            cmd_handling_result_t(*cmd_start_analysis_for_absense)();

            [[=cmd_in_a{kCMD_STOP_ANALYSIS}]]
            cmd_handling_result_t(*cmd_stop_analysis)();
        };
    }
}
#endif
