/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2012 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_TAG "PowerHAL"
#include <cutils/properties.h>
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/ondemand/boostpulse"
#define SAMPLING_RATE_ONDEMAND "/sys/devices/system/cpu/cpufreq/ondemand/sampling_rate"
#define SAMPLING_RATE_SCREEN_ON "40000"
#define SAMPLING_RATE_SCREEN_OFF "400000"

struct s5pc110_power_module {
    struct power_module base;
    pthread_mutex_t lock;
    int boostpulse_fd;
    int boostpulse_warned;
    char sampling_rate_screen_on[PROPERTY_VALUE_MAX];
    char sampling_rate_screen_off[PROPERTY_VALUE_MAX];
};

static void sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

static int boostpulse_open(struct s5pc110_power_module *s5pc110)
{
    char buf[80];

    pthread_mutex_lock(&s5pc110->lock);

    if (s5pc110->boostpulse_fd < 0) {
        s5pc110->boostpulse_fd = open(BOOSTPULSE_PATH, O_WRONLY);

        if (s5pc110->boostpulse_fd < 0) {
            if (!s5pc110->boostpulse_warned) {
                strerror_r(errno, buf, sizeof(buf));
                ALOGE("Error opening %s: %s\n", BOOSTPULSE_PATH, buf);
                s5pc110->boostpulse_warned = 1;
            }
        }
    }

    pthread_mutex_unlock(&s5pc110->lock);
    return s5pc110->boostpulse_fd;
}

static void s5pc110_power_hint(struct power_module *module, power_hint_t hint,
                            void *data)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;
    char buf[80];
    int len;
    int duration = 1;

    switch (hint) {
    case POWER_HINT_INTERACTION:
    case POWER_HINT_CPU_BOOST:
        if (boostpulse_open(s5pc110) >= 0) {
            if (data != NULL)
                duration = (int) data;

            snprintf(buf, sizeof(buf), "%d", duration);
            len = write(s5pc110->boostpulse_fd, buf, strlen(buf));

            if (len < 0) {
                strerror_r(errno, buf, sizeof(buf));
                ALOGE("Error writing to %s: %s\n", BOOSTPULSE_PATH, buf);
            }
        }
        break;

    case POWER_HINT_VSYNC:
        break;

    default:
        break;
    }
}

static void s5pc110_power_set_interactive(struct power_module *module, int on)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;
    sysfs_write(SAMPLING_RATE_ONDEMAND,
            on ? s5pc110->sampling_rate_screen_on : s5pc110->sampling_rate_screen_off);
}

static void s5pc110_power_init(struct power_module *module)
{
    struct s5pc110_power_module *s5pc110 = (struct s5pc110_power_module *) module;
    property_get("ro.sys.sampling_rate_on", s5pc110->sampling_rate_screen_on, SAMPLING_RATE_SCREEN_ON);
    property_get("ro.sys.sampling_rate_off", s5pc110->sampling_rate_screen_off, SAMPLING_RATE_SCREEN_OFF);
    sysfs_write(SAMPLING_RATE_ONDEMAND, s5pc110->sampling_rate_screen_on);
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct s5pc110_power_module HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            module_api_version: POWER_MODULE_API_VERSION_0_2,
            hal_api_version: HARDWARE_HAL_API_VERSION,
            id: POWER_HARDWARE_MODULE_ID,
            name: "S5PC110 Power HAL",
            author: "The Android Open Source Project",
            methods: &power_module_methods,
        },
       init: s5pc110_power_init,
       setInteractive: s5pc110_power_set_interactive,
       powerHint: s5pc110_power_hint,
    },

    lock: PTHREAD_MUTEX_INITIALIZER,
    boostpulse_fd: -1,
    boostpulse_warned: 0,
};
