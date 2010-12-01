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
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "alsa_audio.h"

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;       /* sample_rate * num_channels * bps / 8 */
    uint16_t block_align;     /* num_channels * bps / 8 */
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

int play_file(unsigned rate, unsigned channels, int fd, unsigned count)
{
    struct pcm *pcm;
    struct mixer *mixer;
    struct pcm_ctl *ctl = NULL;
    unsigned bufsize;
    char *data;
    unsigned flags = PCM_OUT;

    if (channels == 1)
        flags |= PCM_MONO;
    else
        flags |= PCM_STEREO;

    pcm = pcm_open(flags);
    if (!pcm_ready(pcm)) {
        pcm_close(pcm);
        return -1;
    }

    mixer = mixer_open();
    if (mixer)
        ctl = mixer_get_control(mixer,"Playback Path", 0);
    
    bufsize = pcm_buffer_size(pcm);
    data = malloc(bufsize);
    if (!data) {
        fprintf(stderr,"could not allocate %d bytes\n", count);
        return -1;
    }

    while (read(fd, data, bufsize) == bufsize) {
        if (pcm_write(pcm, data, bufsize))
            break;
        
            /* HACK: remove */
        if (ctl) {
            //mixer_ctl_select(ctl, "SPK");
            ctl = 0;
        }
    }
    pcm_close(pcm);
    return 0;
}

int play_wav(const char *fn)
{
    struct wav_header hdr;
    unsigned rate, channels;
    int fd;
    fd = open(fn, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "aplay: cannot open '%s'\n", fn);
        return -1;
    }
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fprintf(stderr, "aplay: cannot read header\n");
        return -1;
    }
    fprintf(stderr,"aplay: %d ch, %d hz, %d bit, %s\n",
            hdr.num_channels, hdr.sample_rate, hdr.bits_per_sample,
            hdr.audio_format == FORMAT_PCM ? "PCM" : "unknown");
    
    if ((hdr.riff_id != ID_RIFF) ||
        (hdr.riff_fmt != ID_WAVE) ||
        (hdr.fmt_id != ID_FMT)) {
        fprintf(stderr, "aplay: '%s' is not a riff/wave file\n", fn);
        return -1;
    }
    if ((hdr.audio_format != FORMAT_PCM) ||
        (hdr.fmt_sz != 16)) {
        fprintf(stderr, "aplay: '%s' is not pcm format\n", fn);
        return -1;
    }
    if (hdr.bits_per_sample != 16) {
        fprintf(stderr, "aplay: '%s' is not 16bit per sample\n", fn);
        return -1;
    }

    return play_file(hdr.sample_rate, hdr.num_channels, fd, hdr.data_sz);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,"usage: aplay <file>\n");
        return -1;
    }

    return play_wav(argv[1]);
}

