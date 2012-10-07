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

#define GOVERNOR_PATH "/sys/devices/system/cpu/cpu0/cpufreg/scaling_governor"
#define ONDEMAND_BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/ondemand/boostpulse"
#define INTERACTIVE_BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"

struct s5pc110_power_module {
    struct power_module base;
    pthread_mutex_t lock;
    int boostpulse_fd;
    int boostpulse_warned;
    char* boostpulse_path;
};

static int boostpulse_open(struct s5pc110_power_module *s5pc110)
{
    char gov[32];
    int gov_fd;
    int result;
    char buf[80];

    gov_fd = open(GOVERNOR_PATH, O_RDONLY);
    result = read(gov_fd, gov, 40);
    if (!result) {
		// couldn't read governor info, try ondemand
		s5pc110->boostpulse_path = ONDEMAND_BOOSTPULSE_PATH;
	}
	if (strncmp(gov, "interactive", 32)) {
		s5pc110->boostpulse_path = INTERACTIVE_BOOSTPULSE_PATH;
	} else {
		// assume ondemand if not interactive
		s5pc110->boostpulse_path = ONDEMAND_BOOSTPULSE_PATH;
	}

    pthread_mutex_lock(&s5pc110->lock);

    if (s5pc110->boostpulse_fd < 0) {
        s5pc110->boostpulse_fd = open(s5pc110->boostpulse_path, O_WRONLY);

        if (s5pc110->boostpulse_fd < 0) {
            if (!s5pc110->boostpulse_warned) {
                strerror_r(errno, buf, sizeof(buf));
                ALOGE("Error opening %s: %s\n", s5pc110->boostpulse_path, buf);
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
                ALOGE("Error writing to %s: %s\n", s5pc110->boostpulse_path, buf);
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
    return;
}

static void s5pc110_power_init(struct power_module *module)
{
    return;
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
