#define FORCE_FMT
#define PRINTF_FUNC(...) printk(__VA_ARGS__)

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "c4001_task.hpp"
#include <variant>

#include <nrf_general/lib_msgq_typed.hpp>

#define DFR_UART_NODE DT_ALIAS(dfr_uart)

namespace c4001
{
    namespace{
	template<class... O>
	struct overloaded:O...
	{
	    using O::operator()...;
	};
	template<class... O>
	overloaded(O... o)->overloaded<O...>;
    };
    constinit const struct device *c4001_uart = DEVICE_DT_GET(DFR_UART_NODE);
    static dfr::C4001 c4001(c4001_uart);

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
    K_MSGQ_DEFINE_TYPED(C4001Q, c4001q);

    void c4001_thread_entry(void *, void *, void *);
    constexpr size_t C4001_THREAD_STACK_SIZE = 1024 * 2;
    constexpr size_t C4001_THREAD_PRIORITY=7;

K_THREAD_DEFINE(c4001_thread, C4001_THREAD_STACK_SIZE,
	c4001_thread_entry, NULL, NULL, NULL,
	C4001_THREAD_PRIORITY, 0, -1);

    constinit err_callback_t g_err = nullptr;
    constinit upd_callback_t g_upd = nullptr;
    dfr::C4001* setup(err_callback_t err, upd_callback_t upd)
    {
	auto r = c4001.Init();
	if (!r)
	    return nullptr;
	g_err = err;
	g_upd = upd;
	k_thread_start(c4001_thread);
	return &c4001;
    }

    void set_range(float from, float to)
    {
	c4001q << range_t{.from = from, .to = to};
    }

    void set_range_from(float v)
    {
	c4001q << range_t{.from = v, .to = c4001.GetRangeTo()};
    }

    void set_range_to(float v)
    {
	c4001q << range_t{.from = c4001.GetRangeFrom(), .to = v};
    }

    void set_range_trig(float trig)
    {
	c4001q << range_trig_t{.trig = trig};
    }

    void set_detect_delay(float v)
    {
	c4001q << delay_t{.detect = v, .clear = c4001.GetClearLatency()};
    }

    void set_clear_delay(float v)
    {
	c4001q << delay_t{.detect = c4001.GetDetectLatency(), .clear = v};
    }

    void set_detect_clear_delay(float detect, float clear)
    {
	c4001q << delay_t{.detect = detect, .clear = clear};
    }

    void set_detect_sensitivity(uint8_t s)
    {
	c4001q << sensitivity_t{.detect = s, .hold = 255};
    }

    void set_hold_sensitivity(uint8_t s)
    {
	c4001q << sensitivity_t{.detect = 255, .hold = s};
    }

    void set_sensitivity(uint8_t detect, uint8_t hold)
    {
	c4001q << sensitivity_t{.detect = detect, .hold = hold};
    }

    void set_inhibit_duration(float dur)
    {
	c4001q << inhibit_duration_t{.duration = dur};
    }

    void save_config()
    {
	c4001q << save_cfg_t{};
    }

    void reset_config()
    {
	c4001q << reset_cfg_t{};
    }

    void restart()
    {
	c4001q << restart_cfg_t{};
    }

    void c4001_thread_entry(void *, void *, void *)
    {
	QueueItem q;
	using Cfg = dfr::C4001::Configurator;
	while(1)
	{
	    c4001q >> q;
	    std::visit(
		overloaded{
		    [](range_t const& v){ 
			if (auto r = c4001
				.GetConfigurator()
				.SetRange(v.from, v.to)
				.and_then([](Cfg &cfg){ return cfg.SaveConfig(); })
				.and_then([](Cfg &cfg){ return cfg.UpdateRange(); }); !r)
			{
			    if (g_err) g_err(err_t::Range);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::Range);
		    }
		    ,[](range_trig_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SetTrigRange(v.trig)
				.and_then([](Cfg &cfg){ return cfg.SaveConfig(); })
				.and_then([](Cfg &cfg){ return cfg.UpdateTrigRange(); }); !r)
			{
			    if (g_err) g_err(err_t::RangeTrig);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::RangeTrig);
		    }
		    ,[](delay_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SetLatency(v.detect, v.clear)
				.and_then([](Cfg &cfg){ return cfg.SaveConfig(); })
				.and_then([](Cfg &cfg){ return cfg.UpdateLatency(); }); !r)
			{
			    if (g_err) g_err(err_t::Delay);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::Delay);
		    }
		    ,[](sensitivity_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SetSensitivity(v.detect, v.hold)
				.and_then([](Cfg &cfg){ return cfg.SaveConfig(); })
				.and_then([](Cfg &cfg){ return cfg.UpdateSensitivity(); }); !r)
			{
			    if (g_err) g_err(err_t::Sensitivity);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::Sensitivity);
		    }
		    ,[](inhibit_duration_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SetInhibit(v.duration)
				.and_then([](Cfg &cfg){ return cfg.SaveConfig(); })
				.and_then([](Cfg &cfg){ return cfg.UpdateInhibit(); }); !r)
			{
			    if (g_err) g_err(err_t::InhibitDuration);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::InhibitDuration);
		    }
		    ,[](save_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SaveConfig(); !r && g_err)
			    g_err(err_t::SaveConfig);
		    }
		    ,[](reset_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.ResetConfig(); !r)
			{
			    if (g_err) g_err(err_t::ResetConfig);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::All);
		    }
		    ,[](restart_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.Restart(); !r)
			{
			    if (g_err) g_err(err_t::Restart);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::All);
		    }
		    ,[](reload_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.ReloadConfig(); !r)
			{
			    if (g_err) g_err(err_t::ReloadConfig);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::All);
		    }
		},
		q
	    );
	}
    }
}
