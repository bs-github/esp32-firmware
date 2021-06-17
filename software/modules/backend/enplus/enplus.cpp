/* warp-charger
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
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

#include "enplus.h"
#include "enplus_firmware.h"

#include "bindings/errors.h"

#include "api.h"
#include "event_log.h"
#include "task_scheduler.h"
#include "tools.h"
#include "modules/sse/sse.h"
#include "HardwareSerial.h"

extern EventLog logger;

extern TaskScheduler task_scheduler;
extern TF_HalContext hal;
extern AsyncWebServer server;
extern Sse sse;

extern API api;
extern bool firmware_update_allowed;

ENplus::ENplus()
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
        //{"max_current_managed", Config::Uint16(0)},
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
/*
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
*/
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

int ENplus::bs_evse_start_charging(TF_EVSE *evse) {
    charging = true;
    logger.printfln("BS-EVSE start charging");
    return 0;
}

int ENplus::bs_evse_stop_charging(TF_EVSE *evse) {
    charging = false;
    logger.printfln("BS-EVSE stop charging");
    return 0;
}

void ENplus::setup()
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

    /*task_scheduler.scheduleWithFixedDelay("update_evse_managed", [this](){
        update_evse_managed();
    }, 0, 1000);*/

    /*task_scheduler.scheduleWithFixedDelay("update_evse_user_calibration", [this](){
        update_evse_user_calibration();
    }, 0, 10000);*/
}

String ENplus::get_evse_debug_header() {
    return "millis,iec,vehicle,contactor,_error,charge_release,allowed_current,error,lock,t_state_change,uptime,low_level_mode_enabled,led,cp_pwm,adc_pe_cp,adc_pe_pp,voltage_pe_cp,voltage_pe_pp,voltage_pe_cp_max,resistance_pe_cp,resistance_pe_pp,gpio_in,gpio_out,gpio_motor_in,gpio_relay,gpio_motor_error\n";
}

String ENplus::get_evse_debug_line() {
    if(!initialized)
        return "EVSE is not initialized!";

    uint8_t iec61851_state, vehicle_state, contactor_state, contactor_error, charge_release, error_state, lock_state;
    uint16_t allowed_charging_current;
    uint32_t time_since_state_change, uptime;

    int rc = tf_evse_get_state(&evse,
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

    rc = tf_evse_get_low_level_state(&evse,
        &low_level_mode_enabled,
        &led_state,
        &cp_pwm_duty_cycle,
        adc_values,
        voltages,
        resistances,
        gpio);

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

void ENplus::register_urls()
{
    if (!evse_found)
        return;

    api.addState("evse/state", &evse_state, {}, 1000);
    api.addState("evse/hardware_configuration", &evse_hardware_configuration, {}, 1000);
    api.addState("evse/low_level_state", &evse_low_level_state, {}, 1000);
    api.addState("evse/max_charging_current", &evse_max_charging_current, {}, 1000);
    api.addState("evse/auto_start_charging", &evse_auto_start_charging, {}, 1000);

    api.addCommand("evse/auto_start_charging_update", &evse_auto_start_charging_update, {}, [this](){
        is_in_bootloader(tf_evse_set_charging_autostart(&evse, evse_auto_start_charging_update.get("auto_start_charging")->asBool()));
    }, false);

    api.addCommand("evse/current_limit", &evse_current_limit, {}, [this](){
        is_in_bootloader(tf_evse_set_max_charging_current(&evse, evse_current_limit.get("current")->asUint()));
    }, false);

    api.addCommand("evse/stop_charging", &evse_stop_charging, {}, [this](){bs_evse_stop_charging(&evse);}, true);
    api.addCommand("evse/start_charging", &evse_start_charging, {}, [this](){bs_evse_start_charging(&evse);}, true);
/*
    api.addCommand("evse/managed_current_update", &evse_managed_current, {}, [this](){
        is_in_bootloader(tf_evse_set_managed_current(&evse, evse_managed_current.get("current")->asUint()));
    }, true);

    api.addState("evse/managed", &evse_managed, {}, 1000);
    api.addCommand("evse/managed_update", &evse_managed_update, {"password"}, [this](){
        is_in_bootloader(tf_evse_set_managed(&evse, evse_managed_update.get("managed")->asBool(), evse_managed_update.get("password")->asUint()));
    }, true);
*/

    api.addState("evse/user_calibration", &evse_user_calibration, {}, 1000);
    api.addCommand("evse/user_calibration_update", &evse_user_calibration, {}, [this](){
        int16_t resistance_880[14];
        evse_user_calibration.get("resistance_880")->fillArray<int16_t, Config::ConfInt>(resistance_880, sizeof(resistance_880)/sizeof(resistance_880[0]));

        is_in_bootloader(tf_evse_set_user_calibration(&evse,
            0xCA11B4A0,
            evse_user_calibration.get("user_calibration_active")->asBool(),
            evse_user_calibration.get("voltage_diff")->asInt(),
            evse_user_calibration.get("voltage_mul")->asInt(),
            evse_user_calibration.get("voltage_div")->asInt(),
            evse_user_calibration.get("resistance_2700")->asInt(),
            resistance_880
            ));
    }, true);

    server.on("/evse/start_debug", HTTP_GET, [this](AsyncWebServerRequest *request) {
        task_scheduler.scheduleOnce("enable evse debug", [this](){
            sse.pushStateUpdate(this->get_evse_debug_header(), "evse/debug_header");
            debug = true;
        }, 0);
        request->send(200);
    });

    server.on("/evse/stop_debug", HTTP_GET, [this](AsyncWebServerRequest *request){
        task_scheduler.scheduleOnce("enable evse debug", [this](){
            debug = false;
        }, 0);
        request->send(200);
    });
}

//void printHex8(uint8_t *data, uint8_t length) // prints 8-bit data as hex with leading zeroes
//{
//  char tmp[16];
//  for (int i=0; i<length; i++) {
//    sprintf(tmp, "0x%.2X",data[i]);
//    Serial.print(tmp); Serial.print(" ");
//  }
//}

#define PRIV_COMM_BUFFER_MAX_SIZE 1024
byte PrivCommRxBuffer[PRIV_COMM_BUFFER_MAX_SIZE] = {'1'};

#define PRIVCOMM_MAGIC      0
#define PRIVCOMM_VERSION    1
#define PRIVCOMM_ADDR       2
#define PRIVCOMM_CMD        3
#define PRIVCOMM_SEQ        4
#define PRIVCOMM_LEN        5
#define PRIVCOMM_PAYLOAD    6
#define PRIVCOMM_CRC        7

String ENplus::get_hex_PrivComm_line(uint8_t *data, uint8_t len) {
    char line[250] = {0};

    for(uint32_t i = 0; i < len; i++) {
        sprintf(line, "0x%.2X", data[i]); 
    }
    return String(line);
}

void ENplus::loop()
{
    static uint32_t last_check = 0;
    static uint32_t last_debug = 0;
    static uint32_t last_pcomm = 0;
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint16_t crc;
    static bool cmd_to_process = false;
    static byte PrivCommRxState = PRIVCOMM_MAGIC;
    static int PrivCommRxBufferPointer = 0;
    byte rxByte;

    if(evse_found && !initialized && deadline_elapsed(last_check + 10000)) {
        last_check = millis();
        if(!is_in_bootloader(TF_E_TIMEOUT))
            setup_evse();
    }

    if(debug && deadline_elapsed(last_debug + 50)) {
        last_debug = millis();
        sse.pushStateUpdate(this->get_evse_debug_line(), "evse/debug");
    }

    if( Serial2.available() > 0 && !cmd_to_process) {
        do {
            rxByte = Serial2.read();
            Serial.print(rxByte, HEX);
            Serial.print(" ");
            switch( PrivCommRxState ) {
                // Magic Header (0xFA) Version (0x03) Address (0x0000) CMD (0x??) Seq No. (0x??) Length (0x????) Payload (0-1015) Checksum (crc16)
                case PRIVCOMM_MAGIC:
                    if(rxByte == 0xFA) {
                        PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                        PrivCommRxState = PRIVCOMM_VERSION;
                    } else {
                        logger.printfln("PRIVCOMM ERR: out of sync byte: %.2X", rxByte);
                    }
                    break;
                case PRIVCOMM_VERSION:
                    if(rxByte == 0x03) {
                        PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                        PrivCommRxState = PRIVCOMM_ADDR;
                    } else {
                        logger.printfln("PRIVCOMM ERR: got Rx Packet with wrong Version %.2X.", rxByte);
                        PrivCommRxState = PRIVCOMM_MAGIC;
                    }
                    break;
                case PRIVCOMM_ADDR:
                    if(rxByte == 0x00) {
                        PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                        if(PrivCommRxBufferPointer == 4) { // this was the second byte of the address, move on
                            PrivCommRxState = PRIVCOMM_CMD;
                        }
                    } else {
                        logger.printfln("PRIVCOMM ERR: got Rx Packet with wrong Address %.2X%.2X.", PrivCommRxBuffer[PrivCommRxBufferPointer-1], rxByte);
                        PrivCommRxState = PRIVCOMM_MAGIC;
                    }
                    break;
                case PRIVCOMM_CMD:
                    PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                    PrivCommRxState = PRIVCOMM_SEQ;
                    cmd = rxByte;
                    break;
                case PRIVCOMM_SEQ:
                    PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                    PrivCommRxState = PRIVCOMM_LEN;
                    seq = rxByte;
                    break;
                case PRIVCOMM_LEN:
                    PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                    if(PrivCommRxBufferPointer == 8) { // this was the second byte of the length, move on
                        PrivCommRxState = PRIVCOMM_PAYLOAD;
                        len = (uint16_t)(PrivCommRxBuffer[7] << 8 | PrivCommRxBuffer[6]);
                        //TODO sanity check 0 <= len <= 1015 ?
                    }
                    break;
                case PRIVCOMM_PAYLOAD:
                    PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                    if(PrivCommRxBufferPointer == len + 8) {
                        //final byte of Payload received.
                        //should verify 16-bit CRC is correct to ensure alignment; need to see printed results
                        //msgCRC = Get_CRC16_Check_Sum( (unsigned char *)PrivCommRxBuffer, 10, CRC_INIT );

                        PrivCommRxState = PRIVCOMM_CRC;
                    }
                    break;
                case PRIVCOMM_CRC:
                    PrivCommRxBuffer[PrivCommRxBufferPointer++] = rxByte;
                    if(PrivCommRxBufferPointer == len + 10) {
                        crc = (uint16_t)(PrivCommRxBuffer[len + 9] << 8 | PrivCommRxBuffer[len + 8]);
                        logger.printfln("\r\nPRIVCOMM: Rx(cmd_%.2X seq:%d len:%d crc:%d)", cmd, seq, len, crc);
                        // print header+payload as hex and ascii?
                        //logger.printfln("PRIVCOMM: Rx(cmd_%.2X seq:%d len:%d crc:%d payload: %s) : ", cmd, seq, len, crc, get_hex_PrivComm_line(PrivCommRxBuffer, len));
                        //logger.printfln("PRIVCOMM: Rx(cmd_%.2X seq:%d len:%d crc:%d payload: %s) : ", cmd, seq, len, crc, get_hex_PrivComm_line(PrivCommRxBuffer[8], len));
                        //logger.printfln("PRIVCOMM: Rx(cmd_%.2X seq:%d len:%d crc:%d payload: %s) : ", cmd, seq, len, crc, get_hex_PrivComm_line(PrivCommRxBuffer, len+10));
                        PrivCommRxState = PRIVCOMM_MAGIC;
                        PrivCommRxBufferPointer=0;
                        // check CRC
                        // PROCESS it
                        cmd_to_process = true;
                    }
                    break;
            }//switch read packet
        } while( Serial2.available() > 0 );
    }

    if(cmd_to_process) {
        switch( cmd ) {
            case 0x04: // time request / ESP32-GD32 communication heartbeat
                // && PrivCommRxBuffer[9] == 0x01
                // && PrivCommRxBuffer[10] == 0x00
                // && PrivCommRxBuffer[11] == 0x00
                // && PrivCommRxBuffer[12] == 0x00
                // && PrivCommRxBuffer[13] == 0x00
                // && PrivCommRxBuffer[14] == 0x00
                //sendTime(1);
                logger.printfln("PRIVCOMM: Tx(cmd_%.2X seq:%d) I should have answered.", cmd, seq);
                break;
            default:
                logger.printfln("PRIVCOMM: (cmd_%.2X seq:%d) I don't know what to do about it.", cmd, seq);
                break;
        }//switch process cmd
        cmd_to_process = false;
    }
}

void ENplus::setup_evse()
{
    char uid[7] = {0};
//    if (!find_uid_by_did(&hal, TF_EVSE_DEVICE_IDENTIFIER, uid)) {
//        logger.printfln("No EVSE bricklet found. Disabling EVSE support.");
//        return;
//    }
    
    Serial2.begin(115200, SERIAL_8N1, 26, 27); // PrivComm to EVSE GD32 Chip
    Serial2.setTimeout(90);
    logger.printfln("Set up PrivComm: 115200, SERIAL_8N1, RX 26, TX 27, timeout 90ms");

    evse_found = true;

//    int result = ensure_matching_firmware(&hal, uid, "EVSE", "EVSE", evse_firmware_version, / *evse_bricklet_firmware_bin, evse_bricklet_firmware_bin_len,* / &logger);
//    if(result != 0) {
//        return;
//    }

    int result = tf_evse_create(&evse, uid, &hal);
//    if(result != TF_E_OK) {
//        logger.printfln("Failed to initialize EVSE bricklet. Disabling EVSE support.");
//        return;
//    }

    uint8_t jumper_configuration;
    bool has_lock_switch;

//    result = tf_evse_get_hardware_configuration(&evse, &jumper_configuration, &has_lock_switch);

//    if (result != TF_E_OK) {
//        if(!is_in_bootloader(result)) {
//            logger.printfln("EVSE hardware config query failed (rc %d). Disabling EVSE support.", result);
//        }
//        return;
//    } else {
        evse_hardware_configuration.get("jumper_configuration")->updateUint(jumper_configuration);
        evse_hardware_configuration.get("has_lock_switch")->updateBool(has_lock_switch);
//    }

    logger.printfln("Ignoring all the errors initializing the evse. Continue.");
    initialized = true;
}

void ENplus::update_evse_low_level_state() {
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

void ENplus::update_evse_state() {
    if(!initialized)
        return;
    uint8_t iec61851_state, vehicle_state, contactor_state, contactor_error, charge_release, error_state, lock_state;
    uint16_t allowed_charging_current;
    uint32_t time_since_state_change, uptime;

//    int rc = tf_evse_get_state(&evse,
//        &iec61851_state,
//        &vehicle_state,
//        &contactor_state,
//        &contactor_error,
//        &charge_release,
//        &allowed_charging_current,
//        &error_state,
//        &lock_state,
//        &time_since_state_change,
//        &uptime);
//
//    if(rc != TF_E_OK) {
//        is_in_bootloader(rc);
//        return;
//    }

        iec61851_state = charging ? 2 : 1; // 1 verbunden 2 laedt
        vehicle_state = charging ? 2 : 1; // 1 verbunden 2 leadt
        contactor_state = 2;
        contactor_error = 0;
        charge_release = 1; // manuell 0 automatisch
        allowed_charging_current = 8000;
        error_state = 0;
        lock_state = 0;
        time_since_state_change = 123456;
        uptime = millis();

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

    if (contactor_error_changed) {
        if (contactor_error != 0) {
            logger.printfln("EVSE: Contactor error %d", contactor_error);
        } else {
            logger.printfln("EVSE: Contactor error cleared");
        }
    }

    if (error_state_changed) {
        if (error_state != 0) {
            logger.printfln("EVSE: Error state %d", error_state);
        } else {
            logger.printfln("EVSE: Error state cleared");
        }
    }
}

void ENplus::update_evse_max_charging_current() {
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
    //evse_max_charging_current.get("max_current_managed")->updateUint(managed);
}

void ENplus::update_evse_auto_start_charging() {
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

void ENplus::update_evse_managed() {
    if(!initialized)
        return;
    bool managed;

    int rc = tf_evse_get_managed(&evse,
        &managed);

    if(rc != TF_E_OK) {
        is_in_bootloader(rc);
        return;
    }

    evse_managed.get("managed")->updateBool(managed);
}

void ENplus::update_evse_user_calibration() {
    if(!initialized)
        return;

    bool user_calibration_active;
    int16_t voltage_diff, voltage_mul, voltage_div, resistance_2700, resistance_880[14];

    int rc = tf_evse_get_user_calibration(&evse,
        &user_calibration_active,
        &voltage_diff,
        &voltage_mul,
        &voltage_div,
        &resistance_2700,
        resistance_880);

    if(rc != TF_E_OK) {
        is_in_bootloader(rc);
        return;
    }

    evse_user_calibration.get("user_calibration_active")->updateBool(user_calibration_active);
    evse_user_calibration.get("voltage_diff")->updateInt(voltage_diff);
    evse_user_calibration.get("voltage_mul")->updateInt(voltage_mul);
    evse_user_calibration.get("voltage_div")->updateInt(voltage_div);
    evse_user_calibration.get("resistance_2700")->updateInt(resistance_2700);

    for(int i = 0; i < sizeof(resistance_880)/sizeof(resistance_880[0]); ++i)
        evse_user_calibration.get("resistance_880")->get(i)->updateInt(resistance_880[i]);
}

bool ENplus::is_in_bootloader(int rc) {
    return false;

    if(rc != TF_E_TIMEOUT && rc != TF_E_NOT_SUPPORTED)
        return false;

    uint8_t mode;
    int bootloader_rc = tf_evse_get_bootloader_mode(&evse, &mode);
    if(bootloader_rc != TF_E_OK) {
        return false;
    }

    if(mode != TF_EVSE_BOOTLOADER_MODE_FIRMWARE) {
        initialized = false;
    }

    return mode != TF_EVSE_BOOTLOADER_MODE_FIRMWARE;
}
