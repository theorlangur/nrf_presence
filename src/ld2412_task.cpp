#include "ld2412_task.hpp"
#include <lib_misc_helpers.hpp>
#include <nrf_uart/periphery/lib_ld2412_formatters.hpp>

namespace ld2412
{
    Instance::Instance(Queue &q, const struct device *uart, const char* thread_name):
	m_ThreadName(thread_name)
	, m_Q(q)
	, m_pUART(uart)
	, m_Sensor(uart)
    {
    }

    hlk::LD2412* Instance::setup(err_callback_t err, notify_callback_t notification)
    {
	m_ThreadID = {};
	if (!device_is_ready(m_pUART))
	{
	    printk("uart for ld2412 is not ready\r\n");
	    return {};
	}

	auto r = m_Sensor.Init();
	if (!r)
	    return {};

	r = m_Sensor.ChangeConfiguration()
	    .SetSystemMode(hlk::LD2412::SystemMode::Energy)
	.EndChange();
	if (!r)
	    return {};

	m_ErrCB = err;
	m_NotifyCB = notification;

	m_ThreadID = k_thread_create(&m_Thread 
	    ,m_ThreadStack
	    ,sizeof(m_ThreadStack)
	    ,thread_func
	    ,this, nullptr, nullptr
	    ,THREAD_PRIORITY
	    , 0
	    , {0});
	if (!m_ThreadID)
	    return {};
	k_thread_name_set(m_ThreadID, m_ThreadName);
	return &m_Sensor;
    }

    hlk::LD2412* Instance::sensor()
    {
	if (m_ThreadID)
	    return &m_Sensor;
	else
	    return {};
    }

    void Instance::restart()
    {
	m_Q << restart_cfg_t{};
    }
    void Instance::reload_config()
    {
	m_Q << reload_cfg_t{};
    }
    void Instance::factory_reset()
    {
	m_Q << factory_reset_cfg_t{};
    }
    void Instance::switch_bluetooth(bool on)
    {
	m_Q << bt_cfg_t{.on = on};
    }
    void Instance::set_basic_config(basic_cfg_t const& cfg)
    {
	m_Q << cfg;
    }
    void Instance::set_light_sense(light_sense_cfg_t const& cfg)
    {
	m_Q << cfg;
    }
    void Instance::set_still_thresholds(still_thresholds_cfg_t const& cfg)
    {
	FMT_PRINTLN("still thresholds: {}", cfg.still_thresholds);
	m_Q << cfg;
    }
    void Instance::set_move_thresholds(move_thresholds_cfg_t const& cfg)
    {
	FMT_PRINTLN("move thresholds: {}", cfg.move_thresholds);
	m_Q << cfg;
    }
    void Instance::run_back_analysis(run_background_analysis_t const& cfg)
    {
	m_Q << cfg;
    }
    void Instance::collect_statistics(uint8_t win_size)
    {
	m_Q << collect_statistics_cfg_t{win_size};
    }
    void Instance::take_statistic_snapshot(snapshot_statistics_cfg_t const& cfg)
    {
	m_Q << cfg;
    }

    void Instance::thread_func(void *_pThis, void *, void *)
    {
	Instance *pThis = (Instance*)(_pThis);
	return pThis->mainloop();
    }

    void Instance::collect_sample()
    {
	if (m_StatSampleCount)//we're collecting statistics
	{
	    auto &sample = m_StatSampleBuffer[m_StatSampleIndex];
	    sample.still_energies = m_Sensor.GetAllMeasuredStillEnergies();
	    sample.move_energies = m_Sensor.GetAllMeasuredMoveEnergies();
	    m_StatSampleIndex = (m_StatSampleIndex + 1) % m_StatSampleCount;
	    ++m_TotalSamplesWritten;
	}
    }

    void Instance::check_back_analysis()
    {
	if (m_BackgroundAnalysisDoneCB)
	{
	    run_background_analysis_t::Result r;
	    switch(m_Sensor.GetPresence().m_State)
	    {
		case hlk::LD2412::TargetState::BackgroundAnalysisOk:
		    r = run_background_analysis_t::Result::Ok;
		    break;
		case hlk::LD2412::TargetState::BackgroundAnalysisFailed:
		    r = run_background_analysis_t::Result::Failed;
		    break;
		case hlk::LD2412::TargetState::BackgroundAnalysisRunning:
		    return;
		default:
		    if (m_Sensor.IsDynamicBackgroundAnalysisRunning())
			return;
		    //assuming done
		    r = run_background_analysis_t::Result::Ok;
		    break;
	    }

	    m_BackgroundAnalysisDoneCB(r);
	    m_BackgroundAnalysisDoneCB = nullptr;
	}
    }

    void Instance::mainloop()
    {
	QueueItem q;
	bool processMessage = true;
	bool readFrameNow = false;

	auto last_frame_time = k_uptime_get();

	while(1)
	{
	    if (m_StatSampleCount || m_BackgroundAnalysisDoneCB)
		readFrameNow = true;
	    else
	    {
		auto interval = m_LightSensorUpdateInterval;
		readFrameNow = (k_uptime_get() - last_frame_time) >= interval;
	    }

	    if (readFrameNow)
	    {
		auto r = m_Sensor.TryReadSingleFrame(3, hlk::LD2412::Drain::Try);
		if (r)
		{
		    last_frame_time = k_uptime_get();
		    collect_sample();
		    check_back_analysis();
		}else
		    ++m_FrameReadErrorCount;
		processMessage = m_Q.Recv(q, K_NO_WAIT) == 0;
	    }
	    else
	    {
		m_Q >> q;
		processMessage = true;
	    }

	    if (processMessage)
	    {
		std::visit(
		    overloaded{
			[&](restart_cfg_t const&)
			{
			    if (auto r = m_Sensor.Restart(); !r)
			    {
				FMT_PRINTLN("Restart failed with: {}", r.error());
				if (m_ErrCB) m_ErrCB(err_t::Restart);
			    }
			}
			,[&](reload_cfg_t const&)
			{
			    if (auto r = m_Sensor.ReloadConfig(); !r)
			    {
				FMT_PRINTLN("Reload failed with: {}", r.error());
				if (m_ErrCB) m_ErrCB(err_t::ReloadConfig);
			    }
			}
			,[&](factory_reset_cfg_t const&)
			{
			    if (auto r = m_Sensor.FactoryReset(); !r)
			    {
				FMT_PRINTLN("Factory reset failed with: {}", r.error());
				if (m_ErrCB) m_ErrCB(err_t::FactoryReset);
			    }
			}
			,[&](bt_cfg_t const& cfg)
			{
			    if (auto r = m_Sensor.SwitchBluetooth(cfg.on); !r)
			    {
				FMT_PRINTLN("BT switch failed with: {}", r.error());
				if (m_ErrCB) m_ErrCB(err_t::Bluetooth);
			    }
			}
			,[&](basic_cfg_t const& cfg)
			{
			    auto r = m_Sensor.ChangeConfiguration(true)
				.SetDistanceRes(cfg.resolution)
				.SetMinDistanceRaw(cfg.gate_from)
				.SetMaxDistanceRaw(cfg.gate_to)
				.SetTimeout(cfg.clear_delay)
			    .EndChange();
			    if (!r)
			    {
				FMT_PRINTLN("Basic config set failed with: {}", r.error());
			    }
			    else if (r && m_NotifyCB)
				m_NotifyCB(notification_id_t::SetBasicCfgDone);
			    if (!r && m_ErrCB)
				m_ErrCB(err_t::SetBasicCfg);
			}
			,[&](light_sense_cfg_t const& cfg)
			{
			    auto r = m_Sensor.ChangeConfiguration(true)
				.SetLightSensitivity(cfg.mode, cfg.threshold)
			    .EndChange();
			    if (!r)
			    {
				FMT_PRINTLN("Light sense set failed with: {}", r.error());
			    }
			    else if (r && m_NotifyCB)
				m_NotifyCB(notification_id_t::SetLightSenseDone);
			    if (!r && m_ErrCB)
				m_ErrCB(err_t::SetLightSenseCfg);
			}
			,[&](still_thresholds_cfg_t const& cfg)
			{
			    auto r = m_Sensor.ChangeConfiguration(true)
				.SetStillThresholds(cfg.still_thresholds)
			    .EndChange();
			    if (!r)
			    {
				FMT_PRINTLN("Still thr set failed with: {}", r.error());
			    }
			    else if (r && m_NotifyCB)
				m_NotifyCB(notification_id_t::SetEnergyThresholdsDone);
			    if (!r && m_ErrCB)
				m_ErrCB(err_t::SetEnergyThresholds);
			}
			,[&](move_thresholds_cfg_t const& cfg)
			{
			    auto r = m_Sensor.ChangeConfiguration(true)
				.SetMoveThresholds(cfg.move_thresholds)
			    .EndChange();
			    if (!r)
			    {
				FMT_PRINTLN("Move thr set failed with: {}", r.error());
			    }
			    else if (r && m_NotifyCB)
				m_NotifyCB(notification_id_t::SetEnergyThresholdsDone);
			    if (!r && m_ErrCB)
				m_ErrCB(err_t::SetEnergyThresholds);
			}
			,[&](run_background_analysis_t const& cfg)
			{
			    if (!m_Sensor.RunDynamicBackgroundAnalysis())
			    {
				if (m_ErrCB)
				    m_ErrCB(err_t::RunBackAnalysis);
			    }else
				m_BackgroundAnalysisDoneCB = cfg.cb;
			}
			,[&](collect_statistics_cfg_t const& cfg)
			{
			    m_StatSampleCount = cfg.window_size;
			    if (m_StatSampleCount > MAX_STAT_SAMPLE_SIZE)
				m_StatSampleCount = MAX_STAT_SAMPLE_SIZE;
			    m_StatSampleIndex = 0;
			    m_TotalSamplesWritten = 0;
			    FMT_PRINTLN("{}: Collecting stats with window {}", m_ThreadName, m_StatSampleCount);
			    if (m_Sensor.GetSystemMode() != hlk::LD2412::SystemMode::Energy)
			    {
				FMT_PRINTLN("{}: changing mode to 'Energy'", m_ThreadName);
				auto r = m_Sensor.ChangeConfiguration()
				    .SetSystemMode(hlk::LD2412::SystemMode::Energy)
				.EndChange();
				if (!r)
				{
				    FMT_PRINTLN("{}: changing mode failed with {}", m_ThreadName, r.error());
				}
			    }
			}
			,[&](snapshot_statistics_cfg_t const& cfg)
			{
			    if (cfg.cb)
			    {
				if (!m_TotalSamplesWritten)
				{
				    FMT_PRINTLN("{}: no samples collected. Frame reading errors: {}", m_ThreadName, m_FrameReadErrorCount);
				    if (m_ErrCB)
					m_ErrCB(err_t::SnapshotStatistics);
				    return;
				}
				hlk::LD2412::energy_stat_array_t still, move;
				size_t sum_still[14];  
				size_t sum_move[14];  

				for(int g = 0; g < 14; ++g)
				{
				    auto still_e = m_StatSampleBuffer[0].still_energies[g];
				    auto move_e = m_StatSampleBuffer[0].move_energies[g];

				    sum_still[g] = still[g].max = still[g].min = still_e;
				    sum_move[g] = move[g].max = move[g].min = move_e;
				}

				size_t n = std::min(m_TotalSamplesWritten, m_StatSampleCount);
				for(size_t sample = 1; sample < n; ++sample)
				{
				    for(int g = 0; g < 14; ++g)
				    {
					auto still_e = m_StatSampleBuffer[sample].still_energies[g];
					auto move_e = m_StatSampleBuffer[sample].move_energies[g];

					sum_still[g] += still_e;
					sum_move[g] += move_e;
				    }
				}

				for(int g = 0; g < 14; ++g)
				{
				    still[g].avg = sum_still[g] / n;
				    move[g].avg = sum_move[g] / n;
				}

				cfg.cb(still, move);
			    }
			}
		    },
		     q
		);
	    }
	}
    }
}
