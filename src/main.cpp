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

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/task_wdt/task_wdt.h>
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
#include "zb/zb_dev_ctrl_cluster_desc.hpp"
#include <osif/mac_platform.h>

#include <atomic>
//#include <meta>

extern "C"{
#include <zephyr/debug/coredump.h>

void nrf_flash_skip_sync(bool skip);
}

constexpr bool kDebug = false;

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
#define INIT_BASIC_MODEL_ID        "LD2412-NG"
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

constexpr uint16_t kInitialMinClearTimeout = 3;//seconds
constexpr uint32_t kZigbeeFloodProtectionTimeout = 1000;//ms

struct device_ctx_t{
    zb::zb_zcl_basic_names_t basic_attr;
    zb::zb_zcl_status_t status_attr;
    zb::zb_zcl_dev_ctrl_t dev_attr;
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
/* Device control attributes                                          */
/**********************************************************************/
constexpr auto kAttrStillEnergyMain = &zb::zb_zcl_dev_ctrl_t::main_still_energy_analysis;
constexpr auto kAttrStillEnergyAux = &zb::zb_zcl_dev_ctrl_t::aux_still_energy_analysis;

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
zb::cmd_handling_result_t on_cmd_restart();
template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_factory_reset();
template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_run_back_analysis();
template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_do_stat_snapshot();

zb::cmd_handling_result_t on_cmd_stop_wd_feeding();
zb::cmd_handling_result_t on_cmd_clear_coredump();

zb::cmd_handling_result_t on_cmd_start_analysis_for_presence();
zb::cmd_handling_result_t on_cmd_start_analysis_for_absence();
zb::cmd_handling_result_t on_cmd_stop_analysis();

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
    .status_attr{
	.cmd1 = {.cb = on_cmd_stop_wd_feeding}
	,.cmd2 = {.cb = on_cmd_clear_coredump}
    },
    .dev_attr{
	.cmd_start_analysis_for_presence = { .cb = on_cmd_start_analysis_for_presence }
	,.cmd_start_analysis_for_absense = { .cb = on_cmd_start_analysis_for_absence }
	,.cmd_stop_analysis              = { .cb = on_cmd_stop_analysis }
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

constinit static zb::device_full_t zb_ctx{
	zb::make_ep_args<{.ep=kMMW_EP, .dev_id=kDEV_ID, .dev_ver=1}>(
	    dev_ctx.basic_attr
	    , dev_ctx.status_attr
	    , dev_ctx.dev_attr
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
};

/**********************************************************************/
/* Defining access to the global zigbee device context                */
/**********************************************************************/
//needed for proper command handling
struct zb::global_device
{
    static auto& get() { return zb_ctx; }
};

union status3_t
{
    uint16_t s;
    struct{
	uint16_t has_coredump: 1;
	uint16_t wdt_error: 1;
	uint16_t analysis_for_presence: 1;
	uint16_t analysis_for_absence: 1;
	uint16_t reset_reason_pin: 1;
	uint16_t reset_reason_wdt: 1;
	uint16_t reset_reason_sw: 1;
	uint16_t reset_reason_cpu_lockup: 1;
	uint16_t reset_reason_low_power_wake: 1;
	uint16_t reset_reason_dbg: 1;
	uint16_t unused: 6;
    }bits;
};

//a shortcut for a convenient access
constinit static auto &zb_ep = zb_ctx.ep<kMMW_EP>();
constinit static auto &zb_ep_aux = zb_ctx.ep<kMMW_AUX_EP>();

template<ld2412::Instance &i>
auto& get_zb_ep_for_ld2412()
{
    if constexpr (&i == &ld2412_1)
	return zb_ep;
    else if constexpr (&i == &ld2412_2)
	return zb_ep_aux;
}

template<ld2412::Instance &i>
zb::zb_zcl_ld2412_t& get_data_for_ld2412()
{
    if constexpr (&i == &ld2412_1)
	return dev_ctx.ld2412_main;
    else if constexpr (&i == &ld2412_2)
	return dev_ctx.ld2412_aux;
}

template<ld2412::Instance &i>
const char* get_name_for_ld2412()
{
    if constexpr (&i == &ld2412_1)
	return "main";
    else if constexpr (&i == &ld2412_2)
	return "aux";
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
auto as_print_dest(zb::zigbee_str_t<N> &str)
{
    return tools::BufferFormatter(str.name + 1, str.capacity());
}


/**********************************************************************/
/* Fault end-handling                                                 */
/**********************************************************************/
void ultimate_timer_fail()
{
    printk("ultimate_timer_fail");
    constexpr uint32_t kPATTERN_dot_dash_dot = 0xf00ff00f;
    while(true)
    {
	led::show_pattern(kPATTERN_dot_dash_dot, 2000);
	k_msleep(2000);
    }
}

void ultimate_cmd_fail()
{
    printk("ultimate_cmd_fail");
    constexpr uint32_t kPATTERN_dash_dot_dash = 0xff03c0ff;
    while(true)
    {
	led::show_pattern(kPATTERN_dash_dot_dash, 2000);
	k_msleep(2000);
    }
}

void ultimate_zb_fail(zb_ret_t r)
{
    printk("ultimate_zb_fail: %d", r);
    constexpr uint32_t kPATTERN_rapid_blinking = 0xaaaaaaaa;
    while(true)
    {
	led::show_pattern(kPATTERN_rapid_blinking, 2000);
	k_msleep(2000);
    }
}

/**********************************************************************/
/* Watchdog                                                           */
/**********************************************************************/
constexpr static uint32_t WD_SWTimeoutMS = CONFIG_TASK_WDT_MIN_TIMEOUT;
constexpr static uint32_t WD_CoredumpReservedTime = CONFIG_TASK_WDT_HW_FALLBACK_DELAY;
constexpr static uint32_t WD_HWTimeoutMS = WD_SWTimeoutMS + WD_CoredumpReservedTime;
bool g_WD_FeedTheDog = true;
const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
zb::zb_timer_ext_16_t g_WDTFeeder;
int wdt_channel_id = -1;
int configure_wdt();

/**********************************************************************/
/* Presence                                                           */
/**********************************************************************/

using atomic_state_t = std::atomic<uint8_t>;
constinit atomic_state_t g_PresenceChangeState{0};
constexpr uint8_t HWUnsetState = 0xff;
constexpr uint8_t OnOffVirtualMask = 0xfe;
constinit atomic_state_t g_PresenceHWState{HWUnsetState};


void send_on_off(uint8_t val);
void log_presence_change(uint8_t val);

union PresenceChange
{
    static constexpr uint8_t kChangedOnlyMask = 0x7 << 3;
    struct
    {
	uint8_t pir : 1;
	uint8_t main : 1;
	uint8_t aux : 1;
	uint8_t pir_changed : 1;
	uint8_t main_changed : 1;
	uint8_t aux_changed : 1;
	uint8_t unused : 2;
    }bits;
    uint8_t val;
};

gpio_callback g_on_ld2412_triggered_main;
gpio_callback g_on_ld2412_triggered_aux;
gpio_callback g_on_pir_triggered;

int g_ld2412_main_presence_out = 0;
int g_ld2412_aux_presence_out = 0;
int g_pir_presence = 0;
uint8_t g_presence_state = false;

void presence_triggered(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins)
{
    bool pir_changed = pins & BIT(pir.pin);
    bool main_changed = pins & BIT(presence.pin);
    bool aux_changed = pins & BIT(presence2.pin);
    int new_pir = g_pir_presence;
    int new_main = g_ld2412_main_presence_out;
    int new_aux = g_ld2412_aux_presence_out;

    uint8_t new_presence_state = g_presence_state;

    if (pir_changed) new_pir = gpio_pin_get_dt(&pir);
    if (main_changed) new_main = gpio_pin_get_dt(&presence);
    if (aux_changed) new_aux = gpio_pin_get_dt(&presence2);

    if (pir_changed && new_pir && !g_pir_presence)
	new_presence_state = true;
    else if (!new_pir && !new_main && !new_aux)
	new_presence_state = false;

    if (g_ZigbeeReady) //post to zigbee and shoot commands
    {
	PresenceChange v = {.val = 0};
	v.bits.pir_changed = pir_changed;
	v.bits.main_changed = main_changed;
	v.bits.aux_changed = aux_changed;
	v.bits.pir = new_pir;
	v.bits.main = new_main;
	v.bits.aux = new_aux;

	{
	    uint8_t cur = g_PresenceChangeState.load();
	    while(!g_PresenceChangeState.compare_exchange_strong(cur, ((cur & PresenceChange::kChangedOnlyMask) | v.val)));
	    if (!cur)//completely clear
		zb_schedule_app_callback(&log_presence_change, v.val);
	}
    }

    g_pir_presence = new_pir;
    g_ld2412_main_presence_out = new_main;
    g_ld2412_aux_presence_out = new_aux;


    if (new_presence_state != g_presence_state)
    {
	g_presence_state = new_presence_state;

	if (g_ZigbeeReady) //post to zigbee and shoot commands
	{
	    if (g_PresenceHWState.exchange(g_presence_state) == HWUnsetState)
		zb_schedule_app_callback(&send_on_off, g_presence_state);
	}
	else
	{
	    gpio_pin_set_dt(&led0, g_presence_state);
	    //write latest state directly
	    dev_ctx.occupancy.occupancy = g_presence_state;
	}
    }
}

template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_restart()
{
    printk("ld2412::restart\r\n");
    i.restart();
    return {};
}

template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_factory_reset()
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
    FMT_PRINTLN("on_back_analysis_done: ok={}", (int)flags.background_analysis_ok);
    ep.template attr<kAttrFlags>() = flags;
}

template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_run_back_analysis()
{
    printk("ld2412::run_back_analysis\r\n");
    i.run_back_analysis({&on_back_analysis_done<i>});

    auto &d = get_data_for_ld2412<i>();
    auto &ep = get_zb_ep_for_ld2412<i>();
    auto flags = d.flags;
    flags.background_analysis_active = true;
    flags.background_analysis_ok = true;
    ep.template attr<kAttrFlags>() = flags;

    zb_ep.attr<kAttrStatus1>() = 0;//reset error
    return {};
}

template<ld2412::Instance &i>
void on_get_stat_snapshot(hlk::LD2412::energy_stat_array_t const& still, hlk::LD2412::energy_stat_array_t const& move)
{
    auto &d = get_data_for_ld2412<i>();
    auto &ep = get_zb_ep_for_ld2412<i>();
    printk("stats for  %s:\r\n", get_name_for_ld2412<i>());
    printk("stat still: ");
    for(int j = 0; j < 14; ++j)
    {
	printk("%d=[min: %d; max: %d; avg: %d] ", j, still[j].min, still[j].max, still[j].avg);
    }
    printk("\r\n");
    printk("stat move: ");
    for(int j = 0; j < 14; ++j)
    {
	printk("%d=[min: %d; max: %d; avg: %d] ", j, move[j].min, move[j].max, move[j].avg);
    }
    printk("\r\n");
    ep.template attr<kAttrStatStill>() = still;
    ep.template attr<kAttrStatMove>() = move;
}

template<ld2412::Instance &i>
zb::cmd_handling_result_t on_cmd_do_stat_snapshot()
{
    printk("(%s)ld2412::do_stat_snapshot\r\n", get_name_for_ld2412<i>());
    i.take_statistic_snapshot({&on_get_stat_snapshot<i>});
    return {};
}

constexpr uint8_t kOccupancyFromDebug = 0x40;
constexpr uint8_t kOccupancyClearFromTimer = 0x80;
zb::zb_alarm_ext_t<> g_OccupancyResetProtection;
uint8_t g_LastRegisteredOccupancyState = 0;

void on_occupancy_protection_finished()
{
    if (g_LastRegisteredOccupancyState == 0)
    {
	//last registered is 'clear'
	zb_schedule_app_callback(&send_on_off, kOccupancyClearFromTimer);
    }
}

void send_on_off_zb(uint8_t val)
{
    zb_ep.attr<kAttrOccupancy>() = val == 1;
    if (val == 1)
    {
	if (!zb_ep.send_cmd<kCmdOn>())
	    ultimate_cmd_fail();
    }
    else
    {
	if (!zb_ep.send_cmd<kCmdOff>())
	    ultimate_cmd_fail();
    }
}

zb::zb_alarm_ext_t<> g_ZbFloodGate;
constinit uint8_t g_DelayedVal = HWUnsetState;
void send_on_off_zb_flood_protected(uint8_t val)
{
    if (g_ZbFloodGate.IsRunning())
	g_DelayedVal = val;
    else
    {
	send_on_off_zb(val);
	g_ZbFloodGate.Setup([]{
		if (g_DelayedVal != HWUnsetState)
		{
		    send_on_off_zb_flood_protected(std::exchange(g_DelayedVal, HWUnsetState));
		}
	    }, kZigbeeFloodProtectionTimeout);
    }
}

void send_on_off(uint8_t val)
{
    auto prevRegisteredState = g_LastRegisteredOccupancyState;
    if ((val & kOccupancyFromDebug))
    {
	g_LastRegisteredOccupancyState = val & 1;
    }

    if (!(val & OnOffVirtualMask))//no 'higher' bits (all bits but bit 0) a present
    {
	//taking last state
	val = g_PresenceHWState.exchange(HWUnsetState);
	g_LastRegisteredOccupancyState = val;
    }else
    {
	val &= ~OnOffVirtualMask;
	if (!val && g_LastRegisteredOccupancyState)
	    return;//we still have occupancy 
    }

    if (g_OccupancyResetProtection.IsRunning())
	return;

    if (!prevRegisteredState && g_LastRegisteredOccupancyState)
    {   //0->1
	//start occupancy protection timer
	auto min_clear_delay = std::max(
		kInitialMinClearTimeout
		,dev_ctx.occupancy.UltrasonicOccupiedToUnoccupiedDelay
	);
	if (min_clear_delay)
	{
	    if (g_OccupancyResetProtection.Setup(on_occupancy_protection_finished, min_clear_delay * 1000) != RET_OK)
		ultimate_timer_fail();
	}
    }

    gpio_pin_set_dt(&led0, val == 1);

    send_on_off_zb_flood_protected(val);
}

int16_t get_presence_as_status(uint8_t val)
{
    return int16_t(val & 0x07) | int16_t(((val >> 3) & 0x07) << 8);
}

void log_presence_change(uint8_t val)
{
    val = g_PresenceChangeState.exchange(0);
    PresenceChange v = {.val = val};
    if (v.bits.pir_changed) printk("pir=%d; ", v.bits.pir);
    if (v.bits.main_changed) printk("main=%d; ", v.bits.main);
    if (v.bits.aux_changed) printk("aux=%d; ", v.bits.aux);
    printk("\r\n");

    zb_ep.attr<kAttrStatus2>() = get_presence_as_status(val);
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
    auto const& still = pLD2412->GetAllStillThresholds();
    auto const& move = pLD2412->GetAllMoveThresholds();
    FMT_PRINTLN("zb updated thr still to {}", still);
    FMT_PRINTLN("zb updated thr move to {}", move);
    ep.template attr<kAttrStillThr>() = still;
    ep.template attr<kAttrMoveThr>() = move;
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
    FMT_PRINTLN("Base cfg updated to: [r_from={}, r_min={}, res={}, del={}]", (float)base.range_min, (float)base.range_max, base.distance_resolution, (uint16_t)base.clear_delay);
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
	case EnergyModeAfterBackAnalysis:
	case RunBackAnalysis:
	    zb_ld2412_update_flags<i>();
	break;
	case ConfigureCollectStatistics:
	    zb_ld2412_update_stat_collection<i>();
	break;
	default:
	break;
    }
    FMT_PRINTLN("zb_ld2412_error: e={}", (int)e);
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
	    FMT_PRINTLN("zb notify: back done: ok");
	    f.background_analysis_active = false;
	    f.background_analysis_ok = true;
	    ep.template attr<kAttrFlags>() = f;
	    break;
	case ld2412::notification_id_t::BackgroundAnalysisError:
	    FMT_PRINTLN("zb notify: back done: failed");
	    f.background_analysis_active = false;
	    f.background_analysis_ok = false;
	    ep.template attr<kAttrFlags>() = f;
	    break;
	case ld2412::notification_id_t::SetBasicCfgDone:
	    FMT_PRINTLN("zb notify: set basic cfg: ok");
	    zb_ld2412_update_base_config<i>();
	    break;
	case ld2412::notification_id_t::SetLightSenseDone:
	    FMT_PRINTLN("zb notify: set light sesne: ok");
	    zb_ld2412_update_light_sense<i>();
	    break;
	case ld2412::notification_id_t::SetEnergyThresholdsDone:
	    FMT_PRINTLN("zb notify: set energy threshold: ok");
	    zb_ld2412_update_thresholds<i>();
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

    d.still_energy_thresholds = pLD2412->GetAllStillThresholds();
    d.move_energy_thresholds = pLD2412->GetAllMoveThresholds();
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

int configure_presence_pins();

zb::zb_timer_ext_t<> g_EnvironmentSensorFetcher;

bool update_environment_sensors()
{
    if (device_is_ready(rht2sensor))
    {
	zb_ep.dump_info<kCmdOn, kCmdOff>();
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

constinit uint32_t reset_reasons = 0;

void on_zigbee_start()
{
    printk("on_zigbee_start\r\n");
    g_ZigbeeReady = true;
    if (g_EnvironmentSensorFetcher.Setup(update_environment_sensors, 15000) != RET_OK)
	ultimate_timer_fail();

    status3_t s;
    s.s = dev_ctx.status_attr.status3;
    uint16_t hasCoredump = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, nullptr) == 1;

    s.bits.wdt_error = configure_wdt() == -1;
    s.bits.has_coredump = hasCoredump;
    s.bits.reset_reason_pin = (reset_reasons & RESET_PIN) != 0;
    s.bits.reset_reason_wdt = (reset_reasons & RESET_WATCHDOG) != 0;
    s.bits.reset_reason_sw = (reset_reasons & RESET_SOFTWARE) != 0;
    s.bits.reset_reason_cpu_lockup = (reset_reasons & RESET_CPU_LOCKUP) != 0;
    s.bits.reset_reason_low_power_wake = (reset_reasons & RESET_LOW_POWER_WAKE) != 0;
    s.bits.reset_reason_dbg = (reset_reasons & RESET_DEBUG) != 0;
    zb_ep.attr<kAttrStatus3>() = s.s;
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

zb::zb_timer_ext_t<> g_FactoryResetDoneChecker;
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
	    auto r = g_FactoryResetDoneChecker.Setup([]{
		    if (was_factory_reset_done()) {
			/* The long press was for Factory Reset */
			led::show_pattern(led::kPATTERN_2_BLIPS_NORMED, 2000);
			return false;
		    }
		    return true;
	    }, 1000);
	    if (r != RET_OK)
		ultimate_timer_fail();
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

void print_ld2412_config(hlk::LD2412 &ld)
{
    FMT_PRINTLN("Version: {}", ld.GetVersion());
    FMT_PRINTLN("BT: {}", ld.GetBluetoothMAC());
    FMT_PRINTLN("SysMode: {}", ld.GetSystemMode());
    FMT_PRINTLN("Timeout: {}", ld.GetTimeout());
    FMT_PRINTLN("Dist resolution: {}", ld.GetDistanceRes());
    FMT_PRINTLN("Min dist: {}", ld.GetMinDistance()/100.f);
    FMT_PRINTLN("Max dist: {}", ld.GetMaxDistance()/100.f);
    FMT_PRINTLN("Light sense mode: {}", ld.GetLightSensitivityMode());
    FMT_PRINTLN("Light sense Threshold: {}", ld.GetLightSensitivityThreshold());
    for(int i = 0; i < 14; ++i)
    {
	FMT_PRINTLN("Gate {}; Threshold: move: {}; still: {}", i + 1, ld.GetMoveThreshold(i), ld.GetStillThreshold(i));
    }
}

zb::cmd_handling_result_t on_cmd_start_analysis_for_presence()
{
    ld2412_1.start_analysis_for_presence({});
    ld2412_2.start_analysis_for_presence({});

    status3_t s;
    s.s = dev_ctx.status_attr.status3;
    s.bits.analysis_for_absence = false;
    s.bits.analysis_for_presence = true;
    zb_ep.attr<kAttrStatus3>() = s.s;
    return {};
}

zb::cmd_handling_result_t on_cmd_start_analysis_for_absence()
{
    ld2412_1.start_analysis_for_absence({});
    ld2412_2.start_analysis_for_absence({});

    status3_t s;
    s.s = dev_ctx.status_attr.status3;
    s.bits.analysis_for_absence = true;
    s.bits.analysis_for_presence = false;
    zb_ep.attr<kAttrStatus3>() = s.s;
    return {};
}

zb::cmd_handling_result_t on_cmd_stop_analysis()
{
    ld2412_1.stop_analysis({});
    ld2412_2.stop_analysis({});

    hlk::LD2412::gate_array_t resultsMain;
    ld2412_1.get_analysis_results(resultsMain);
    zb_ep.attr<kAttrStillEnergyMain>() = resultsMain;

    hlk::LD2412::gate_array_t resultsAux;
    ld2412_2.get_analysis_results(resultsAux);
    zb_ep.attr<kAttrStillEnergyAux>() = resultsAux;

    status3_t s;
    s.s = dev_ctx.status_attr.status3;
    s.bits.analysis_for_absence = false;
    s.bits.analysis_for_presence = false;
    zb_ep.attr<kAttrStatus3>() = s.s;
    return {};
}

zb::cmd_handling_result_t on_cmd_clear_coredump()
{
    coredump_cmd(COREDUMP_CMD_INVALIDATE_STORED_DUMP, nullptr);
    uint16_t hasCoredump = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, nullptr) == 1;
    reset_reasons = 0;
    status3_t s;
    s.s = dev_ctx.status_attr.status3;
    s.s &= ~(0b111111 << 4);//set all reset_* to 0
    zb_ep.attr<kAttrStatus3>() = s.s;
    return {};
}

zb::cmd_handling_result_t on_cmd_stop_wd_feeding()
{
    g_WD_FeedTheDog = false;
    return {};
}

static void wdt_callback(int channel_id, void *user_data)
{
    nrf_flash_skip_sync(true);
    k_panic();
}

int configure_wdt()
{
    return 0;


    if (!device_is_ready(wdt)) {
	printk("%s: device not ready.\n", wdt->name);
	return -1;
    }

	//   struct wdt_timeout_cfg wdt_config = {
	//	/* Expire watchdog after max window */
	//	.window = {
	//	    .min = 0,
	//	    .max = WD_HWTimeoutMS,
	//	},
	//
	//	/* Reset SoC when watchdog timer expires. */
	//	.flags = WDT_FLAG_RESET_SOC,
	//};
	//
	//   wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	//   if (wdt_channel_id < 0) {
	//printk("Watchdog install error\n");
	//return wdt_channel_id;
	//   }
	//
	//   int err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	//   if (err < 0) {
	//printk("Watchdog setup error\n");
	//return err;
	//   }

    int ret = task_wdt_init(wdt);
    if (ret < 0)
    {
	printk("Could not initialize WDT task.\n");
	return ret;
    }

    wdt_channel_id = task_wdt_add(WD_SWTimeoutMS, wdt_callback, nullptr);
    if (wdt_channel_id < 0)
    {
	printk("Could not create a channel from WDT task.\n");
	return wdt_channel_id;
    }

    printk("feeder configured: ch=%d\r\n", wdt_channel_id);
    //starting the feeding sequence
    g_WDTFeeder.Setup([]{ 
	    printk("feeder: %d; ch=%d\r\n", g_WD_FeedTheDog, wdt_channel_id);
	    if (g_WD_FeedTheDog) 
	    {
		task_wdt_feed(wdt_channel_id); 
	    }
	    return true;
	}, 
    1000);
    return 0;
}

void disable_hw_wdt()
{
    if (!device_is_ready(wdt)) {
	printk("%s: device not ready.\r\n", wdt->name);
	return;
    }

    int err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err < 0)
    {
	printk("disable_hw_wdt: setup failed with %d; %s\r\n", err, strerror(-err));
	return;
    }
    int ret = wdt_disable(wdt);
    if (ret < 0)
    {
	printk("wdt_disabled failed with %d; %s\r\n", ret, strerror(-ret));
	return;
    }
    printk("hw wdt disabled\r\n");
}

int main(void)
{
    static_assert(atomic_state_t::is_always_lock_free);
    disable_hw_wdt();
    int err = settings_subsys_init();
    hwinfo_get_reset_cause(&reset_reasons);
    hwinfo_clear_reset_cause();

    led::setup();
    led::start();

    zb::g_GlobalErrorHandler = ultimate_zb_fail;

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
    if(!pLD2412_1)
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
    if (!pLD2412_2)
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
	zb::dev_cb_handlers_desc_t{ .error_handler = on_dev_cb_error }
	//main instance
	, zb::handle_set_for<kAttrBaseCfg,               &on_set_base_config<ld2412_1>>(zb_ep)
	, zb::handle_set_for<kAttrLightSense,            &on_set_light_sense<ld2412_1>>(zb_ep)
	, zb::handle_set_for<kAttrStillThr,              method_fwd<ld2412_1, &ld2412::Instance::set_still_thresholds_raw>>(zb_ep)
	, zb::handle_set_for<kAttrMoveThr,               method_fwd<ld2412_1, &ld2412::Instance::set_move_thresholds_raw>>(zb_ep)
	, zb::handle_set_for<kAttrStatWinSize,           method_fwd<ld2412_1, &ld2412::Instance::collect_statistics>>(zb_ep)
	, zb::handle_set_for<kAttrBT,                    method_fwd<ld2412_1, &ld2412::Instance::switch_bluetooth>>(zb_ep)
	//aux instance
	, zb::handle_set_for<kAttrBaseCfg,               &on_set_base_config<ld2412_2>>(zb_ep_aux)
	, zb::handle_set_for<kAttrLightSense,            &on_set_light_sense<ld2412_2>>(zb_ep_aux)
	, zb::handle_set_for<kAttrStillThr,              method_fwd<ld2412_2, &ld2412::Instance::set_still_thresholds_raw>>(zb_ep_aux)
	, zb::handle_set_for<kAttrMoveThr,               method_fwd<ld2412_2, &ld2412::Instance::set_move_thresholds_raw>>(zb_ep_aux)
	, zb::handle_set_for<kAttrStatWinSize,           method_fwd<ld2412_2, &ld2412::Instance::collect_statistics>>(zb_ep_aux)
	, zb::handle_set_for<kAttrBT,                    method_fwd<ld2412_2, &ld2412::Instance::switch_bluetooth>>(zb_ep_aux)
    >;

    ZB_ZCL_REGISTER_DEVICE_CB(dev_cb);

    /* Register device context (endpoints). */
    ZB_AF_REGISTER_DEVICE_CTX(zb_ctx);

    err = settings_load();

    //real config from the device takes precedense
    update_dev_ctx_from_ld2412<ld2412_1>();
    update_dev_ctx_from_ld2412<ld2412_2>();

    if (int err = configure_presence_pins(); err != 0)
    {
	printk("Failed to configure c4001 out pin\r\n");
    }
    {
	int p1 = gpio_pin_get_dt(&presence);
	int p2 = gpio_pin_get_dt(&presence2);
	int pirVal = gpio_pin_get_dt(&pir);
	g_presence_state = p1 | p2 | pirVal;
	printk("Presence pin state: %d\r\n", g_presence_state);
	dev_ctx.occupancy.occupancy = g_presence_state;
    }
    zigbee_enable();

    printk("Main: sleep forever\r\n");
    FMT_PRINTLN("-----LD2412 main-----");
    print_ld2412_config(*pLD2412_1);
    FMT_PRINTLN("-----LD2412 aux-----");
    print_ld2412_config(*pLD2412_2);

    while (1) {
	k_sleep(K_FOREVER);
    }

    return 0;
}

int configure_presence_pins()
{
    PresenceChange state;
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    int err = gpio_pin_configure_dt(&presence, GPIO_INPUT);
    if (err != 0)
    {
	printk("(main)gpio_pin_configure_dt: %d\r\n", err);
	return err;
    }
    gpio_pin_set_dt(&led0, 0);

    err = gpio_pin_interrupt_configure_dt(&presence, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("(main)gpio_pin_interrupt_configure_dt: %d\r\n", err);
	return err;
    }
    g_ld2412_main_presence_out = gpio_pin_get_dt(&presence);
    state.bits.main = g_ld2412_main_presence_out;
    printk("(main)initial: %d\r\n", g_ld2412_main_presence_out);

    err = gpio_pin_interrupt_configure_dt(&presence2, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("(aux)gpio_pin_interrupt_configure_dt: %d\r\n", err);
	return err;
    }
    g_ld2412_aux_presence_out = gpio_pin_get_dt(&presence2);
    state.bits.aux = g_ld2412_aux_presence_out;
    printk("(aux)initial: %d\r\n", g_ld2412_aux_presence_out);

    err = gpio_pin_configure_dt(&pir, GPIO_INPUT);
    if (err != 0)
    {
	printk("(pir)gpio_pin_configure_dt: %d\r\n", err);
	return err;
    }
    err = gpio_pin_interrupt_configure_dt(&pir, GPIO_INT_EDGE_BOTH);
    if (err != 0)
    {
	printk("(pir)gpio_pin_interrupt_configure_dt: %d\r\n", err);
	return err;
    }
    g_pir_presence = gpio_pin_get_dt(&pir);
    state.bits.pir = g_pir_presence;

    dev_ctx.occupancy.occupancy = g_pir_presence;//initially only conservatively by PIR
    dev_ctx.status_attr.status2 = get_presence_as_status(state.val);

    gpio_init_callback(&g_on_ld2412_triggered_main, presence_triggered, BIT(presence.pin));
    err = gpio_add_callback_dt(&presence, &g_on_ld2412_triggered_main);
    if (!err)
    {
	gpio_init_callback(&g_on_ld2412_triggered_aux, presence_triggered, BIT(presence2.pin));
	err = gpio_add_callback_dt(&presence2, &g_on_ld2412_triggered_aux);
	if (!err)
	{
	    gpio_init_callback(&g_on_pir_triggered, presence_triggered, BIT(pir.pin));
	    err = gpio_add_callback_dt(&pir, &g_on_pir_triggered);
	    if (err != 0)
		printk("(pir)gpio_add_callback_dt: %d\r\n", err);
	}else
	    printk("(aux)gpio_add_callback_dt: %d\r\n", err);
    }else
	printk("(main)gpio_add_callback_dt: %d\r\n", err);
    return err;
}

