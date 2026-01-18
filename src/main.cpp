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
#include "c4001_task.hpp"
#include <dk_buttons_and_leds.h>
#include <nrf_general/led.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ens160.h>
/**********************************************************************/
/* Zigbee                                                             */
/**********************************************************************/
#include <nrfzbcpp/zb_main.hpp>
#include <nrfzbcpp/zb_std_cluster_desc.hpp>
#include <nrfzbcpp/zb_status_cluster_desc.hpp>
#include <nrfzbcpp/zb_occupancy_sensing_cluster_desc.hpp>
#include <nrfzbcpp/zb_humid_cluster_desc.hpp>
#include <nrfzbcpp/zb_temp_cluster_desc.hpp>
#include <nrfzbcpp/zb_co2_cluster_desc.hpp>
#include <nrfzbcpp/zb_nstd_air_q_cluster_desc.hpp>
#include "zb/zb_c4001_cluster_desc.hpp"

/**********************************************************************/
/* C4001 presence sensor configurations/data                          */
/**********************************************************************/
#define DFR_UART_NODE DT_ALIAS(dfr_uart)
#define DFR_UART_NODE2 DT_ALIAS(dfr_uart2)
constinit const struct device *c4001_uart1 = DEVICE_DT_GET(DFR_UART_NODE);
constinit const struct device *c4001_uart2 = DEVICE_DT_GET(DFR_UART_NODE2);

K_MSGQ_DEFINE_TYPED(c4001::Instance::C4001Q, c4001q_1);
K_MSGQ_DEFINE_TYPED(c4001::Instance::C4001Q, c4001q_2);

c4001::Instance c4001_1(c4001q_1, c4001_uart1, "c4001_1");
c4001::Instance c4001_2(c4001q_2, c4001_uart2, "c4001_2");

constinit static dfr::C4001 *pC4001_1 = nullptr;
constinit static dfr::C4001 *pC4001_2 = nullptr;

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
constexpr uint8_t kMMW_EP = 1;
constexpr uint8_t kMMW_AUX_EP = 2;
constexpr uint16_t kDEV_ID = 0xBAAD;

struct device_ctx_t{
    zb::zb_zcl_basic_names_t basic_attr;
    zb::zb_zcl_status_t status_attr;
    zb::zb_zcl_occupancy_pir_and_ultrasonic_t occupancy;
    zb::zb_zcl_on_off_attrs_client_t on_off_client;
    zb::zb_zcl_c4001_t c4001;
    zb::zb_zcl_c4001_t c4001_aux;
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
/* C4001 attributes                                                   */
/**********************************************************************/
constexpr auto kAttrRMin = &zb::zb_zcl_c4001_t::range_min;
constexpr auto kAttrRMax = &zb::zb_zcl_c4001_t::range_max;
constexpr auto kAttrRTrig = &zb::zb_zcl_c4001_t::range_trig;
constexpr auto kAttrInhibitDuration = &zb::zb_zcl_c4001_t::inhibit_duration;
constexpr auto kAttrSTrig = &zb::zb_zcl_c4001_t::sensitivity_detect;
constexpr auto kAttrSHold = &zb::zb_zcl_c4001_t::sensitivity_hold;


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
constexpr auto kAttrDetectToClearDelay = &zb::zb_zcl_occupancy_ultrasonic_t::UltrasonicOccupiedToUnoccupiedDelay;
constexpr auto kAttrClearToDetectDelay = &zb::zb_zcl_occupancy_ultrasonic_t::UltrasonicUnoccupiedToOccupiedDelay;
//constexpr auto kAttrDetectToClearDelay = &zb::zb_zcl_c4001_t::clear_delay; //&zb::zb_zcl_occupancy_ultrasonic_t::UltrasonicOccupiedToUnoccupiedDelay;
//constexpr auto kAttrClearToDetectDelay = &zb::zb_zcl_c4001_t::detect_delay;//&zb::zb_zcl_occupancy_ultrasonic_t::UltrasonicUnoccupiedToOccupiedDelay;

constexpr auto kCmdOn = &zb::zb_zcl_on_off_attrs_client_t::on;
constexpr auto kCmdOff = &zb::zb_zcl_on_off_attrs_client_t::off;

template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_restart();
template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_save_config();
template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_reset_config();

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
    .c4001{
	.cmd_restart = {.cb = on_cmd_restart<c4001_1>},
	.cmd_save_config = {.cb = on_cmd_save_config<c4001_1>},
	.cmd_reset_config = {.cb = on_cmd_reset_config<c4001_1>}
    },
    .c4001_aux{
	.cmd_restart = {.cb = on_cmd_restart<c4001_2>},
	.cmd_save_config = {.cb = on_cmd_save_config<c4001_2>},
	.cmd_reset_config = {.cb = on_cmd_reset_config<c4001_2>}
    }
};

constinit static auto zb_ctx = zb::make_device(
	zb::make_ep_args<{.ep=kMMW_EP, .dev_id=kDEV_ID, .dev_ver=1}>(
	    dev_ctx.basic_attr
	    , dev_ctx.status_attr
	    , dev_ctx.occupancy
	    , dev_ctx.c4001
	    , dev_ctx.humidity
	    , dev_ctx.temperature
	    , dev_ctx.co2
	    , dev_ctx.airq
	    , dev_ctx.on_off_client
	    ),
	zb::make_ep_args<{.ep=kMMW_AUX_EP, .dev_id=kDEV_ID, .dev_ver=1}>(
	    dev_ctx.c4001_aux
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

/* Get the GPIO spec directly from the node */
/* Note: We look for the property "gpios" inside the node */
static const struct gpio_dt_spec presence = GPIO_DT_SPEC_GET(SENSOR_NODE, gpios);
static const struct gpio_dt_spec presence2 = GPIO_DT_SPEC_GET(SENSOR_NODE2, gpios);

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

template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_restart()
{
    printk("c4001::restart\r\n");
    i.restart();
    return {};
}

template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_save_config()
{
    printk("c4001::save_config\r\n");
    i.save_config();
    return {};
}

template<c4001::Instance &i>
zb::CmdHandlingResult on_cmd_reset_config()
{
    printk("c4001::reset_config\r\n");
    i.reset_config();
    return {};
}

void on_set_detect_to_clear_delay(uint16_t OtoU)
{
    c4001_1.set_clear_delay(OtoU);
    c4001_2.set_clear_delay(OtoU);
}

void on_set_clear_to_detect_delay(uint16_t UtoO)
{
    c4001_1.set_detect_delay(UtoO);
    c4001_2.set_detect_delay(UtoO);
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

template<c4001::Instance &i, auto &zb>
void zb_c4001_update(uint8_t e)
{
    using namespace c4001; 
    cfg_id_t id = (cfg_id_t)e;
    auto *pC4001 = i.sensor();
    if (id & cfg_id_t::Range)
    {
	zb.template attr<kAttrRMin>() = pC4001->GetRangeFrom();
	zb.template attr<kAttrRMax>() = pC4001->GetRangeTo();
    }
    if (id & cfg_id_t::RangeTrig)
    {
	zb.template attr<kAttrRTrig>() = pC4001->GetTriggerDistance();
    }
    if (id & cfg_id_t::Delay)
    {
	zb_ep.attr<kAttrClearToDetectDelay>() = pC4001->GetDetectLatency();
	zb_ep.attr<kAttrDetectToClearDelay>() = pC4001->GetClearLatency();
    }
    if (id & cfg_id_t::InhibitDuration)
    {
	zb.template attr<kAttrInhibitDuration>() = pC4001->GetInhibitDuration();
    }
    if (id & cfg_id_t::Sensitivity)
    {
	zb.template attr<kAttrSTrig>() = pC4001->GetSensitivityTrig();
	zb.template attr<kAttrSHold>() = pC4001->GetSensitivityHold();
    }
}

template<c4001::Instance &i, auto &zb>
void zb_c4001_error(uint8_t e)
{
    using namespace c4001;
    //generally: set zb attributes to current values
    switch(err_t(e))
    {
	case err_t::Range:
	    zb_c4001_update<i, zb>((uint8_t)cfg_id_t::Range);
	    break;
	case err_t::RangeTrig:
	    zb_c4001_update<i, zb>((uint8_t)cfg_id_t::RangeTrig);
	    break;
	case err_t::Delay:
	    zb_c4001_update<i, zb>((uint8_t)cfg_id_t::Delay);
	    break;
	case err_t::InhibitDuration:
	    zb_c4001_update<i, zb>((uint8_t)cfg_id_t::InhibitDuration);
	    break;
	case err_t::Sensitivity:
	    zb_c4001_update<i, zb>((uint8_t)cfg_id_t::Sensitivity);
	    break;
	default:
	break;
    }
    zb_ep.attr<kAttrStatus1>() = e;
}

template<c4001::Instance &i, auto &zb>
void on_c4001_error(c4001::err_t e)
{
    if (g_ZigbeeReady)
	zb_schedule_app_callback(&zb_c4001_error<i, zb>, (uint8_t)e);
}

template<c4001::Instance &i, auto &zb>
void on_c4001_upd(c4001::cfg_id_t id)
{
    if (g_ZigbeeReady)
	zb_schedule_app_callback(&zb_c4001_update<i, zb>, (uint8_t)id);
}

template<c4001::Instance &i>
void on_uto_delay_changed(uint16_t d)
{
    i.set_detect_delay(d);
}

template<c4001::Instance &i>
void on_otu_delay_changed(uint16_t d)
{
    i.set_clear_delay(d);
}


int configure_c4001_out_pin();

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
	zb_ep.attr<kAttrTVOC>() = float(v.val1) / 1000'000.f;
	sensor_channel_get(eco2sensor, (sensor_channel)SENSOR_CHAN_ENS160_AQI, &v);
	zb_ep.attr<kAttrAQI>() = (zb::zb_zcl_air_q_t::AQI)v.val1;
    }
    return true;
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
	    k_sleep(K_MSEC(2100));
	    sys_reboot(SYS_REBOOT_COLD);
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

int main(void)
{
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

    pC4001_1 = c4001_1.setup(&on_c4001_error<c4001_1, zb_ep>, &on_c4001_upd<c4001_1, zb_ep>);
    if (pC4001_1)
    {
	dev_ctx.c4001.range_min = pC4001_1->GetRangeFrom();
	dev_ctx.c4001.range_max = pC4001_1->GetRangeTo();
	dev_ctx.c4001.range_trig = pC4001_1->GetTriggerDistance();
	dev_ctx.c4001.inhibit_duration = pC4001_1->GetInhibitDuration();
	dev_ctx.c4001.sensitivity_detect = pC4001_1->GetSensitivityTrig();
	dev_ctx.c4001.sensitivity_hold = pC4001_1->GetSensitivityHold();
	dev_ctx.c4001.sw_ver = pC4001_1->GetSWVer().m_Version;
	dev_ctx.c4001.hw_ver = pC4001_1->GetHWVer().m_Version;
    }else
    {
	printk("C4001 not found\r\n");
	int val = 1;
	while(true)
	{
	    gpio_pin_set_dt(&led0, val);
	    k_msleep(1000);
	    val ^= 1;
	    printk("C4001 not found; led: %d\r\n", val);
	}
	return 0;
    }

    pC4001_2 = c4001_2.setup(&on_c4001_error<c4001_2, zb_ep_aux>, &on_c4001_upd<c4001_2, zb_ep_aux>);
    if (pC4001_2)
    {
	dev_ctx.c4001_aux.range_min = pC4001_2->GetRangeFrom();
	dev_ctx.c4001_aux.range_max = pC4001_2->GetRangeTo();
	dev_ctx.c4001_aux.range_trig = pC4001_2->GetTriggerDistance();
	dev_ctx.c4001_aux.inhibit_duration = pC4001_2->GetInhibitDuration();
	dev_ctx.c4001_aux.sensitivity_detect = pC4001_2->GetSensitivityTrig();
	dev_ctx.c4001_aux.sensitivity_hold = pC4001_2->GetSensitivityHold();
	dev_ctx.c4001_aux.sw_ver = pC4001_2->GetSWVer().m_Version;
	dev_ctx.c4001_aux.hw_ver = pC4001_2->GetHWVer().m_Version;
    }else
    {
	printk("C4001(2) not found\r\n");
	int val = 1;
	while(true)
	{
	    gpio_pin_set_dt(&led0, val);
	    k_msleep(500);
	    val ^= 1;
	    printk("C4001(2) not found; led: %d\r\n", val);
	}
	return 0;
    }

    dev_ctx.occupancy.occupancy = false;

    /* Register callback for handling ZCL commands. */
    auto dev_cb = zb::tpl_device_cb<
	zb::dev_cb_handlers_desc{ .error_handler = on_dev_cb_error }
	, zb::handle_set_for<kAttrDetectToClearDelay, on_set_detect_to_clear_delay>(zb_ep)
	, zb::handle_set_for<kAttrClearToDetectDelay, on_set_clear_to_detect_delay>(zb_ep)
	, zb::handle_set_for<kAttrRMin,               method_fwd<c4001_1, &c4001::Instance::set_range_from>>(zb_ep)
	, zb::handle_set_for<kAttrRMax,               method_fwd<c4001_1, &c4001::Instance::set_range_to>>(zb_ep)
	, zb::handle_set_for<kAttrRTrig,              method_fwd<c4001_1, &c4001::Instance::set_range_trig>>(zb_ep)
	, zb::handle_set_for<kAttrInhibitDuration,    method_fwd<c4001_1, &c4001::Instance::set_inhibit_duration>>(zb_ep)
	, zb::handle_set_for<kAttrSTrig,              method_fwd<c4001_1, &c4001::Instance::set_detect_sensitivity>>(zb_ep)
	, zb::handle_set_for<kAttrSHold,              method_fwd<c4001_1, &c4001::Instance::set_hold_sensitivity>>(zb_ep)

	, zb::handle_set_for<kAttrRMin,               method_fwd<c4001_2, &c4001::Instance::set_range_from>>(zb_ep_aux)
	, zb::handle_set_for<kAttrRMax,               method_fwd<c4001_2, &c4001::Instance::set_range_to>>(zb_ep_aux)
	, zb::handle_set_for<kAttrRTrig,              method_fwd<c4001_2, &c4001::Instance::set_range_trig>>(zb_ep_aux)
	, zb::handle_set_for<kAttrInhibitDuration,    method_fwd<c4001_2, &c4001::Instance::set_inhibit_duration>>(zb_ep_aux)
	, zb::handle_set_for<kAttrSTrig,              method_fwd<c4001_2, &c4001::Instance::set_detect_sensitivity>>(zb_ep_aux)
	, zb::handle_set_for<kAttrSHold,              method_fwd<c4001_2, &c4001::Instance::set_hold_sensitivity>>(zb_ep_aux)
    >;

    ZB_ZCL_REGISTER_DEVICE_CB(dev_cb);

    /* Register device context (endpoints). */
    ZB_AF_REGISTER_DEVICE_CTX(zb_ctx);

    if (int err = configure_c4001_out_pin(); err != 0)
    {
	printk("Failed to configure c4001 out pin\r\n");
    }
    {
	int val = gpio_pin_get_dt(&presence);
	printk("Presence pin state: %d\r\n", val);
	dev_ctx.occupancy.occupancy = val;
    }
    zigbee_enable();

    printk("Main: sleep forever\r\n");
    {
	printk("C4001(1); HW=%s\r\n", pC4001_1->GetHWVer().m_Version);
	printk("C4001(1); SW=%s\r\n", pC4001_1->GetSWVer().m_Version);
	printk("C4001(1); Range=%.1f to %.1fm\r\n", (double)pC4001_1->GetRangeFrom(), (double)pC4001_1->GetRangeTo());
	printk("C4001(1); Latency; to detect=%.1fs; to clear=%.1fs\r\n", (double)pC4001_1->GetDetectLatency(), (double)pC4001_1->GetClearLatency());
	printk("C4001(1); Trig Range=%.1fm\r\n", (double)pC4001_1->GetTriggerDistance());
	printk("C4001(1); Sensitivity Detect=%d; Hold=%d;\r\n", pC4001_1->GetSensitivityTrig(), pC4001_1->GetSensitivityHold());
	printk("C4001(1); Inhibut Duration=%.1fs\r\n", (double)pC4001_1->GetInhibitDuration());
    }
    {
	printk("C4001(2); HW=%s\r\n", pC4001_2->GetHWVer().m_Version);
	printk("C4001(2); SW=%s\r\n", pC4001_2->GetSWVer().m_Version);
	printk("C4001(2); Range=%.1f to %.1fm\r\n", (double)pC4001_2->GetRangeFrom(), (double)pC4001_2->GetRangeTo());
	printk("C4001(2); Latency; to detect=%.1fs; to clear=%.1fs\r\n", (double)pC4001_2->GetDetectLatency(), (double)pC4001_2->GetClearLatency());
	printk("C4001(2); Trig Range=%.1fm\r\n", (double)pC4001_2->GetTriggerDistance());
	printk("C4001(2); Sensitivity Detect=%d; Hold=%d;\r\n", pC4001_2->GetSensitivityTrig(), pC4001_2->GetSensitivityHold());
	printk("C4001(2); Inhibut Duration=%.1fs\r\n", (double)pC4001_2->GetInhibitDuration());
    }
    while (1) {
	k_sleep(K_FOREVER);
    }

    return 0;
}

int configure_c4001_out_pin()
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
	gpio_init_callback(&g_on_presence_triggered_cb2, presence_triggered<presence2, g_state_presence2, g_state_presence1>, BIT(presence.pin));
	err = gpio_add_callback_dt(&presence2, &g_on_presence_triggered_cb2);
    }
    return err;
}

