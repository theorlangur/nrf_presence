#ifndef C4001_TASK_HPP_
#define C4001_TASK_HPP_
#include "lib/lib_dfr_c4001.h"
#include <utility>

namespace c4001
{
    enum class cfg_id_t: uint8_t
    {
        Range           = 1 << 0,
        RangeTrig       = 1 << 1,
        Delay           = 1 << 2,
        Sensitivity     = 1 << 3,
        InhibitDuration = 1 << 4,
        All = Range | RangeTrig | Delay | Sensitivity | InhibitDuration,
    };
    constexpr bool operator&(cfg_id_t i1, cfg_id_t i2) { return (std::to_underlying(i1) & std::to_underlying(i2)) != 0; }

    enum class err_t
    {
        Ok,
        Range,
        RangeTrig,
        Delay,
        Sensitivity,
        InhibitDuration,
        SaveConfig,
        ResetConfig,
        Restart,
        ReloadConfig,
    };
    using err_callback_t = void(*)(err_t);
    using upd_callback_t = void(*)(cfg_id_t);
    dfr::C4001* setup(err_callback_t err, upd_callback_t upd);

    void set_range(float from, float to);
    void set_range_from(float v);
    void set_range_to(float v);
    void set_range_trig(float trig);
    void set_detect_delay(float v);
    void set_clear_delay(float v);
    void set_detect_clear_delay(float detect, float clear);
    void set_detect_sensitivity(uint8_t s);
    void set_hold_sensitivity(uint8_t s);
    void set_sensitivity(uint8_t detect, uint8_t hold);
    void set_inhibit_duration(float dur);
    void save_config();
    void reset_config();
    void restart();
}

#endif
