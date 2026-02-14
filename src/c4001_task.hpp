#ifndef C4001_TASK_HPP_
#define C4001_TASK_HPP_
#include <nrf_uart/periphery/lib_dfr_c4001.h>
#include <utility>
#include <variant>
#include <nrf_general/lib_msgq_typed.hpp>

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

    class Instance
    {
    public:
        using err_callback_t = void(*)(err_t);
        using upd_callback_t = void(*)(cfg_id_t);

        /**********************************************************************/
        /* Message Queue definitions + commands                               */
        /**********************************************************************/
        struct range_t
        {
            float from;
            float to;
        };
        struct range_trig_t
        {
            float trig;
        };
        struct delay_t
        {
            float detect;
            float clear;
        };
        struct sensitivity_t
        {
            uint8_t detect;
            uint8_t hold;
        };
        struct inhibit_duration_t
        {
            float duration;
        };
        struct save_cfg_t{};
        struct reset_cfg_t{};
        struct restart_cfg_t{};
        struct reload_cfg_t{};

        using QueueItem = std::variant<
                      range_t
                    , range_trig_t
                    , delay_t
                    , sensitivity_t
                    , inhibit_duration_t
                    , save_cfg_t
                    , reset_cfg_t
                    , restart_cfg_t
                    , reload_cfg_t
                >;

        using C4001Q = msgq::Queue<QueueItem,4>;

        Instance(C4001Q &q, const struct device *uart, const char* thread_name);

        dfr::C4001* setup(err_callback_t err, upd_callback_t upd);

        dfr::C4001* sensor();

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

    private:
        static void c4001_thread_entry(void *, void *, void *);
        void c4001_mainloop();


        static constexpr size_t C4001_THREAD_STACK_SIZE = 1024 * 2;
        static constexpr size_t C4001_THREAD_PRIORITY=7;
        alignas(16) k_thread_stack_t c4001_thread_stack[C4001_THREAD_STACK_SIZE];
        k_thread c4001_thread_e;
        const char *c4001_thread_name = nullptr;

        k_tid_t c4001_thread = nullptr;
        C4001Q &c4001q;
        err_callback_t g_err = nullptr;
        upd_callback_t g_upd = nullptr;
        const struct device *c4001_uart;
        dfr::C4001 c4001;
    };
}

#endif
