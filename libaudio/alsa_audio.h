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

#ifndef _AUDIO_H_
#define _AUDIO_H_

struct pcm;

#define PCM_OUT        0x00000000
#define PCM_IN         0x10000000

#define PCM_STEREO     0x00000000
#define PCM_MONO       0x01000000

#define PCM_44100HZ    0x00000000
#define PCM_48000HZ    0x00100000
#define PCM_8000HZ     0x00200000
#define PCM_RATE_MASK  0x00F00000

#define PCM_PERIOD_CNT_MIN 2
#define PCM_PERIOD_CNT_SHIFT 16
#define PCM_PERIOD_CNT_MASK (0xF << PCM_PERIOD_CNT_SHIFT)
#define PCM_PERIOD_SZ_MIN 128
#define PCM_PERIOD_SZ_SHIFT 12
#define PCM_PERIOD_SZ_MASK (0xF << PCM_PERIOD_SZ_SHIFT)

/* Acquire/release a pcm channel.
 * Returns non-zero on error
 */
struct pcm *pcm_open(unsigned flags);
int pcm_close(struct pcm *pcm);
int pcm_ready(struct pcm *pcm);

/* Returns a human readable reason for the last error. */
const char *pcm_error(struct pcm *pcm);

/* Returns the buffer size (int bytes) that should be used for pcm_write.
 * This will be 1/2 of the actual fifo size.
 */
unsigned pcm_buffer_size(struct pcm *pcm);

/* Write data to the fifo.
 * Will start playback on the first write or on a write that
 * occurs after a fifo underrun.
 */
int pcm_write(struct pcm *pcm, void *data, unsigned count);
int pcm_read(struct pcm *pcm, void *data, unsigned count);

struct mixer;
struct mixer_ctl;

struct mixer *mixer_open(void);
void mixer_close(struct mixer *mixer);
void mixer_dump(struct mixer *mixer);

struct mixer_ctl *mixer_get_control(struct mixer *mixer,
                                    const char *name, unsigned index);
struct mixer_ctl *mixer_get_nth_control(struct mixer *mixer, unsigned n);

int mixer_ctl_set(struct mixer_ctl *ctl, unsigned percent);
int mixer_ctl_select(struct mixer_ctl *ctl, const char *value);
void mixer_ctl_print(struct mixer_ctl *ctl);

#endif
