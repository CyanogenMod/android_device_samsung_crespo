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

int record_file(unsigned rate, unsigned channels, int fd, unsigned count)
{
    struct pcm *pcm;
    unsigned avail, xfer, bufsize;
    char *data, *next;
    int r;

    pcm = pcm_open(PCM_IN|PCM_MONO);
    if (!pcm_ready(pcm)) {
        pcm_close(pcm);
        goto fail;
    }

    bufsize = pcm_buffer_size(pcm);
    
    data = malloc(bufsize);
    if (!data) {
        fprintf(stderr,"could not allocate %d bytes\n", count);
        return -1;
    }

    while (!pcm_read(pcm, data, bufsize)) {
        if (write(fd, data, bufsize) != bufsize) {
            fprintf(stderr,"could not write %d bytes\n", bufsize);
            return -1;
        }
    }

    close(fd);
    pcm_close(pcm);
    return 0;
    
fail:
    fprintf(stderr,"pcm error: %s\n", pcm_error(pcm));
    return -1;
}

int rec_wav(const char *fn)
{
    struct wav_header hdr;
    unsigned rate, channels;
    int fd;
    fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        fprintf(stderr, "arec: cannot open '%s'\n", fn);
        return -1;
    }

    hdr.riff_id = ID_RIFF;
    hdr.riff_fmt = ID_WAVE;
    hdr.fmt_id = ID_FMT;
    hdr.audio_format = FORMAT_PCM;
    hdr.fmt_sz = 16;
    hdr.bits_per_sample = 16;
    hdr.num_channels = 1;
    hdr.data_sz = 0;
    hdr.sample_rate = 44100;

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fprintf(stderr, "arec: cannot write header\n");
        return -1;
    }
    fprintf(stderr,"arec: %d ch, %d hz, %d bit, %s\n",
            hdr.num_channels, hdr.sample_rate, hdr.bits_per_sample,
            hdr.audio_format == FORMAT_PCM ? "PCM" : "unknown");
    

    return record_file(hdr.sample_rate, hdr.num_channels, fd, hdr.data_sz);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,"usage: arec <file>\n");
        return -1;
    }

    return rec_wav(argv[1]);
}

