/* esp32-firmware
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

#pragma once

#include "bindings/bricklet_evse_v2.h"

#include "config.h"
#include "device_module.h"
#include "evse_v2_firmware.h"

class EVSEV2 : public DeviceModule<TF_EVSEV2,
                                 evse_v2_bricklet_firmware_bin,
                                 evse_v2_bricklet_firmware_bin_len,
                                 tf_evse_v2_create,
                                 tf_evse_v2_get_bootloader_mode,
                                 tf_evse_v2_reset>  {
public:
    EVSEV2();
    void setup();
    void register_urls();
    void loop();

    ConfigRoot evse_energy_meter_state;

    // Called in evse_v2_meter setup
    void update_all_data();

private:
    void setup_evse();
    bool flash_firmware();
    bool flash_plugin(int regular_plugin_upto);
    bool wait_for_bootloader_mode(int mode);
    String get_evse_debug_header();
    String get_evse_debug_line();
    void set_managed_current(uint16_t current);
    String get_evse_monitor_header();
    String get_evse_monitor_line();

    bool debug = false;

    ConfigRoot evse_state;
    ConfigRoot evse_hardware_configuration;
    ConfigRoot evse_low_level_state;
    ConfigRoot evse_max_charging_current;
    ConfigRoot evse_auto_start_charging;
    ConfigRoot evse_auto_start_charging_update;
    ConfigRoot evse_current_limit;
    ConfigRoot evse_stop_charging;
    ConfigRoot evse_start_charging;
    ConfigRoot evse_energy_meter_values;

    ConfigRoot evse_dc_fault_current_state;
    ConfigRoot evse_reset_dc_fault_current;
    ConfigRoot evse_gpio_configuration;
    ConfigRoot evse_button_configuration;
    ConfigRoot evse_button_configuration_update;
    ConfigRoot evse_managed;
    ConfigRoot evse_managed_update;
    ConfigRoot evse_managed_current;
    ConfigRoot evse_button_state;
    ConfigRoot evse_control_pilot_configuration;
    ConfigRoot evse_control_pilot_configuration_update;

    uint32_t last_current_update = 0;
    bool shutdown_logged = false;
};
