/*
** Copyright 2010, The Android Open-Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <linux/ioctl.h>

#include "alsa_audio.h"

#define __force
#define __bitwise
#define __user
#include "asound.h"

#define DEBUG 1

/* alsa parameter manipulation cruft */

#define PARAM_MAX SNDRV_PCM_HW_PARAM_LAST_INTERVAL

static inline int param_is_mask(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_MASK);
}

static inline int param_is_interval(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL);
}

static inline struct snd_interval *param_to_interval(struct snd_pcm_hw_params *p, int n)
{
    return &(p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]);
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p, int n)
{
    return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{
    if (bit >= SNDRV_MASK_MAX)
        return;
    if (param_is_mask(n)) {
        struct snd_mask *m = param_to_mask(p, n);
        m->bits[0] = 0;
        m->bits[1] = 0;
        m->bits[bit >> 5] |= (1 << (bit & 31));
    }
}

static void param_set_min(struct snd_pcm_hw_params *p, int n, unsigned val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->min = val;
    }
}

static void param_set_max(struct snd_pcm_hw_params *p, int n, unsigned val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->max = val;
    }
}

static void param_set_int(struct snd_pcm_hw_params *p, int n, unsigned val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->min = val;
        i->max = val;
        i->integer = 1;
    }
}

static void param_init(struct snd_pcm_hw_params *p)
{
    int n;
    memset(p, 0, sizeof(*p));
    for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK;
         n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
            struct snd_mask *m = param_to_mask(p, n);
            m->bits[0] = ~0;
            m->bits[1] = ~0;
    }
    for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
         n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
            struct snd_interval *i = param_to_interval(p, n);
            i->min = 0;
            i->max = ~0;
    }
}

/* debugging gunk */

#if DEBUG
static const char *param_name[PARAM_MAX+1] = {
    [SNDRV_PCM_HW_PARAM_ACCESS] = "access",
    [SNDRV_PCM_HW_PARAM_FORMAT] = "format",
    [SNDRV_PCM_HW_PARAM_SUBFORMAT] = "subformat",

    [SNDRV_PCM_HW_PARAM_SAMPLE_BITS] = "sample_bits",
    [SNDRV_PCM_HW_PARAM_FRAME_BITS] = "frame_bits",
    [SNDRV_PCM_HW_PARAM_CHANNELS] = "channels",
    [SNDRV_PCM_HW_PARAM_RATE] = "rate",
    [SNDRV_PCM_HW_PARAM_PERIOD_TIME] = "period_time",
    [SNDRV_PCM_HW_PARAM_PERIOD_SIZE] = "period_size",
    [SNDRV_PCM_HW_PARAM_PERIOD_BYTES] = "period_bytes",
    [SNDRV_PCM_HW_PARAM_PERIODS] = "periods",
    [SNDRV_PCM_HW_PARAM_BUFFER_TIME] = "buffer_time",
    [SNDRV_PCM_HW_PARAM_BUFFER_SIZE] = "buffer_size",
    [SNDRV_PCM_HW_PARAM_BUFFER_BYTES] = "buffer_bytes",
    [SNDRV_PCM_HW_PARAM_TICK_TIME] = "tick_time",
};

static void param_dump(struct snd_pcm_hw_params *p)
{
    int n;

    for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK;
         n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
            struct snd_mask *m = param_to_mask(p, n);
            printf("%s = %08x%08x\n", param_name[n],
                   m->bits[1], m->bits[0]);
    }
    for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
         n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
            struct snd_interval *i = param_to_interval(p, n);
            printf("%s = (%d,%d) omin=%d omax=%d int=%d empty=%d\n",
                   param_name[n], i->min, i->max, i->openmin,
                   i->openmax, i->integer, i->empty);
    }
    printf("info = %08x\n", p->info);
    printf("msbits = %d\n", p->msbits);
    printf("rate = %d/%d\n", p->rate_num, p->rate_den);
    printf("fifo = %d\n", (int) p->fifo_size);
}

static void info_dump(struct snd_pcm_info *info)
{
    printf("device = %d\n", info->device);
    printf("subdevice = %d\n", info->subdevice);
    printf("stream = %d\n", info->stream);
    printf("card = %d\n", info->card);
    printf("id = '%s'\n", info->id);
    printf("name = '%s'\n", info->name);
    printf("subname = '%s'\n", info->subname);
    printf("dev_class = %d\n", info->dev_class);
    printf("dev_subclass = %d\n", info->dev_subclass);
    printf("subdevices_count = %d\n", info->subdevices_count);
    printf("subdevices_avail = %d\n", info->subdevices_avail);
}
#else
static void param_dump(struct snd_pcm_hw_params *p) {}
static void info_dump(struct snd_pcm_info *info) {}
#endif

#define PCM_ERROR_MAX 128

struct pcm {
    int fd;
    unsigned flags;
    int running:1;
    int underruns;
    unsigned buffer_size;
    char error[PCM_ERROR_MAX];
};

unsigned pcm_buffer_size(struct pcm *pcm)
{
    return pcm->buffer_size;
}

const char* pcm_error(struct pcm *pcm)
{
    return pcm->error;
}

static int oops(struct pcm *pcm, int e, const char *fmt, ...)
{
    va_list ap;
    int sz;

    va_start(ap, fmt);
    vsnprintf(pcm->error, PCM_ERROR_MAX, fmt, ap);
    va_end(ap);
    sz = strlen(pcm->error);

    if (errno)
        snprintf(pcm->error + sz, PCM_ERROR_MAX - sz,
                 ": %s", strerror(e));
    return -1;
}

int pcm_write(struct pcm *pcm, void *data, unsigned count)
{
    struct snd_xferi x;

    if (pcm->flags & PCM_IN)
        return -EINVAL;

    x.buf = data;
    x.frames = (pcm->flags & PCM_MONO) ? (count / 2) : (count / 4);

    for (;;) {
        if (!pcm->running) {
            if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_PREPARE))
                return oops(pcm, errno, "cannot prepare channel");
            if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x))
                return oops(pcm, errno, "cannot write initial data");
            fprintf(stderr,"RUNNING\n");
            pcm->running = 1;
            return 0;
        }
        if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x)) {
            pcm->running = 0;
            if (errno == EPIPE) {
                    /* we failed to make our window -- try to restart */
                pcm->underruns++;
                continue;
            }
            return oops(pcm, errno, "cannot write stream data");
        }
        fprintf(stderr,".");
        return 0;
    }
}

int pcm_read(struct pcm *pcm, void *data, unsigned count)
{
    struct snd_xferi x;

    if (!(pcm->flags & PCM_IN))
        return -EINVAL;

    x.buf = data;
    x.frames = (pcm->flags & PCM_MONO) ? (count / 2) : (count / 4);

    for (;;) {
        if (!pcm->running) {
            if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_PREPARE))
                return oops(pcm, errno, "cannot prepare channel");
            if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_START))
                return oops(pcm, errno, "cannot start channel");
            fprintf(stderr,"RUNNING\n");
            pcm->running = 1;
            return 0;
        }
        if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_READI_FRAMES, &x)) {
            pcm->running = 0;
            if (errno == EPIPE) {
                    /* we failed to make our window -- try to restart */
                pcm->underruns++;
                continue;
            }
            return oops(pcm, errno, "cannot read stream data");
        }
        fprintf(stderr,".");
        return 0;
    }
}

static struct pcm bad_pcm = {
    .fd = -1,
};

int pcm_close(struct pcm *pcm)
{
    if (pcm == &bad_pcm)
        return 0;

    if (pcm->fd >= 0)
        close(pcm->fd);
    pcm->running = 0;
    pcm->buffer_size = 0;
    pcm->fd = -1;
    return 0;
}

struct pcm *pcm_open(unsigned flags)
{
    const char *dname;
    struct pcm *pcm;
    struct snd_pcm_info info;
    struct snd_pcm_hw_params params;
    struct snd_pcm_sw_params sparams;
    unsigned bufsz = 8192;

    pcm = calloc(1, sizeof(struct pcm));
    if (!pcm)
        return &bad_pcm;

    if (flags & PCM_IN) {
        dname = "/dev/snd/pcmC0D0c";
    } else {
        dname = "/dev/snd/pcmC0D0p";
    }

    pcm->flags = flags;
    pcm->fd = open(dname, O_RDWR);
    if (pcm->fd < 0) {
        oops(pcm, errno, "cannot open device '%s'");
        return pcm;
    }

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_INFO, &info)) {
        oops(pcm, errno, "cannot get info - %s");
        goto fail;
    }
    info_dump(&info);

    param_init(&params);
    param_set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    param_set_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT,
                   SNDRV_PCM_FORMAT_S16_LE);
    param_set_mask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                   SNDRV_PCM_SUBFORMAT_STD);
    param_set_min(&params, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, bufsz);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                  (flags & PCM_MONO) ? 16 : 32);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  (flags & PCM_MONO) ? 1 : 2);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_PERIODS, 2);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_RATE, 44100);

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params)) {
        oops(pcm, errno, "cannot set hw params");
        goto fail;
    }
    param_dump(&params);

    memset(&sparams, 0, sizeof(sparams));
    sparams.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sparams.period_step = 1;
    sparams.avail_min = 1;
    sparams.start_threshold = bufsz / 4;
    sparams.stop_threshold = bufsz / 4;
    sparams.xfer_align = bufsz / 8; /* needed for old kernels */
    sparams.silence_size = 0;
    sparams.silence_threshold = 0;

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sparams)) {
        oops(pcm, errno, "cannot set sw params");
        goto fail;
    }

    pcm->buffer_size = bufsz / 2;
    pcm->underruns = 0;
    return pcm;

fail:
    close(pcm->fd);
    pcm->fd = -1;
    return pcm;
}

int pcm_ready(struct pcm *pcm)
{
    return pcm->fd >= 0;
}
