/*
 * Copyright (c) 2023, Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define FORCE_FMT
#define PRINTF_FUNC(...) printk(__VA_ARGS__)

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>
#include "ld2412_task.hpp"
#include <nrf_uart/periphery/lib_ld2412_formatters.hpp>
#include <dk_buttons_and_leds.h>
#include <nrf_general/led.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ens160.h>
/**********************************************************************/
/* Zigbee                                                             */
/**********************************************************************/
#include <nrfzbcpp/zb_main.hpp>
#include "zb/zb_mem_cfg.hpp"
#include <nrfzbcpp/zb_std_cluster_desc.hpp>
#include <nrfzbcpp/zb_status_cluster_desc.hpp>
#include <nrfzbcpp/zb_occupancy_sensing_cluster_desc.hpp>
#include <nrfzbcpp/zb_humid_cluster_desc.hpp>
#include <nrfzbcpp/zb_temp_cluster_desc.hpp>
#include <nrfzbcpp/zb_co2_cluster_desc.hpp>
#include <nrfzbcpp/zb_nstd_air_q_cluster_desc.hpp>
#include "zb/zb_ld2412_cluster_desc.hpp"
#include <nrf_uart/periphery/lib_ld2412.hpp>
#include <osif/mac_platform.h>

#define MMWAVE_UART_NODE DT_ALIAS(mmwave_uart)
#define MMWAVE_UART_NODE2 DT_ALIAS(mmwave_uart2)
constinit const struct device *mmwave_uart1 = DEVICE_DT_GET(MMWAVE_UART_NODE);
constinit const struct device *mmwave_uart2 = DEVICE_DT_GET(MMWAVE_UART_NODE2);

/**********************************************************************/
/* LD2412 presence sensor configurations/data                         */
/**********************************************************************/
K_MSGQ_DEFINE_TYPED(ld2412::Queue, ld2412q_1);
K_MSGQ_DEFINE_TYPED(ld2412::Queue, ld2412q_2);

ld2412::Instance ld2412_1(ld2412q_1, mmwave_uart1, "ld2412_1");
ld2412::Instance ld2412_2(ld2412q_2, mmwave_uart2, "ld2412_2");

constinit static hlk::LD2412 *pLD2412_1 = nullptr;
constinit static hlk::LD2412 *pLD2412_2 = nullptr;

/**********************************************************************/
/* Zigbee Declarations and Definitions                                */
/**********************************************************************/
static bool g_ZigbeeReady = false;

/* Manufacturer name (32 bytes). */
#define INIT_BASIC_MANUF_NAME      "SFINAE"

/* Model number assigned by manufacturer (32-bytes long string). */
#define INIT_BASIC_MODEL_ID        "C4001-NG"
//#define INIT_SW_VER                "C4001-1.0"


/* Button used to enter the Bulb into the Identify mode. */
#define IDENTIFY_MODE_BUTTON            DK_BTN2_MSK

/* Button to start Factory Reset */
#define FACTORY_RESET_BUTTON IDENTIFY_MODE_BUTTON

/* Device endpoint, used to receive light controlling commands. */
constexpr int8_t kTX_POWER = 7;
constexpr uint8_t kMMW_EP = 1;
constexpr uint8_t kMMW_AUX_EP = 2;
constexpr uint16_t kDEV_ID = 0xBAAD;

struct device_ctx_t{
    zb::zb_zcl_basic_names_t basic_attr;
    zb::zb_zcl_status_t status_attr;
    zb::zb_zcl_occupancy_pir_and_ultrasonic_t occupancy;
    zb::zb_zcl_on_off_attrs_client_t on_off_client;
    zb::zb_zcl_ld2412_t ld2412_main;
    zb::zb_zcl_ld2412_t ld2412_aux;
    zb::zb_zcl_rel_humid_basic_t humidity;
    zb::zb_zcl_temp_basic_t temperature;
    zb::zb_zcl_co2_basic_t co2;
    zb::zb_zcl_air_q_t airq;
};

//attribute shortcuts for template arguments

/**********************************************************************/
/* Status attribute shortcuts                                         */
/**********************************************************************/
constexpr auto kAttrStatus1 = &zb::zb_zcl_status_t::status1;
constexpr auto kAttrStatus2 = &zb::zb_zcl_status_t::status2;
constexpr auto kAttrStatus3 = &zb::zb_zcl_status_t::status3;

/**********************************************************************/
/* LD2412 attributes                                                  */
/**********************************************************************/
constexpr auto kAttrBaseCfg     = &zb::zb_zcl_ld2412_t::base_config;
constexpr auto kAttrStillThr    = &zb::zb_zcl_ld2412_t::still_energy_thresholds;
constexpr auto kAttrMoveThr     = &zb::zb_zcl_ld2412_t::move_energy_thresholds;
constexpr auto kAttrLightLevel  = &zb::zb_zcl_ld2412_t::light_level;
constexpr auto kAttrFlags       = &zb::zb_zcl_ld2412_t::flags;
constexpr auto kAttrBT          = &zb::zb_zcl_ld2412_t::bluetooth_state;
constexpr auto kAttrStatStill   = &zb::zb_zcl_ld2412_t::energy_stat_still;
constexpr auto kAttrStatMove    = &zb::zb_zcl_ld2412_t::energy_stat_move;
constexpr auto kAttrLightSense  = &zb::zb_zcl_ld2412_t::light_sense;
constexpr auto kAttrStatWinSize = &zb::zb_zcl_ld2412_t::statistics_sample_count_window;


/**********************************************************************/
/* Humidity                                                           */
/**********************************************************************/
constexpr auto kAttrHumid = &zb::zb_zcl_rel_humid_basic_t::measured_value;


/**********************************************************************/
/* Temperature                                                        */
/**********************************************************************/
constexpr auto kAttrTemp = &zb::zb_zcl_temp_basic_t::measured_value;

/**********************************************************************/
/* CO2                                                                */
/**********************************************************************/
constexpr auto kAttrCO2 = &zb::zb_zcl_co2_basic_t::measured_value;

/**********************************************************************/
/* Air quality                                                        */
/**********************************************************************/
constexpr auto kAttrTVOC = &zb::zb_zcl_air_q_t::tvoc;
constexpr auto kAttrAQI = &zb::zb_zcl_air_q_t::aqi;

/**********************************************************************/
/* Occupancy attribute shortcuts                                      */
/**********************************************************************/
constexpr auto kAttrOccupancy = &zb::zb_zcl_occupancy_ultrasonic_t::occupancy;

constexpr auto kCmdOn = &zb::zb_zcl_on_off_attrs_client_t::on;
constexpr auto kCmdOff = &zb::zb_zcl_on_off_attrs_client_t::off;

template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_restart();
template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_factory_reset();
template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_run_back_analysis();
template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_do_stat_snapshot();

/* Zigbee device application context storage. */
static constinit device_ctx_t dev_ctx{
    .basic_attr = {
	{
	    .zcl_version = ZB_ZCL_VERSION,
	    .power_source = zb::zb_zcl_basic_min_t::PowerSource::DC,
	},
	/*.manufacturer =*/ INIT_BASIC_MANUF_NAME,
	/*.model =*/ INIT_BASIC_MODEL_ID,
    },
    .ld2412_main{
	.still_energy_thresholds = zb::zb_zcl_ld2412_t::gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15}
	,.move_energy_thresholds = zb::zb_zcl_ld2412_t::gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15}
	,.cmd_restart = {.cb = on_cmd_restart<ld2412_1>}
	,.cmd_factory_reset = {.cb = on_cmd_factory_reset<ld2412_1>}
	,.cmd_run_background_analysis = {.cb = on_cmd_run_back_analysis<ld2412_1>}
	,.cmd_take_statistic_snapshot = {.cb = on_cmd_do_stat_snapshot<ld2412_1>}
    },
    .ld2412_aux{
	.still_energy_thresholds = zb::zb_zcl_ld2412_t::gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15}
	,.move_energy_thresholds = zb::zb_zcl_ld2412_t::gate_array_t{100, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15}
	,.cmd_restart = {.cb = on_cmd_restart<ld2412_2>}
	,.cmd_factory_reset = {.cb = on_cmd_factory_reset<ld2412_2>}
	,.cmd_run_background_analysis = {.cb = on_cmd_run_back_analysis<ld2412_2>}
	,.cmd_take_statistic_snapshot = {.cb = on_cmd_do_stat_snapshot<ld2412_2>}
    },
};

constinit static auto zb_ctx = zb::make_device(
	zb::make_ep_args<{.ep=kMMW_EP, .dev_id=kDEV_ID, .dev_ver=1}>(
	    dev_ctx.basic_attr
	    , dev_ctx.status_attr
	    , dev_ctx.occupancy
	    , dev_ctx.ld2412_main
	    , dev_ctx.humidity
	    , dev_ctx.temperature
	    , dev_ctx.co2
	    , dev_ctx.airq
	    , dev_ctx.on_off_client
	),
	zb::make_ep_args<{.ep=kMMW_AUX_EP, .dev_id=kDEV_ID, .dev_ver=1}>(
	    dev_ctx.ld2412_aux
	)
    );

/**********************************************************************/
/* Defining access to the global zigbee device context                */
/**********************************************************************/
//needed for proper command handling
struct zb::global_device
{
    static auto& get() { return zb_ctx; }
};

//a shortcut for a convenient access
constinit static auto &zb_ep = zb_ctx.ep<kMMW_EP>();
constinit static auto &zb_ep_aux = zb_ctx.ep<kMMW_AUX_EP>();

template<ld2412::Instance &i>
auto& get_zb_ep_for_ld2412()
{
    if constexpr (&i == &ld2412_1)
	return zb_ep;
    else if (&i == &ld2412_2)
	return zb_ep_aux;
}

template<ld2412::Instance &i>
zb::zb_zcl_ld2412_t& get_data_for_ld2412()
{
    if constexpr (&i == &ld2412_1)
	return dev_ctx.ld2412_main;
    else if (&i == &ld2412_2)
	return dev_ctx.ld2412_aux;
}

/**********************************************************************/
/* Device defines                                                     */
/**********************************************************************/
/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(userled0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

///* Get the node from the alias */
#define SENSOR_NODE DT_ALIAS(presence)
#define SENSOR_NODE2 DT_ALIAS(presence2)
#define PIR_NODE2 DT_ALIAS(pir)

/* Get the GPIO spec directly from the node */
/* Note: We look for the property "gpios" inside the node */
static const struct gpio_dt_spec presence = GPIO_DT_SPEC_GET(SENSOR_NODE, gpios);
static const struct gpio_dt_spec presence2 = GPIO_DT_SPEC_GET(SENSOR_NODE2, gpios);
static const struct gpio_dt_spec pir = GPIO_DT_SPEC_GET(PIR_NODE2, gpios);

static const struct device *const eco2sensor = DEVICE_DT_GET(DT_NODELABEL(eco2sensor));
static const struct device *const rht2sensor = DEVICE_DT_GET(DT_NODELABEL(rht2sensor));

/**********************************************************************/
/* Template helper. TODO: move to some generic lib                    */
/**********************************************************************/
template<class T>
struct method_1st_arg_t;

template<class T, class Arg>
struct method_1st_arg_t<void(T::*)(Arg)>
{
    using type = Arg;
};

template<auto pM>
using arg1_type_t = method_1st_arg_t<decltype(pM)>::type;

template<auto &inst, auto M>
void method_fwd(arg1_type_t<M> a)
{
    (inst.*M)(a);
}

template<size_t N>
auto as_print_dest(zb::ZigbeeStr<N> &str)
{
    return tools::BufferFormatter(str.name + 1, str.size());
}

void send_on_off(uint8_t val);

gpio_callback g_on_presence_triggered_cb;
gpio_callback g_on_presence_triggered_cb2;

int g_state_presence1 = 0;
int g_state_presence2 = 0;

template<gpio_dt_spec const& pin, int &state, int const& state_aux>
void presence_triggered(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins)
{
    int old_val = state || state_aux;
    state = gpio_pin_get_dt(&pin);
    int val = state || state_aux;
    if (val != old_val)
    {
	//TODO: remove me
	gpio_pin_set_dt(&led0, val);

	if (g_ZigbeeReady) //post to zigbee and shoot commands
	    zb_schedule_app_callback(&send_on_off, val);
	else
	{
	    //write latest state directly
	    dev_ctx.occupancy.occupancy = val;
	}
    }
}

template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_restart()
{
    printk("ld2412::restart\r\n");
    i.restart();
    return {};
}

template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_factory_reset()
{
    printk("ld2412::factory_reset\r\n");
    i.factory_reset();
    return {};
}

template<ld2412::Instance &i>
void on_back_analysis_done(ld2412::run_background_analysis_t::Result result)
{
    auto &d = get_data_for_ld2412<i>();
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto flags = d.flags;
    flags.background_analysis_active = false;
    flags.background_analysis_ok = result == ld2412::run_background_analysis_t::Result::Ok;
    ep.template attr<kAttrFlags>() = flags;
}

template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_run_back_analysis()
{
    printk("ld2412::run_back_analysis\r\n");
    i.run_back_analysis({&on_back_analysis_done<i>});

    auto &d = get_data_for_ld2412<i>();
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto flags = d.flags;
    flags.background_analysis_active = true;
    flags.background_analysis_ok = true;
    ep.template attr<kAttrFlags>() = flags;

    return {};
}

template<ld2412::Instance &i>
void on_get_stat_snapshot(hlk::LD2412::energy_stat_array_t const& still, hlk::LD2412::energy_stat_array_t const& move)
{
    auto &d = get_data_for_ld2412<i>();
    auto &ep = get_zb_ep_for_ld2412<i>();
    ep.template attr<kAttrStatStill>() = still;
    ep.template attr<kAttrStatMove>() = move;
}

template<ld2412::Instance &i>
zb::CmdHandlingResult on_cmd_do_stat_snapshot()
{
    printk("ld2412::do_stat_snapshot\r\n");
    i.take_statistic_snapshot({&on_get_stat_snapshot<i>});
    return {};
}

void send_on_off(uint8_t val)
{
    zb_ep.attr<kAttrOccupancy>() = val == 1;
    if (val == 1)
	zb_ep.send_cmd<kCmdOn>();
    else
	zb_ep.send_cmd<kCmdOff>();
}

void on_dev_cb_error(int err)
{
    printk("on_dev_cb_error: %d\r\n", err);
}

template<ld2412::Instance &i>
void zb_ld2412_update_light_sense()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto *pLD2412 = i.sensor();
    zb::zb_zcl_ld2412_t::light_sense_cfg_t l;
    l.mode = pLD2412->GetLightSensitivityMode();
    l.threshold = pLD2412->GetLightSensitivityThreshold();
    ep.template attr<kAttrLightSense>() = l;
}

template<ld2412::Instance &i>
void zb_ld2412_update_thresholds()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto *pLD2412 = i.sensor();
    ep.template attr<kAttrStillThr>() = pLD2412->GetAllStillThresholds();
    ep.template attr<kAttrMoveThr>() = pLD2412->GetAllMoveThresholds();
}

template<ld2412::Instance &i>
void zb_ld2412_update_flags()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto &d = get_data_for_ld2412<i>();
    auto *pLD2412 = i.sensor();
    zb::zb_zcl_ld2412_t::flags_t f = d.flags;
    f.background_analysis_active = i.is_running_back_analysis();
    ep.template attr<kAttrFlags>() = f;
    ep.template attr<kAttrBT>() = pLD2412->GetLastBluetoothState();
}

template<ld2412::Instance &i>
void zb_ld2412_update_stat_collection()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto &d = get_data_for_ld2412<i>();
    ep.template attr<kAttrStatWinSize>() = i.get_stat_collect_window_size();
}

template<ld2412::Instance &i>
void zb_ld2412_update_base_config()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto &d = get_data_for_ld2412<i>();
    auto *pLD2412 = i.sensor();
    zb::zb_zcl_ld2412_t::base_cfg_t base;
    base.clear_delay = pLD2412->GetTimeout();
    base.distance_resolution = pLD2412->GetDistanceRes();
    base.range_min = pLD2412->GetMinDistance() / 100.f;
    base.range_max = pLD2412->GetMaxDistance() / 100.f;
    ep.template attr<kAttrBaseCfg>() = base;
}

template<ld2412::Instance &i>
void zb_ld2412_error(uint8_t e)
{
    using namespace ld2412;
    //generally: set zb attributes to current values
    switch(err_t(e))
    {
	using enum err_t;
	case Restart:
	case ReloadConfig:
	case FactoryReset:
	{
	    zb_ld2412_update_base_config<i>();
	    zb_ld2412_update_flags<i>();
	    zb_ld2412_update_light_sense<i>();
	    zb_ld2412_update_thresholds<i>();
	    zb_ld2412_update_stat_collection<i>();
	}
	break;
	case Bluetooth:
	    zb_ld2412_update_flags<i>();
	break;
	case SetBasicCfg:
	    zb_ld2412_update_base_config<i>();
	break;
	case SetLightSenseCfg:
	    zb_ld2412_update_light_sense<i>();
	break;
	case SetEnergyThresholds:
	    zb_ld2412_update_thresholds<i>();
	break;
	case RunBackAnalysis:
	    zb_ld2412_update_flags<i>();
	break;
	case ConfigureCollectStatistics:
	    zb_ld2412_update_stat_collection<i>();
	break;
	default:
	break;
    }
    zb_ep.attr<kAttrStatus1>() = e;
}

template<ld2412::Instance &i>
void on_ld2412_error(ld2412::err_t e)
{
    if (g_ZigbeeReady)
	zb_schedule_app_callback(&zb_ld2412_error<i>, (uint8_t)e);
}

template<ld2412::Instance &i>
void zb_ld2412_notify(uint8_t id)
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto &d = get_data_for_ld2412<i>();
    auto *pLD2412 = i.sensor();
    zb::zb_zcl_ld2412_t::flags_t f = d.flags;

    switch(ld2412::notification_id_t(id))
    {
	case ld2412::notification_id_t::BackgroundAnalysisDone:
	    f.background_analysis_active = false;
	    f.background_analysis_ok = true;
	    ep.template attr<kAttrFlags>() = f;
	    break;
	case ld2412::notification_id_t::BackgroundAnalysisError:
	    f.background_analysis_active = false;
	    f.background_analysis_ok = false;
	    ep.template attr<kAttrFlags>() = f;
	    break;
    }
}

template<ld2412::Instance &i>
void on_ld2412_notify(ld2412::notification_id_t id)
{
    if (g_ZigbeeReady)
	zb_schedule_app_callback(&zb_ld2412_notify<i>, (uint8_t)id);
}

template<ld2412::Instance &i>
void update_dev_ctx_from_ld2412()
{
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto &d = get_data_for_ld2412<i>();
    auto *pLD2412 = i.sensor();

    d.bluetooth_state = pLD2412->GetLastBluetoothState();
    d.flags.background_analysis_active = pLD2412->IsDynamicBackgroundAnalysisRunning();
    d.light_sense->mode = pLD2412->GetLightSensitivityMode();
    d.light_sense->threshold = pLD2412->GetLightSensitivityThreshold();
    d.base_config->distance_resolution = pLD2412->GetDistanceRes();
    d.base_config->clear_delay = pLD2412->GetTimeout();
    d.base_config->range_min = pLD2412->GetMinDistance() / 100.f;
    d.base_config->range_max = pLD2412->GetMaxDistance() / 100.f;
    d.light_level = pLD2412->GetMeasuredLight();
    d.bluetooth_mac = pLD2412->GetBluetoothMAC();
    tools::format_to_silent(as_print_dest(d.sw_ver), "{}", pLD2412->GetVersion());
    d.statistics_sample_count_window = 0;
}

template<ld2412::Instance &i>
void on_set_base_config(zb::zb_zcl_ld2412_t::base_cfg_t const& cfg)
{
    i.set_basic_config({
	    .resolution = cfg.distance_resolution
	    , .gate_from = hlk::LD2412::GetGateFromDistanceCM(cfg.range_min * 100.f, cfg.distance_resolution)
	    , .gate_to = hlk::LD2412::GetGateFromDistanceCM(cfg.range_max * 100.f, cfg.distance_resolution)
	    , .clear_delay = cfg.clear_delay
	});
}

template<ld2412::Instance &i>
void on_set_light_sense(zb::zb_zcl_ld2412_t::light_sense_cfg_t const& cfg)
{
    i.set_light_sense({ .mode = cfg.mode, .threshold = cfg.threshold });
}

int configure_ld2412_out_pin();

zb::ZbTimerExt g_EnvironmentSensorFetcher;

bool update_environment_sensors()
{
    if (device_is_ready(rht2sensor))
    {
	sensor_sample_fetch(rht2sensor);
	sensor_value v;
	sensor_channel_get(rht2sensor, sensor_channel::SENSOR_CHAN_AMBIENT_TEMP, &v);
        sensor_attr_set(eco2sensor, SENSOR_CHAN_ALL, (sensor_attribute)SENSOR_ATTR_ENS160_TEMP, &v);
	zb_ep.attr<kAttrTemp>() = zb::zb_zcl_temp_basic_t::FromC(float(v.val1) + float(v.val2) / 1000'000.f);

	sensor_channel_get(rht2sensor, sensor_channel::SENSOR_CHAN_HUMIDITY, &v);
        sensor_attr_set(eco2sensor, SENSOR_CHAN_ALL, (sensor_attribute)SENSOR_ATTR_ENS160_RH, &v);
	zb_ep.attr<kAttrHumid>() = zb::zb_zcl_rel_humid_basic_t::FromRelH(float(v.val1) + float(v.val2) / 1000'000.f);

	sensor_sample_fetch(eco2sensor);
	sensor_channel_get(eco2sensor, sensor_channel::SENSOR_CHAN_CO2, &v);
	zb_ep.attr<kAttrCO2>() = float(v.val1) / 1000'000.f;
	sensor_channel_get(eco2sensor, sensor_channel::SENSOR_CHAN_VOC, &v);
	zb_ep.attr<kAttrTVOC>() = float(v.val1);
	sensor_channel_get(eco2sensor, (sensor_channel)SENSOR_CHAN_ENS160_AQI, &v);
	zb_ep.attr<kAttrAQI>() = (zb::zb_zcl_air_q_t::AQI)v.val1;
    }
    return true;
}

/**@brief Callback for TX power setting result.
 *
 * @param  param  Reference to Zigbee stack buffer.
 */
static void tx_power_cb(zb_bufid_t param)
{
	const char *status = NULL;
	zb_tx_power_params_t *power_params = (zb_tx_power_params_t *)zb_buf_begin(param);

	switch (power_params->status) {
	case RET_OK:
		status = "success";
		break;
	case RET_INVALID_PARAMETER_1:
		status = "invalid page";
		break;
	case RET_INVALID_PARAMETER_2:
		status = "invalid channel";
		break;
	case RET_INVALID_PARAMETER_3:
		status = "invalid tx power";
		break;
	default:
		status = "unknown";
		break;
	}

	printk("Zigbee transceiver tx power set result (pg: %d, ch: %d, pw: %d): %s\r\n",
		power_params->page, power_params->channel, power_params->tx_power, status);

	zb_buf_free(param);
}

/**@brief Function for scheduling TX power set.
 *
 * @param  param    Reference to Zigbee stack buffer.
 * @param  channel  Channel number.
 */
static void schedule_tx_power_set(zb_bufid_t param, zb_uint16_t channel)
{
	zb_tx_power_params_t *power_params;

	power_params = (zb_tx_power_params_t *)zb_buf_initial_alloc(param, sizeof(zb_tx_power_params_t));

	power_params->page = ZB_CHANNEL_PAGE0_2_4_GHZ;
	power_params->channel = channel;
	power_params->tx_power = kTX_POWER;
	power_params->cb = tx_power_cb;

	ZB_SCHEDULE_APP_CALLBACK(zb_set_tx_power_async, param);
}

/**@brief Function for setting TX power for configured channels.
 */
void set_tx_power(void)
{
	zb_ret_t ret;
	uint32_t channel_mask;
	uint8_t channel = ZB_TRANSCEIVER_START_CHANNEL_NUMBER;

#if defined(CONFIG_ZIGBEE_CHANNEL_SELECTION_MODE_SINGLE)
	channel_mask = 1 << CONFIG_ZIGBEE_CHANNEL;
#elif defined(CONFIG_ZIGBEE_CHANNEL_SELECTION_MODE_MULTI)
	channel_mask = CONFIG_ZIGBEE_CHANNEL_MASK;
#endif

	for (; channel <= ZB_TRANSCEIVER_MAX_CHANNEL_NUMBER; channel++) {
		if (channel_mask & (1 << channel)) {
			printk("Setting tx power for channel %d: %d dBm\r\n", channel,
				kTX_POWER);
			ret = zb_buf_get_out_delayed_ext(schedule_tx_power_set, channel, 0);
			if (ret != RET_OK) {
				printk("Failed to set tx power for channel %d, error %d\r\n",
					channel, ret);
			}
		}
	}
}

void on_zigbee_start()
{
    printk("on_zigbee_start\r\n");
    g_ZigbeeReady = true;
    g_EnvironmentSensorFetcher.Setup(update_environment_sensors, 15000);
}

/**@brief Zigbee stack event handler.
 *
 * @param[in]   bufid   Reference to the Zigbee stack buffer
 *                      used to pass signal.
 */
void zboss_signal_handler(zb_bufid_t bufid)
{
        zb_zdo_app_signal_hdr_t *pHdr;
        auto signalId = zb_get_app_signal(bufid, &pHdr);

	auto ret = zb::tpl_signal_handler<zb::sig_handlers_t{
	.on_leave = +[]{ 
	    printk("left the network\r\n");
	},
	    //.on_error = []{ led::show_pattern(led::kPATTERN_3_BLIPS_NORMED, 1000); },
	    .on_dev_reboot = on_zigbee_start,
	    .on_steering = on_zigbee_start,
	   }>(bufid);
    const uint32_t LOCAL_ERR_CODE = (uint32_t) (-ret);	
    if (LOCAL_ERR_CODE != RET_OK) {				
	zb_osif_abort();				
    }							
}

zb::ZbTimerExt g_FactoryResetDoneChecker;
/**@brief Callback for button events.
 *
 * @param[in]   button_state  Bitmask containing the state of the buttons.
 * @param[in]   has_changed   Bitmask containing buttons that have changed their state.
 */
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
    if (FACTORY_RESET_BUTTON & has_changed) {
	if (FACTORY_RESET_BUTTON & button_state) {
	    /* Button changed its state to pressed */
	    g_FactoryResetDoneChecker.Setup([]{
		    if (was_factory_reset_done()) {
			/* The long press was for Factory Reset */
			led::show_pattern(led::kPATTERN_2_BLIPS_NORMED, 2000);
			return false;
		    }
		    return true;
	    }, 1000);
	} else {
	    /* Button changed its state to released */
	    if (!was_factory_reset_done()) {
		/* Button released before Factory Reset */
		g_FactoryResetDoneChecker.Cancel();
		led::show_pattern(led::kPATTERN_2_BLIPS_NORMED, 500);
	    }
	}
	check_factory_reset_button(button_state, has_changed);
    }
}

gpio_callback g_test_pir_cb;

void test_pir(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins)
{
    int state = gpio_pin_get_dt(&pir);
    gpio_pin_set_dt(&led0, state);
}

int config_pir()
{
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&led0, 0);
    int err = gpio_pin_configure_dt(&pir, GPIO_INPUT);
    if (err != 0)
    {
	printk("gpio_pin_configure_dt: %d\r\n", err);
	return err;
    }
    err = gpio_pin_interrupt_configure_dt(&pir, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("gpio_pin_interrupt_configure_dt: %d\r\n", err);
	return err;
    }

    gpio_init_callback(&g_test_pir_cb, test_pir, BIT(pir.pin));
    err = gpio_add_callback_dt(&pir, &g_test_pir_cb);
    return err;
}

int test_ld2412()
{
    pLD2412_1 = ld2412_1.setup(&on_ld2412_error<ld2412_1>, &on_ld2412_notify<ld2412_1>);

    config_pir();
    hlk::LD2412 ld(mmwave_uart1);
    auto initRes = ld.Init();
    if (!initRes)
    {
	FMT_PRINTLN("Init failed with {}", initRes.error());
	return 1;
    }
    FMT_PRINTLN("Version: {}", ld.GetVersion());
    FMT_PRINTLN("BT: {}", ld.GetBluetoothMAC());
    FMT_PRINTLN("SysMode: {}", ld.GetSystemMode());
    FMT_PRINTLN("Timeout: {}", ld.GetTimeout());
    FMT_PRINTLN("Dist resolution: {}", ld.GetDistanceRes());
    FMT_PRINTLN("Min dist: {}", ld.GetMinDistance());
    FMT_PRINTLN("Max dist: {}", ld.GetMaxDistance());
    FMT_PRINTLN("Light sense mode: {}", ld.GetLightSensitivityMode());
    FMT_PRINTLN("Light sense Threshold: {}", ld.GetLightSensitivityThreshold());
    for(int i = 0; i < 14; ++i)
    {
	FMT_PRINTLN("Gate {}; Threshold: move: {}; still: {}", i + 1, ld.GetMoveThreshold(i), ld.GetStillThreshold(i));
    }

    FMT_PRINTLN("Switching to energy mode...");
    auto ccR = ld.ChangeConfiguration()
	.SetSystemMode(hlk::LD2412::SystemMode::Energy)
    .EndChange();
    if (!ccR)
    {
	FMT_PRINTLN("Switching to energy mode failed with {}", ccR.error());
    }
    else
    {
	FMT_PRINTLN("Switching to energy mode OK");
    }

    ld.StartContinuousReading();
    while(true)
    {
	//auto readFrame = ld.TryReadSingleFrame();
	auto readFrame = ld.TryReadFrame();
	if (!readFrame)
	{
	    FMT_PRINTLN("Failed to read frame with {}", readFrame.error());
	    return 2;
	}
	FMT_PRINTLN("Presence: {}; Light: {}", ld.GetPresence(), ld.GetMeasuredLight());
	if (ld.GetSystemMode() == hlk::LD2412::SystemMode::Energy)
	{
	    printk("E: ");
	    for(int i = 0; i < 14; ++i)
	    {
		FMT_PRINT("G{};M:{};S:{}|", i + 1, ld.GetMeasuredMoveEnergy(i), ld.GetMeasuredStillEnergy(i));
	    }
	    printk("\n");
	}
    }
    return 0;
}

int main(void)
{
    printk("waiting...\n");
    k_msleep(10000);
    auto r = test_ld2412();
    k_msleep(60000);
    return r;


    int err = settings_subsys_init();
    err = settings_load();

    led::setup();
    led::start();


    //configure button handler
    err = dk_buttons_init(button_changed);
    if (dk_get_buttons() & FACTORY_RESET_BUTTON)
    {
	//zigbee button is pressed
	zigbee_erase_persistent_storage(true);
	led::show_pattern(led::kPATTERN_4_BLIPS_NORMED, 500);
    }
    //assign a button for a factory reset procedure
    register_factory_reset_button(FACTORY_RESET_BUTTON);

    printk("main\r\n");

    pLD2412_1 = ld2412_1.setup(&on_ld2412_error<ld2412_1>, &on_ld2412_notify<ld2412_1>);
    if (pLD2412_1)
    {
	//TODO: the other way around and update the config on ld2412 from stored settings?
	update_dev_ctx_from_ld2412<ld2412_1>();
    }else
    {
	printk("LD2412 not found\r\n");
	int val = 1;
	while(true)
	{
	    gpio_pin_set_dt(&led0, val);
	    k_msleep(1000);
	    val ^= 1;
	    printk("LD2412 not found; led: %d\r\n", val);
	}
	return 0;
    }

    pLD2412_2 = ld2412_2.setup(&on_ld2412_error<ld2412_2>, &on_ld2412_notify<ld2412_2>);
    if (pLD2412_2)
    {
	//TODO: the other way around and update the config on ld2412 from stored settings?
	update_dev_ctx_from_ld2412<ld2412_2>();
    }else
    {
	printk("LD2412(aux) not found\r\n");
	int val = 1;
	while(true)
	{
	    gpio_pin_set_dt(&led0, val);
	    k_msleep(500);
	    val ^= 1;
	    printk("LD2412(aux) not found; led: %d\r\n", val);
	}
	return 0;
    }

    dev_ctx.occupancy.occupancy = false;

    /* Register callback for handling ZCL commands. */
    auto dev_cb = zb::tpl_device_cb<
	zb::dev_cb_handlers_desc{ .error_handler = on_dev_cb_error }
	, zb::handle_set_for<kAttrBaseCfg,               &on_set_base_config<ld2412_1>>(zb_ep)
	, zb::handle_set_for<kAttrLightSense,            &on_set_light_sense<ld2412_1>>(zb_ep)
	, zb::handle_set_for<kAttrStillThr,              method_fwd<ld2412_1, &ld2412::Instance::set_still_thresholds_raw>>(zb_ep)
	, zb::handle_set_for<kAttrMoveThr,               method_fwd<ld2412_1, &ld2412::Instance::set_move_thresholds_raw>>(zb_ep)
	, zb::handle_set_for<kAttrStatWinSize,           method_fwd<ld2412_1, &ld2412::Instance::collect_statistics>>(zb_ep)
	//, zb::handle_set_for<kAttrDetectToClearDelay, on_set_detect_to_clear_delay>(zb_ep)
	//, zb::handle_set_for<kAttrClearToDetectDelay, on_set_clear_to_detect_delay>(zb_ep)
	//, zb::handle_set_for<kAttrRMin,               method_fwd<c4001_1, &c4001::Instance::set_range_from>>(zb_ep)
	//, zb::handle_set_for<kAttrRMax,               method_fwd<c4001_1, &c4001::Instance::set_range_to>>(zb_ep)
	//, zb::handle_set_for<kAttrRTrig,              method_fwd<c4001_1, &c4001::Instance::set_range_trig>>(zb_ep)
	//, zb::handle_set_for<kAttrInhibitDuration,    method_fwd<c4001_1, &c4001::Instance::set_inhibit_duration>>(zb_ep)
	//, zb::handle_set_for<kAttrSTrig,              method_fwd<c4001_1, &c4001::Instance::set_detect_sensitivity>>(zb_ep)
	//, zb::handle_set_for<kAttrSHold,              method_fwd<c4001_1, &c4001::Instance::set_hold_sensitivity>>(zb_ep)
	//
	//, zb::handle_set_for<kAttrRMin,               method_fwd<c4001_2, &c4001::Instance::set_range_from>>(zb_ep_aux)
	//, zb::handle_set_for<kAttrRMax,               method_fwd<c4001_2, &c4001::Instance::set_range_to>>(zb_ep_aux)
	//, zb::handle_set_for<kAttrRTrig,              method_fwd<c4001_2, &c4001::Instance::set_range_trig>>(zb_ep_aux)
	//, zb::handle_set_for<kAttrInhibitDuration,    method_fwd<c4001_2, &c4001::Instance::set_inhibit_duration>>(zb_ep_aux)
	//, zb::handle_set_for<kAttrSTrig,              method_fwd<c4001_2, &c4001::Instance::set_detect_sensitivity>>(zb_ep_aux)
	//, zb::handle_set_for<kAttrSHold,              method_fwd<c4001_2, &c4001::Instance::set_hold_sensitivity>>(zb_ep_aux)
    >;

    ZB_ZCL_REGISTER_DEVICE_CB(dev_cb);

    /* Register device context (endpoints). */
    ZB_AF_REGISTER_DEVICE_CTX(zb_ctx);

    if (int err = configure_ld2412_out_pin(); err != 0)
    {
	printk("Failed to configure c4001 out pin\r\n");
    }
    {
	int val = gpio_pin_get_dt(&presence);
	printk("Presence pin state: %d\r\n", val);
	dev_ctx.occupancy.occupancy = val;
    }
    set_tx_power();
    zigbee_enable();

    printk("Main: sleep forever\r\n");
	//   {
	//printk("C4001(1); HW=%s\r\n", pC4001_1->GetHWVer().m_Version);
	//printk("C4001(1); SW=%s\r\n", pC4001_1->GetSWVer().m_Version);
	//printk("C4001(1); Range=%.1f to %.1fm\r\n", (double)pC4001_1->GetRangeFrom(), (double)pC4001_1->GetRangeTo());
	//printk("C4001(1); Latency; to detect=%.1fs; to clear=%.1fs\r\n", (double)pC4001_1->GetDetectLatency(), (double)pC4001_1->GetClearLatency());
	//printk("C4001(1); Trig Range=%.1fm\r\n", (double)pC4001_1->GetTriggerDistance());
	//printk("C4001(1); Sensitivity Detect=%d; Hold=%d;\r\n", pC4001_1->GetSensitivityTrig(), pC4001_1->GetSensitivityHold());
	//printk("C4001(1); Inhibut Duration=%.1fs\r\n", (double)pC4001_1->GetInhibitDuration());
	//   }
	//   {
	//printk("C4001(2); HW=%s\r\n", pC4001_2->GetHWVer().m_Version);
	//printk("C4001(2); SW=%s\r\n", pC4001_2->GetSWVer().m_Version);
	//printk("C4001(2); Range=%.1f to %.1fm\r\n", (double)pC4001_2->GetRangeFrom(), (double)pC4001_2->GetRangeTo());
	//printk("C4001(2); Latency; to detect=%.1fs; to clear=%.1fs\r\n", (double)pC4001_2->GetDetectLatency(), (double)pC4001_2->GetClearLatency());
	//printk("C4001(2); Trig Range=%.1fm\r\n", (double)pC4001_2->GetTriggerDistance());
	//printk("C4001(2); Sensitivity Detect=%d; Hold=%d;\r\n", pC4001_2->GetSensitivityTrig(), pC4001_2->GetSensitivityHold());
	//printk("C4001(2); Inhibut Duration=%.1fs\r\n", (double)pC4001_2->GetInhibitDuration());
	//   }
    while (1) {
	k_sleep(K_FOREVER);
    }

    return 0;
}

int configure_ld2412_out_pin()
{
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    int err = gpio_pin_configure_dt(&presence, GPIO_INPUT);
    if (err != 0)
    {
	printk("gpio_pin_configure_dt: %d\r\n", err);
	return err;
    }
    gpio_pin_set_dt(&led0, 0);

    err = gpio_pin_interrupt_configure_dt(&presence, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("gpio_pin_interrupt_configure_dt: %d\r\n", err);
	return err;
    }
    g_state_presence1 = gpio_pin_get_dt(&presence);

    err = gpio_pin_interrupt_configure_dt(&presence2, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("gpio_pin_interrupt_configure_dt(2): %d\r\n", err);
	return err;
    }
    g_state_presence2 = gpio_pin_get_dt(&presence2);

    if (g_state_presence1 || g_state_presence2)
	dev_ctx.occupancy.occupancy = true;

    gpio_init_callback(&g_on_presence_triggered_cb, presence_triggered<presence, g_state_presence1, g_state_presence2>, BIT(presence.pin));
    err = gpio_add_callback_dt(&presence, &g_on_presence_triggered_cb);
    if (!err)
    {
	gpio_init_callback(&g_on_presence_triggered_cb2, presence_triggered<presence2, g_state_presence2, g_state_presence1>, BIT(presence2.pin));
	err = gpio_add_callback_dt(&presence2, &g_on_presence_triggered_cb2);
    }
    return err;
}

