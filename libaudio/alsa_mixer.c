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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <linux/ioctl.h>
#define __force
#define __bitwise
#define __user
#include "asound.h"

#include "alsa_audio.h"

static const char *elem_iface_name(snd_ctl_elem_iface_t n)
{
    switch (n) {
    case SNDRV_CTL_ELEM_IFACE_CARD: return "CARD";
    case SNDRV_CTL_ELEM_IFACE_HWDEP: return "HWDEP";
    case SNDRV_CTL_ELEM_IFACE_MIXER: return "MIXER";
    case SNDRV_CTL_ELEM_IFACE_PCM: return "PCM";
    case SNDRV_CTL_ELEM_IFACE_RAWMIDI: return "MIDI";
    case SNDRV_CTL_ELEM_IFACE_TIMER: return "TIMER";
    case SNDRV_CTL_ELEM_IFACE_SEQUENCER: return "SEQ";
    default: return "???";
    }
}

static const char *elem_type_name(snd_ctl_elem_type_t n)
{
    switch (n) {
    case SNDRV_CTL_ELEM_TYPE_NONE: return "NONE";
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN: return "BOOL";
    case SNDRV_CTL_ELEM_TYPE_INTEGER: return "INT32";
    case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "ENUM";
    case SNDRV_CTL_ELEM_TYPE_BYTES: return "BYTES";
    case SNDRV_CTL_ELEM_TYPE_IEC958: return "IEC958";
    case SNDRV_CTL_ELEM_TYPE_INTEGER64: return "INT64";
    default: return "???";
    }
}


struct mixer_ctl {
    struct mixer *mixer;
    struct snd_ctl_elem_info *info;
    char **ename;
};

struct mixer {
    int fd;
    struct snd_ctl_elem_info *info;
    struct mixer_ctl *ctl;
    unsigned count;
};

void mixer_close(struct mixer *mixer)
{
    unsigned n,m;

    if (mixer->fd >= 0)
        close(mixer->fd);

    if (mixer->ctl) {
        for (n = 0; n < mixer->count; n++) {
            if (mixer->ctl[n].ename) {
                unsigned max = mixer->ctl[n].info->value.enumerated.items;
                for (m = 0; m < max; m++)
                    free(mixer->ctl[n].ename[m]);
                free(mixer->ctl[n].ename);
            }
        }
        free(mixer->ctl);
    }

    if (mixer->info)
        free(mixer->info);

    free(mixer);
}

struct mixer *mixer_open(void)
{
    struct snd_ctl_elem_list elist;
    struct snd_ctl_elem_info tmp;
    struct snd_ctl_elem_id *eid = NULL;
    struct mixer *mixer = NULL;
    unsigned n, m;
    int fd;

    fd = open("/dev/snd/controlC0", O_RDWR);
    if (fd < 0)
        return 0;

    memset(&elist, 0, sizeof(elist));
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &elist) < 0)
        goto fail;

    mixer = calloc(1, sizeof(*mixer));
    if (!mixer)
        goto fail;

    mixer->ctl = calloc(elist.count, sizeof(struct mixer_ctl));
    mixer->info = calloc(elist.count, sizeof(struct snd_ctl_elem_info));
    if (!mixer->ctl || !mixer->info)
        goto fail;

    eid = calloc(elist.count, sizeof(struct snd_ctl_elem_id));
    if (!eid)
        goto fail;

    mixer->count = elist.count;
    mixer->fd = fd;
    elist.space = mixer->count;
    elist.pids = eid;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &elist) < 0)
        goto fail;

    for (n = 0; n < mixer->count; n++) {
        struct snd_ctl_elem_info *ei = mixer->info + n;
        ei->id.numid = eid[n].numid;
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, ei) < 0)
            goto fail;
        mixer->ctl[n].info = ei;
        mixer->ctl[n].mixer = mixer;
        if (ei->type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
            char **enames = calloc(ei->value.enumerated.items, sizeof(char*));
            if (!enames)
                goto fail;
            mixer->ctl[n].ename = enames;
            for (m = 0; m < ei->value.enumerated.items; m++) {
                memset(&tmp, 0, sizeof(tmp));
                tmp.id.numid = ei->id.numid;
                tmp.value.enumerated.item = m;
                if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &tmp) < 0)
                    goto fail;
                enames[m] = strdup(tmp.value.enumerated.name);
                if (!enames[m])
                    goto fail;
            }
        }
    }

    free(eid);
    return mixer;

fail:
    if (eid)
        free(eid);
    if (mixer)
        mixer_close(mixer);
    else if (fd >= 0)
        close(fd);
    return 0;
}

void mixer_dump(struct mixer *mixer)
{
    unsigned n, m;

    printf("  id iface dev sub idx num perms     type   name\n");
    for (n = 0; n < mixer->count; n++) {
        struct snd_ctl_elem_info *ei = mixer->info + n;

        printf("%4d %5s %3d %3d %3d %3d %c%c%c%c%c%c%c%c%c %-6s %s",
               ei->id.numid, elem_iface_name(ei->id.iface),
               ei->id.device, ei->id.subdevice, ei->id.index,
               ei->count,
               (ei->access & SNDRV_CTL_ELEM_ACCESS_READ) ? 'r' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_WRITE) ? 'w' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_VOLATILE) ? 'V' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_TIMESTAMP) ? 'T' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_TLV_READ) ? 'R' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_TLV_WRITE) ? 'W' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND) ? 'C' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_INACTIVE) ? 'I' : ' ',
               (ei->access & SNDRV_CTL_ELEM_ACCESS_LOCK) ? 'L' : ' ',
               elem_type_name(ei->type),
               ei->id.name);
        switch (ei->type) {
        case SNDRV_CTL_ELEM_TYPE_INTEGER:
            printf(ei->value.integer.step ?
                   " { %ld-%ld, %ld }\n" : " { %ld-%ld }",
                   ei->value.integer.min,
                   ei->value.integer.max,
                   ei->value.integer.step);
            break;
        case SNDRV_CTL_ELEM_TYPE_INTEGER64:
            printf(ei->value.integer64.step ?
                   " { %lld-%lld, %lld }\n" : " { %lld-%lld }",
                   ei->value.integer64.min,
                   ei->value.integer64.max,
                   ei->value.integer64.step);
            break;
        case SNDRV_CTL_ELEM_TYPE_ENUMERATED: {
            unsigned m;
            printf(" { %s=0", mixer->ctl[n].ename[0]);
            for (m = 1; m < ei->value.enumerated.items; m++)
                printf(", %s=%d", mixer->ctl[n].ename[m],m);
            printf(" }");
            break;
        }
        }
        printf("\n");
    }
}

struct mixer_ctl *mixer_get_control(struct mixer *mixer,
                                    const char *name, unsigned index)
{
    unsigned n;
    for (n = 0; n < mixer->count; n++) {
        if (mixer->info[n].id.index == index) {
            if (!strcmp(name, (char*) mixer->info[n].id.name)) {
                return mixer->ctl + n;
            }
        }
    }
    return 0;
}

struct mixer_ctl *mixer_get_nth_control(struct mixer *mixer, unsigned n)
{
    if (n < mixer->count)
        return mixer->ctl + n;
    return 0;
}

void mixer_ctl_print(struct mixer_ctl *ctl)
{
    struct snd_ctl_elem_value ev;
    unsigned n;

    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info->id.numid;
    if (ioctl(ctl->mixer->fd, SNDRV_CTL_IOCTL_ELEM_READ, &ev))
        return;
    printf("%s:", ctl->info->id.name);

    switch (ctl->info->type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
        for (n = 0; n < ctl->info->count; n++)
            printf(" %s", ev.value.integer.value[n] ? "ON" : "OFF");
        break;
    case SNDRV_CTL_ELEM_TYPE_INTEGER: {
        for (n = 0; n < ctl->info->count; n++)
            printf(" %ld", ev.value.integer.value[n]);
        break;
    }
    case SNDRV_CTL_ELEM_TYPE_INTEGER64:
        for (n = 0; n < ctl->info->count; n++)
            printf(" %lld", ev.value.integer64.value[n]);
        break;
    case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
        for (n = 0; n < ctl->info->count; n++) {
            unsigned v = ev.value.enumerated.item[n];
            printf(" %d (%s)", v,
                   (v < ctl->info->value.enumerated.items) ? ctl->ename[v] : "???");
        }
        break;
    default:
        printf(" ???");
    }
    printf("\n");
}

static long scale_int(struct snd_ctl_elem_info *ei, unsigned _percent)
{
    long percent;
    long range;

    if (_percent > 100)
        percent = 100;
    else
        percent = (long) _percent;

    range = (ei->value.integer.max - ei->value.integer.min);

    return ei->value.integer.min + (range * percent) / 100LL;
}

static long long scale_int64(struct snd_ctl_elem_info *ei, unsigned _percent)
{
    long long percent;
    long long range;

    if (_percent > 100)
        percent = 100;
    else
        percent = (long) _percent;

    range = (ei->value.integer.max - ei->value.integer.min) * 100LL;

    return ei->value.integer.min + (range / percent);
}

int mixer_ctl_set(struct mixer_ctl *ctl, unsigned percent)
{
    struct snd_ctl_elem_value ev;
    unsigned n;

    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info->id.numid;
    switch (ctl->info->type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
        for (n = 0; n < ctl->info->count; n++)
            ev.value.integer.value[n] = !!percent;
        break;
    case SNDRV_CTL_ELEM_TYPE_INTEGER: {
        long value = scale_int(ctl->info, percent);
        for (n = 0; n < ctl->info->count; n++)
            ev.value.integer.value[n] = value;
        break;
    }
    case SNDRV_CTL_ELEM_TYPE_INTEGER64: {
        long long value = scale_int64(ctl->info, percent);
        for (n = 0; n < ctl->info->count; n++)
            ev.value.integer64.value[n] = value;
        break;
    }
    default:
        errno = EINVAL;
        return -1;
    }

    return ioctl(ctl->mixer->fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev);
}

int mixer_ctl_select(struct mixer_ctl *ctl, const char *value)
{
    unsigned n, max;
    struct snd_ctl_elem_value ev;

    if (ctl->info->type != SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
        errno = EINVAL;
        return -1;
    }

    max = ctl->info->value.enumerated.items;
    for (n = 0; n < max; n++) {
        if (!strcmp(value, ctl->ename[n])) {
            memset(&ev, 0, sizeof(ev));
            ev.value.enumerated.item[0] = n;
            ev.id.numid = ctl->info->id.numid;
            if (ioctl(ctl->mixer->fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev) < 0)
                return -1;
            return 0;
        }
    }

    errno = EINVAL;
    return -1;
}
