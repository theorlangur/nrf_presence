#define FORCE_FMT
#define PRINTF_FUNC(...) printk(__VA_ARGS__)

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "c4001_task.hpp"
#include <lib_misc_helpers.hpp>

namespace c4001
{
    Instance::Instance(C4001Q &q, const struct device *uart, const char* thread_name):
	c4001_thread_name(thread_name),
	c4001q(q),
	c4001_uart(uart),
	c4001(c4001_uart)
    {
    }

    dfr::C4001* Instance::setup(err_callback_t err, upd_callback_t upd)
    {
	if (!device_is_ready(c4001_uart))
	{
	    printk("uart for c4001 is not ready\r\n");
	    c4001_thread = nullptr;
	    return nullptr;
	}
	auto r = c4001.Init();
	if (!r)
	{
	    FMT_PRINTLN("c4001 init failed with {}", r.error());
	    c4001_thread = nullptr;
	    return nullptr;
	}
	g_err = err;
	g_upd = upd;

	c4001_thread = k_thread_create(&c4001_thread_e 
	    ,c4001_thread_stack
	    ,sizeof(c4001_thread_stack)
	    ,c4001_thread_entry
	    ,this, nullptr, nullptr
	    ,C4001_THREAD_PRIORITY
	    , 0
	    , {0});
	if (!c4001_thread)
	    return nullptr;
	k_thread_name_set(c4001_thread, c4001_thread_name);
	return &c4001;
    }

    dfr::C4001* Instance::sensor()
    {
	return &c4001;
	//if (c4001_thread)
	//    return &c4001;
	//else
	//    return nullptr;
    }

    void Instance::set_range(float from, float to)
    {
	c4001q << range_t{.from = from, .to = to};
    }

    void Instance::set_range_from(float v)
    {
	c4001q << range_t{.from = v, .to = c4001.GetRangeTo()};
    }

    void Instance::set_range_to(float v)
    {
	c4001q << range_t{.from = c4001.GetRangeFrom(), .to = v};
    }

    void Instance::set_range_trig(float trig)
    {
	c4001q << range_trig_t{.trig = trig};
    }

    void Instance::set_detect_delay(float v)
    {
	c4001q << delay_t{.detect = v, .clear = c4001.GetClearLatency()};
    }

    void Instance::set_clear_delay(float v)
    {
	c4001q << delay_t{.detect = c4001.GetDetectLatency(), .clear = v};
    }

    void Instance::set_detect_clear_delay(float detect, float clear)
    {
	c4001q << delay_t{.detect = detect, .clear = clear};
    }

    void Instance::set_detect_sensitivity(uint8_t s)
    {
	c4001q << sensitivity_t{.detect = s, .hold = 255};
    }

    void Instance::set_hold_sensitivity(uint8_t s)
    {
	c4001q << sensitivity_t{.detect = 255, .hold = s};
    }

    void Instance::set_sensitivity(uint8_t detect, uint8_t hold)
    {
	c4001q << sensitivity_t{.detect = detect, .hold = hold};
    }

    void Instance::set_inhibit_duration(float dur)
    {
	c4001q << inhibit_duration_t{.duration = dur};
    }

    void Instance::save_config()
    {
	c4001q << save_cfg_t{};
    }

    void Instance::reset_config()
    {
	c4001q << reset_cfg_t{};
    }

    void Instance::restart()
    {
	c4001q << restart_cfg_t{};
    }

    void Instance::c4001_thread_entry(void *_pThis, void *, void *)
    {
	Instance *pThis = (Instance*)(_pThis);
	return pThis->c4001_mainloop();
    }

    void Instance::c4001_mainloop()
    {
	QueueItem q;
	using Cfg = dfr::C4001::Configurator;
	while(1)
	{
	    c4001q >> q;
	    std::visit(
		overloaded{
		    [&](range_t const& v){ 
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
		    ,[&](range_trig_t const& v){  
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
		    ,[&](delay_t const& v){  
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
		    ,[&](sensitivity_t const& v){  
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
		    ,[&](inhibit_duration_t const& v){  
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
		    ,[&](save_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.SaveConfig(); !r && g_err)
			    g_err(err_t::SaveConfig);
		    }
		    ,[&](reset_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.ResetConfig(); !r)
			{
			    if (g_err) g_err(err_t::ResetConfig);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::All);
		    }
		    ,[&](restart_cfg_t const& v){  
			if (auto r = c4001
				.GetConfigurator()
				.Restart(); !r)
			{
			    if (g_err) g_err(err_t::Restart);
			}
			else if (g_upd)
			    g_upd(cfg_id_t::All);
		    }
		    ,[&](reload_cfg_t const& v){  
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
