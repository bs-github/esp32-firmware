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
#include "ntp.h"

#include "task_scheduler.h"

#include "modules.h"

#include <time.h>
#include <sntp.h>
#include <lwip/inet.h>
#include <esp_netif.h>

#include "timezone_translation.h"
#include "build_timestamp.h"

extern TaskScheduler task_scheduler;
extern API api;

#if MODULE_RTC_AVAILABLE()
extern Rtc rtc;
#endif

static Config *ntp_state;
static bool first = true;

static void ntp_sync_cb(struct timeval *t)
{
    if (first) {
        first = false;
        auto now = millis();
        auto secs = now / 1000;
        auto ms = now % 1000;
        logger.printfln("NTP synchronized at %lu,%03lu!", secs, ms);

        task_scheduler.scheduleWithFixedDelay([](){
            ntp_state->get("time")->updateUint(timestamp_minutes());
        }, 0, 1000);
    }

    task_scheduler.scheduleOnce([]() {
        ntp_state->get("synced")->updateBool(true);
    }, 0);

#if MODULE_RTC_AVAILABLE()
    if (api.hasFeature("rtc"))
    {
        task_scheduler.scheduleOnce([]() {
            timeval time;
            gettimeofday(&time, nullptr);
            rtc.set_time(time.tv_sec + time.tv_usec / 1000000);
        }, 0);
    }
#endif
}

//Because there is the risk of a race condition with the rtc module we have to replace the sntp_sync_time function with a threadsave implementation.
extern "C" void sntp_sync_time(struct timeval *tv)
{
    if (sntp_get_sync_mode() == SNTP_SYNC_MODE_IMMED)
    {
        {
            std::lock_guard<std::mutex> lock{ntp.mtx};
            settimeofday(tv, NULL);
            ntp.mtx_count++;
        }
        sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
        ntp.set_last_sync();
    }
    else
        logger.printfln("This sync mode is not supported.");
    ntp_sync_cb(tv);
}

void NTP::pre_setup()
{
    config = ConfigRoot{Config::Object({
        {"enable", Config::Bool(true)},
        {"use_dhcp", Config::Bool(true)},
        {"timezone", Config::Str("Europe/Berlin", 0, 32)}, // Longest is America/Argentina/ComodRivadavia = 32 chars
        {"server", Config::Str("ptbtime1.ptb.de", 0, 64)}, // We've applied for a vendor zone @ pool.ntp.org, however this seems to take quite a while. Use the ptb servers for now.
        {"server2", Config::Str("ptbtime2.ptb.de", 0, 64)},
    }), [](Config &conf) -> String {
        if (lookup_timezone(conf.get("timezone")->asCStr()) == nullptr)
            return "Can't update config: Failed to look up timezone.";
        return "";
    }};

    state = Config::Object({
        {"synced", Config::Bool(false)},
        {"time", Config::Uint32(0)} // unix timestamp in minutes
    });
}

void NTP::setup()
{
    initialized = true;

    api.restorePersistentConfig("ntp/config", &config);

    // sntp_set_time_sync_notification_cb(ntp_sync_cb);             since we use our own sntp_sync_time function we do not need to register the cb function.

    bool dhcp = config.get("use_dhcp")->asBool();
    sntp_servermode_dhcp(dhcp ? 1 : 0);

    esp_netif_init();
    if (sntp_enabled()) {
        sntp_stop();
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    if (config.get("server")->asString() != "")
        sntp_setservername(dhcp ? 1 : 0, config.get("server")->asCStr());
    if (config.get("server2")->asString() != "")
        sntp_setservername(dhcp ? 2 : 1, config.get("server2")->asCStr());

    const char *tzstring = lookup_timezone(config.get("timezone")->asCStr());

    if (tzstring == nullptr) {
        logger.printfln("Failed to look up timezone information for %s. Will not set timezone", config.get("timezone")->asCStr());
        return;
    }
    setenv("TZ", tzstring, 1);
    tzset();
    logger.printfln("Set timezone to %s", config.get("timezone")->asCStr());

    ntp_state = &state;

    if (config.get("enable")->asBool())
         sntp_init();
}

void NTP::set_last_sync()
{
    gettimeofday(&last_sync, NULL);
    ntp_state->get("synced")->updateBool(true);
}

void NTP::register_urls()
{
    api.addPersistentConfig("ntp/config", &config, {}, 1000);
    api.addState("ntp/state", &state, {}, 1000);

    task_scheduler.scheduleWithFixedDelay([this]() {
        struct timeval time;
        gettimeofday(&time, NULL);
        if (time.tv_sec - this->last_sync.tv_sec >= 30 || time.tv_sec < BUILD_TIMESTAMP)
            ntp_state->get("synced")->updateBool(false);
    }, 0, 1000 * 60 * 60 * 25);
}

void NTP::loop()
{
}
