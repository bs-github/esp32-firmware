/* warp-charger
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 * Copyright (C)      2021 Birger Schmidt <bs-warp@netgaroo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "sonoff.h"
#include "charge_management_protocol.h"

#include "bindings/errors.h"

#include "api.h"
#include "event_log.h"
#include "task_scheduler.h"
#include "tools.h"
#include "web_server.h"
#include "modules/ws/ws.h"

extern EventLog logger;

extern TaskScheduler task_scheduler;
extern TF_HalContext hal;
extern WebServer server;
extern WS ws;

extern API api;
extern bool firmware_update_allowed;

#define RELAY1 27
#define RELAY2 14
#define SWITCH1 32
#define SWITCH2 33

sonoff::sonoff()
{
    evse_state = Config::Object({
        {"iec61851_state", Config::Uint8(0)},
        {"vehicle_state", Config::Uint8(0)},
        {"contactor_state", Config::Uint8(0)},
        {"contactor_error", Config::Uint8(0)},
        {"charge_release", Config::Uint8(0)},
        {"allowed_charging_current", Config::Uint16(0)},
        {"error_state", Config::Uint8(0)},
        {"lock_state", Config::Uint8(0)},
        {"time_since_state_change", Config::Uint32(0)},
        {"uptime", Config::Uint32(0)}
    });

    evse_hardware_configuration = Config::Object({
        {"jumper_configuration", Config::Uint8(0)},
        {"has_lock_switch", Config::Bool(false)}
    });

    evse_low_level_state = Config::Object ({
        {"low_level_mode_enabled", Config::Bool(false)},
        {"led_state", Config::Uint8(0)},
        {"cp_pwm_duty_cycle", Config::Uint16(0)},
        {"adc_values", Config::Array({
                Config::Uint16(0),
                Config::Uint16(0),
            }, Config::Uint16(0), 2, 2, Config::type_id<Config::ConfUint>())
        },
        {"voltages", Config::Array({
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
            }, Config::Int16(0), 3, 3, Config::type_id<Config::ConfInt>())
        },
        {"resistances", Config::Array({
                Config::Uint32(0),
                Config::Uint32(0),
            }, Config::Uint32(0), 2, 2, Config::type_id<Config::ConfUint>())
        },
        {"gpio", Config::Array({Config::Bool(false),Config::Bool(false),Config::Bool(false),Config::Bool(false), Config::Bool(false)}, Config::Bool(false), 5, 5, Config::type_id<Config::ConfBool>())}
    });

    evse_max_charging_current = Config::Object ({
        {"max_current_configured", Config::Uint16(0)},
        {"max_current_incoming_cable", Config::Uint16(0)},
        {"max_current_outgoing_cable", Config::Uint16(0)},
        {"max_current_managed", Config::Uint16(0)},
    });

    evse_auto_start_charging = Config::Object({
        {"auto_start_charging", Config::Bool(true)}
    });

    evse_auto_start_charging_update = Config::Object({
        {"auto_start_charging", Config::Bool(true)}
    });
    evse_current_limit = Config::Object({
        {"current", Config::Uint(32000, 6000, 32000)}
    });

    evse_stop_charging = Config::Null();
    evse_start_charging = Config::Null();

    evse_managed_current = Config::Object ({
        {"current", Config::Uint16(0)}
    });

    evse_managed = Config::Object({
        {"managed", Config::Bool(false)}
    });

    evse_managed_update = Config::Object({
        {"managed", Config::Bool(false)},
        {"password", Config::Uint32(0)}
    });

    evse_user_calibration = Config::Object({
        {"user_calibration_active", Config::Bool(false)},
        {"voltage_diff", Config::Int16(0)},
        {"voltage_mul", Config::Int16(0)},
        {"voltage_div", Config::Int16(0)},
        {"resistance_2700", Config::Int16(0)},
        {"resistance_880", Config::Array({
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
                Config::Int16(0),
            }, Config::Int16(0), 14, 14, Config::type_id<Config::ConfInt>())},
    });
}

int sonoff::bs_evse_start_charging(TF_EVSE *evse) {
    charging = true;
    logger.printfln("EVSE start charging");
    return 0;
}

int sonoff::bs_evse_stop_charging(TF_EVSE *evse) {
    charging = false;
    logger.printfln("EVSE stop charging");
    return 0;
}

int sonoff::bs_evse_get_state(TF_EVSE *evse, uint8_t *ret_iec61851_state, uint8_t *ret_vehicle_state, uint8_t *ret_contactor_state, uint8_t *ret_contactor_error, uint8_t *ret_charge_release, uint16_t *ret_allowed_charging_current, uint8_t *ret_error_state, uint8_t *ret_lock_state, uint32_t *ret_time_since_state_change, uint32_t *ret_uptime) {
//    if(tf_hal_get_common(evse->tfp->hal)->locked) {
//        return TF_E_LOCKED;
//    }
//
//    bool response_expected = true;
//    tf_tfp_prepare_send(evse->tfp, TF_EVSE_FUNCTION_GET_STATE, 0, 17, response_expected);
//
//    uint32_t deadline = tf_hal_current_time_us(evse->tfp->hal) + tf_hal_get_common(evse->tfp->hal)->timeout;
//
//    uint8_t error_code = 0;
//    int result = tf_tfp_transmit_packet(evse->tfp, response_expected, deadline, &error_code);
//    if(result < 0)
//        return result;
//
//    if (result & TF_TICK_TIMEOUT) {
//        //return -result;
//        return TF_E_TIMEOUT;
//    }
//
//    if (result & TF_TICK_PACKET_RECEIVED && error_code == 0) {
//        if (ret_iec61851_state != NULL) { *ret_iec61851_state = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_vehicle_state != NULL) { *ret_vehicle_state = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_contactor_state != NULL) { *ret_contactor_state = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_contactor_error != NULL) { *ret_contactor_error = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_charge_release != NULL) { *ret_charge_release = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_allowed_charging_current != NULL) { *ret_allowed_charging_current = tf_packetbuffer_read_uint16_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 2); }
//        if (ret_error_state != NULL) { *ret_error_state = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_lock_state != NULL) { *ret_lock_state = tf_packetbuffer_read_uint8_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 1); }
//        if (ret_time_since_state_change != NULL) { *ret_time_since_state_change = tf_packetbuffer_read_uint32_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 4); }
//        if (ret_uptime != NULL) { *ret_uptime = tf_packetbuffer_read_uint32_t(&evse->tfp->spitfp->recv_buf); } else { tf_packetbuffer_remove(&evse->tfp->spitfp->recv_buf, 4); }
//        tf_tfp_packet_processed(evse->tfp);
//    }
//
//    result = tf_tfp_finish_send(evse->tfp, result, deadline);
//    if(result < 0)
//        return result;
//
    *ret_iec61851_state = charging ? 2 : 1; // 1 verbunden 2 laedt
    *ret_vehicle_state = charging ? 2 : 1; // 1 verbunden 2 leadt
    *ret_contactor_state = 2;
    *ret_contactor_error = 0;
    *ret_charge_release = 1; // manuell 0 automatisch
    *ret_allowed_charging_current = 8000;
    *ret_error_state = 0;
    *ret_lock_state = 0;
    *ret_time_since_state_change = 123456;
    *ret_uptime = millis();
//    return tf_tfp_get_error(error_code);
    return TF_E_OK;
}

int sonoff::bs_evse_set_max_charging_current(TF_EVSE *evse, uint16_t max_current) {
    logger.printfln("EVSE set charging limit to %d Ampere.", uint8_t(max_current/1000));
    evse_max_charging_current.get("max_current_configured")->updateUint(max_current);

    return 0;
}

void sonoff::setup()
{
    setup_evse();

    task_scheduler.scheduleWithFixedDelay("update_evse_state", [this](){
        update_evse_state();
    }, 0, 1000);

    task_scheduler.scheduleWithFixedDelay("update_evse_low_level_state", [this](){
        update_evse_low_level_state();
    }, 0, 1000);

    task_scheduler.scheduleWithFixedDelay("update_evse_max_charging_current", [this](){
        update_evse_max_charging_current();
    }, 0, 1000);

    task_scheduler.scheduleWithFixedDelay("update_evse_auto_start_charging", [this](){
        update_evse_auto_start_charging();
    }, 0, 1000);

    task_scheduler.scheduleWithFixedDelay("update_evse_managed", [this](){
        update_evse_managed();
    }, 0, 1000);

    task_scheduler.scheduleWithFixedDelay("update_evse_user_calibration", [this](){
        update_evse_user_calibration();
    }, 0, 10000);
}

String sonoff::get_evse_debug_header() {
    return "millis,iec,vehicle,contactor,_error,charge_release,allowed_current,error,lock,t_state_change,uptime,low_level_mode_enabled,led,cp_pwm,adc_pe_cp,adc_pe_pp,voltage_pe_cp,voltage_pe_pp,voltage_pe_cp_max,resistance_pe_cp,resistance_pe_pp,gpio_in,gpio_out,gpio_motor_in,gpio_relay,gpio_motor_error\n";
}

String sonoff::get_evse_debug_line() {
    if(!initialized)
        return "EVSE is not initialized!";

    uint8_t iec61851_state, vehicle_state, contactor_state, contactor_error, charge_release, error_state, lock_state;
    uint16_t allowed_charging_current;
    uint32_t time_since_state_change, uptime;

    int rc = bs_evse_get_state(&evse,
        &iec61851_state,
        &vehicle_state,
        &contactor_state,
        &contactor_error,
        &charge_release,
        &allowed_charging_current,
        &error_state,
        &lock_state,
        &time_since_state_change,
        &uptime);

    if(rc != TF_E_OK) {
        return String("evse_get_state failed: rc: ") + String(rc);
    }

    bool low_level_mode_enabled;
    uint8_t led_state;
    uint16_t cp_pwm_duty_cycle;

    uint16_t adc_values[2];
    int16_t voltages[3];
    uint32_t resistances[2];
    bool gpio[5];

//    rc = tf_evse_get_low_level_state(&evse,
//        &low_level_mode_enabled,
//        &led_state,
//        &cp_pwm_duty_cycle,
//        adc_values,
//        voltages,
//        resistances,
//        gpio);

    if(rc != TF_E_OK) {
        return String("evse_get_low_level_state failed: rc: ") + String(rc);
    }

    char line[150] = {0};
    snprintf(line, sizeof(line)/sizeof(line[0]), "%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%c,%u,%u,%u,%u,%d,%d,%d,%u,%u,%c,%c,%c,%c,%c\n",
        millis(),
        iec61851_state,
        vehicle_state,
        contactor_state,
        contactor_error,
        charge_release,
        allowed_charging_current,
        error_state,
        lock_state,
        time_since_state_change,
        uptime,
        low_level_mode_enabled ? '1' : '0',
        led_state,
        cp_pwm_duty_cycle,
        adc_values[0],adc_values[1],
        voltages[0],voltages[1],voltages[2],
        resistances[0],resistances[1],
        gpio[0] ? '1' : '0',gpio[1] ? '1' : '0',gpio[2] ? '1' : '0',gpio[3] ? '1' : '0',gpio[4] ? '1' : '0');

    return String(line);
}

void sonoff::set_managed_current(uint16_t current) {
    is_in_bootloader(tf_evse_set_managed_current(&evse, current));
    this->last_current_update = millis();
    this->shutdown_logged = false;
}

void sonoff::register_urls()
{
    if (!evse_found)
        return;

    api.addState("evse/state", &evse_state, {}, 1000);
    api.addState("evse/hardware_configuration", &evse_hardware_configuration, {}, 1000);
    api.addState("evse/low_level_state", &evse_low_level_state, {}, 1000);
    api.addState("evse/max_charging_current", &evse_max_charging_current, {}, 1000);
    api.addState("evse/auto_start_charging", &evse_auto_start_charging, {}, 1000);
    api.addState("evse/privcomm", &evse_privcomm, {}, 1000);

    //api.addCommand("evse/auto_start_charging_update", &evse_auto_start_charging_update, {}, [this](){
    //    bs_evse_set_charging_autostart(&evse, evse_auto_start_charging_update.get("auto_start_charging")->asBool());
    //}, false);

    api.addCommand("evse/current_limit", &evse_current_limit, {}, [this](){
        bs_evse_set_max_charging_current(&evse, evse_current_limit.get("current")->asUint());
    }, false);

    api.addCommand("evse/stop_charging", &evse_stop_charging, {}, [this](){bs_evse_stop_charging(&evse);}, true);
    api.addCommand("evse/start_charging", &evse_start_charging, {}, [this](){bs_evse_start_charging(&evse);}, true);

    api.addCommand("evse/managed_current_update", &evse_managed_current, {}, [this](){
        this->set_managed_current(evse_managed_current.get("current")->asUint());
    }, true);

    api.addState("evse/managed", &evse_managed, {}, 1000);
    api.addCommand("evse/managed_update", &evse_managed_update, {"password"}, [this](){
        //TODOTODO set managed current as local value, not in the tf_evse
        is_in_bootloader(tf_evse_set_managed(&evse, evse_managed_update.get("managed")->asBool(), evse_managed_update.get("password")->asUint()));
    }, true);

    api.addState("evse/user_calibration", &evse_user_calibration, {}, 1000);
    api.addCommand("evse/user_calibration_update", &evse_user_calibration, {}, [this](){
        int16_t resistance_880[14];
        evse_user_calibration.get("resistance_880")->fillArray<int16_t, Config::ConfInt>(resistance_880, sizeof(resistance_880)/sizeof(resistance_880[0]));

        tf_evse_set_user_calibration(&evse,
            0xCA11B4A0,
            evse_user_calibration.get("user_calibration_active")->asBool(),
            evse_user_calibration.get("voltage_diff")->asInt(),
            evse_user_calibration.get("voltage_mul")->asInt(),
            evse_user_calibration.get("voltage_div")->asInt(),
            evse_user_calibration.get("resistance_2700")->asInt(),
            resistance_880
            );
    }, true);

    server.on("/evse/start_debug", HTTP_GET, [this](WebServerRequest request) {
        task_scheduler.scheduleOnce("enable evse debug", [this](){
            ws.pushStateUpdate(this->get_evse_debug_header(), "evse/debug_header");
            debug = true;
        }, 0);
        request.send(200);
    });

    server.on("/evse/stop_debug", HTTP_GET, [this](WebServerRequest request){
        task_scheduler.scheduleOnce("enable evse debug", [this](){
            debug = false;
        }, 0);
        request.send(200);
    });
}

void sonoff::loop()
{
    static uint32_t last_check = 0;
    static uint32_t last_debug = 0;
    static bool switch1_before;
    static bool switch2_before;

    if(evse_found && !initialized && deadline_elapsed(last_check + 10000)) {
        last_check = millis();
        if(!is_in_bootloader(TF_E_TIMEOUT))
            setup_evse();
    }

    switch1_before = switch1;
    switch2_before = switch2;
    switch1 = digitalRead(SWITCH1);
    switch2 = digitalRead(SWITCH2);

    if(switch1 != switch1_before) {
        digitalWrite(RELAY1, switch1);
        logger.printfln("Der Energieversorger %s das Laden von Elektroautos.", switch1 ? "verbietet" : "erlaubt");
    }
    if(switch2 != switch2_before) {
        digitalWrite(RELAY2, switch2);
        logger.printfln("Schalteingang 2 ist jetzt %sgeschaltet.", switch2 ? "aus" : "ein");
    }

    if(debug && deadline_elapsed(last_debug + 50)) {
        last_debug = millis();
        ws.pushStateUpdate(this->get_evse_debug_line(), "evse/debug");
    }

}

void sonoff::setup_evse()
{
//    Serial2.begin(115200, SERIAL_8N1, 26, 27); // PrivComm to EVSE GD32 Chip
//    Serial2.setTimeout(90);
//    logger.printfln("Set up PrivComm: 115200, SERIAL_8N1, RX 26, TX 27, timeout 90ms");

    //logger.printfln("EN+ GD EVSE found. Enabling EVSE support.");
    evse_found = true;

    pinMode(RELAY1, OUTPUT);
    pinMode(RELAY2, OUTPUT);
    pinMode(SWITCH1, INPUT);
    pinMode(SWITCH2, INPUT);
    digitalWrite(RELAY1, digitalRead(SWITCH1));
    digitalWrite(RELAY2, digitalRead(SWITCH2));

    char uid[7] = {0}; // put SN here?
    int result = tf_evse_create(&evse, uid, &hal);

    uint8_t jumper_configuration;
    bool has_lock_switch;

    evse_hardware_configuration.get("jumper_configuration")->updateUint(jumper_configuration);
    evse_hardware_configuration.get("has_lock_switch")->updateBool(has_lock_switch);

    initialized = true;
}

void sonoff::update_evse_low_level_state() {
    if(!initialized)
        return;

    bool low_level_mode_enabled;
    uint8_t led_state;
    uint16_t cp_pwm_duty_cycle;

    uint16_t adc_values[2];
    int16_t voltages[3];
    uint32_t resistances[2];
    bool gpio[5];

//    int rc = tf_evse_get_low_level_state(&evse,
//        &low_level_mode_enabled,
//        &led_state,
//        &cp_pwm_duty_cycle,
//        adc_values,
//        voltages,
//        resistances,
//        gpio);
//
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }

        low_level_mode_enabled = true;
        led_state = 1;
        cp_pwm_duty_cycle = 100;
        adc_values[0] = 200;
        adc_values[1] = 201;
        voltages[0] = 300;
        voltages[1] = 301;
        voltages[2] = 302;
        resistances[0] = 400;
        resistances[1] = 401;
        gpio[0] = false;
        gpio[1] = false;
        gpio[2] = false;
        gpio[3] = false;
        gpio[4] = false;

    evse_low_level_state.get("low_level_mode_enabled")->updateBool(low_level_mode_enabled);
    evse_low_level_state.get("led_state")->updateUint(led_state);
    evse_low_level_state.get("cp_pwm_duty_cycle")->updateUint(cp_pwm_duty_cycle);

    for(int i = 0; i < sizeof(adc_values)/sizeof(adc_values[0]); ++i)
        evse_low_level_state.get("adc_values")->get(i)->updateUint(adc_values[i]);

    for(int i = 0; i < sizeof(voltages)/sizeof(voltages[0]); ++i)
        evse_low_level_state.get("voltages")->get(i)->updateInt(voltages[i]);

    for(int i = 0; i < sizeof(resistances)/sizeof(resistances[0]); ++i)
        evse_low_level_state.get("resistances")->get(i)->updateUint(resistances[i]);

    for(int i = 0; i < sizeof(gpio)/sizeof(gpio[0]); ++i)
        evse_low_level_state.get("gpio")->get(i)->updateBool(gpio[i]);
}

void sonoff::update_evse_state() {
    if(!initialized)
        return;
    uint8_t iec61851_state, vehicle_state, contactor_state, contactor_error, charge_release, error_state, lock_state;
    uint16_t allowed_charging_current;
    uint32_t time_since_state_change, uptime;

    int rc = bs_evse_get_state(&evse,
        &iec61851_state,
        &vehicle_state,
        &contactor_state,
        &contactor_error,
        &charge_release,
        &allowed_charging_current,
        &error_state,
        &lock_state,
        &time_since_state_change,
        &uptime);

    if(rc != TF_E_OK) {
        is_in_bootloader(rc);
        return;
    }

    //firmware_update_allowed = vehicle_state == 0;
    // fix/revert this before release!!!
    firmware_update_allowed = vehicle_state != 0;

    evse_state.get("iec61851_state")->updateUint(iec61851_state);
    evse_state.get("vehicle_state")->updateUint(vehicle_state);
    evse_state.get("contactor_state")->updateUint(contactor_state);
    bool contactor_error_changed = evse_state.get("contactor_error")->updateUint(contactor_error);
    evse_state.get("charge_release")->updateUint(charge_release);
    evse_state.get("allowed_charging_current")->updateUint(allowed_charging_current);
    bool error_state_changed = evse_state.get("error_state")->updateUint(error_state);
    evse_state.get("lock_state")->updateUint(lock_state);
    evse_state.get("time_since_state_change")->updateUint(time_since_state_change);
    evse_state.get("uptime")->updateUint(uptime);
}

void sonoff::update_evse_max_charging_current() {
    if(!initialized)
        return;
    uint16_t configured, incoming, outgoing, managed;

//    int rc = tf_evse_get_max_charging_current(&evse,
//        &configured,
//        &incoming,
//        &outgoing,
//        &managed);
//
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }

    configured = 7000;
    incoming = 32000;
    outgoing = 16000;
    managed = 15000;

    evse_max_charging_current.get("max_current_configured")->updateUint(configured);
    evse_max_charging_current.get("max_current_incoming_cable")->updateUint(incoming);
    evse_max_charging_current.get("max_current_outgoing_cable")->updateUint(outgoing);
    evse_max_charging_current.get("max_current_managed")->updateUint(managed);
    digitalWrite(RELAY2, charging);
}

void sonoff::update_evse_auto_start_charging() {
    if(!initialized)
        return;
    bool auto_start_charging;

//    int rc = tf_evse_get_charging_autostart(&evse,
//        &auto_start_charging);
//
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }

    auto_start_charging = false;

    evse_auto_start_charging.get("auto_start_charging")->updateBool(auto_start_charging);
}

void sonoff::update_evse_managed() {
    if(!initialized)
        return;
    bool managed;

//    int rc = tf_evse_get_managed(&evse,
//        &managed);
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }

    //evse_managed.get("managed")->updateBool(managed);
    evse_managed.get("managed")->updateBool(true);
}

void sonoff::update_evse_user_calibration() {
    if(!initialized)
        return;

//    bool user_calibration_active;
//    int16_t voltage_diff, voltage_mul, voltage_div, resistance_2700, resistance_880[14];
//
//    int rc = tf_evse_get_user_calibration(&evse,
//        &user_calibration_active,
//        &voltage_diff,
//        &voltage_mul,
//        &voltage_div,
//        &resistance_2700,
//        resistance_880);
//
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }
//
//    evse_user_calibration.get("user_calibration_active")->updateBool(user_calibration_active);
//    evse_user_calibration.get("voltage_diff")->updateInt(voltage_diff);
//    evse_user_calibration.get("voltage_mul")->updateInt(voltage_mul);
//    evse_user_calibration.get("voltage_div")->updateInt(voltage_div);
//    evse_user_calibration.get("resistance_2700")->updateInt(resistance_2700);
//
//    for(int i = 0; i < sizeof(resistance_880)/sizeof(resistance_880[0]); ++i)
//        evse_user_calibration.get("resistance_880")->get(i)->updateInt(resistance_880[i]);
}

bool sonoff::is_in_bootloader(int rc) {
    return false;
}

void sonoff::start_managed_tasks() {
    sock = create_socket(false);

    if(sock < 0)
        return;

    memset(&source_addr, 0, sizeof(source_addr));


    task_scheduler.scheduleWithFixedDelay("evse_managed_receive_task", [this](){
        static uint8_t last_seen_seq_num = 255;
        request_packet recv_buf[2] = {0};

        struct sockaddr_storage temp_addr;
        socklen_t socklen = sizeof(temp_addr);
        int len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&temp_addr, &socklen);

        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                logger.printfln("recvfrom failed: errno %d", errno);
            return;
        }

        if (len != sizeof(request_packet)) {
            logger.printfln("received datagram of wrong size %d", len);
            return;
        }

        request_packet request;
        memcpy(&request, recv_buf, sizeof(request));

        if (request.header.seq_num <= last_seen_seq_num && last_seen_seq_num - request.header.seq_num < 5) {
            logger.printfln("received stale (out of order?) packet. last seen seq_num is %u, received seq_num is %u", last_seen_seq_num, request.header.seq_num);
            return;
        }

        if (request.header.version[0] != _MAJOR_ || request.header.version[1] != _MINOR_ || request.header.version[2] != _PATCH_) {
            logger.printfln("received packet from box with incompatible firmware. Our version is %u.%u.%u, received packet had %u.%u.%u",
                _MAJOR_, _MINOR_, _PATCH_,
                request.header.version[0],
                request.header.version[1],
                request.header.version[2]);
            return;
        }

        last_seen_seq_num = request.header.seq_num;
        source_addr_valid = false;

        source_addr = temp_addr;

        source_addr_valid = true;
        this->set_managed_current(request.allocated_current);
        //logger.printfln("Received request. Allocated current is %u", request.allocated_current);
    }, 100, 100);

    task_scheduler.scheduleWithFixedDelay("evse_managed_send_task", [this](){
        static uint8_t next_seq_num = 0;

        if (!source_addr_valid) {
            //logger.printfln("source addr not valid.");
            return;

        }
        //logger.printfln("Sending response.");

        response_packet response;
        response.header.seq_num = next_seq_num;
        ++next_seq_num;
        response.header.version[0] = _MAJOR_;
        response.header.version[1] = _MINOR_;
        response.header.version[2] = _PATCH_;

        response.iec61851_state = evse_state.get("iec61851_state")->asUint();
        response.vehicle_state = evse_state.get("vehicle_state")->asUint();
        response.error_state = evse_state.get("error_state")->asUint();
        response.uptime = evse_state.get("uptime")->asUint();
        response.allowed_charging_current = evse_state.get("allowed_charging_current")->asUint();
        response.charge_release = evse_state.get("charge_release")->asUint();

        int err = sendto(sock, &response, sizeof(response), 0, (sockaddr *)&source_addr, sizeof(source_addr));
        if (err < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                logger.printfln("sendto failed: errno %d", errno);
            return;
        }
        if (err != sizeof(response)){
            logger.printfln("sendto truncated the response (of size %u bytes) to %d bytes.", sizeof(response), err);
            return;
        }

        //logger.printfln("Sent response.");
    }, 1000, 1000);

    task_scheduler.scheduleWithFixedDelay("evse_managed_current_watchdog", [this]() {
        if (!deadline_elapsed(this->last_current_update + 30000))
            return;
        if(!shutdown_logged)
            logger.printfln("Got no managed current update for more than 30 seconds. Setting managed current to 0");
        shutdown_logged = true;
        is_in_bootloader(tf_evse_set_managed_current(&evse, 0));
    }, 1000, 1000);
}
