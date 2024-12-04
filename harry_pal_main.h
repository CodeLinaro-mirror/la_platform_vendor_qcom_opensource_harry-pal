/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <expat.h>
#include <PalApi.h>
#include <PalDefs.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define MAX_ADDRESS_LEN 100


struct riff_wave_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t wave_id;
};

struct chunk_header {
    uint32_t id;
    uint32_t sz;
};

struct chunk_fmt {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

struct PlatformAudioDevices {
    /// The Sound Card number and device number to play to,
    /// run "aplay -l" from "apt get alsa-tools" to list devices)
    uint32_t card;
    uint32_t device;
    int32_t AudioDeviceId;
    char bus_address[MAX_ADDRESS_LEN];
    /// The number of channels in a frame.
    uint32_t channels;
    /// The number of frames per second.
    uint32_t rate;
    /// The number of frames in a period.
    uint32_t period_size;
    /// The number of periods in a PCM.
    uint32_t period_count;
    /// The sample format of a PCM.
    uint32_t format;
    /// Properties below are optional, if set to 0 defaults will be used.
    /// The minimum number of frames required to start the PCM.
    uint64_t start_threshold;
    /// The minimum number of frames required to stop the PCM.
    uint64_t stop_threshold;
    /// The minimum number of frames to silence the PCM.
    uint64_t silence_threshold;
    /// The number of frames to overwrite the playback buffer
    /// when the playback underrun is greater than the silence threshold.
    uint64_t silence_size;
    /// Minimum number of frames required to be available for writes.
    uint64_t avail_min;
};

typedef enum {
    /// 8000
    HAR_AUDIO_RATE_8KHZ      = 8000,
    /// 11025
    HAR_AUDIO_RATE_11_025KHZ = 11025,
    /// 16000
    HAR_AUDIO_RATE_16KHZ     = 16000,
    /// 22050
    HAR_AUDIO_RATE_22_05KHZ  = 22050,
    /// 24000
    HAR_AUDIO_RATE_24KHZ     = 24000,
    /// 32000
    HAR_AUDIO_RATE_32KHZ     = 32000,
    /// 44100
    HAR_AUDIO_RATE_44_1KHZ   = 44100,
    /// 48000
    HAR_AUDIO_RATE_48KHZ     = 48000,
    /// 96000
    HAR_AUDIO_RATE_96KHZ     = 96000,
    /// 192000
    HAR_AUDIO_RATE_192KHZ    = 192000
} har_audio_rate_t;

typedef enum {
    /// 8 bit
    HAR_AUDIO_BIT_DEPTH_8BIT = 8,
    /// 16 bit
    HAR_AUDIO_BIT_DEPTH_16BIT = 16,
    /// 24 bit
    HAR_AUDIO_BIT_DEPTH_24BIT = 24,
    /// 32 bit
    HAR_AUDIO_BIT_DEPTH_32BIT = 32,
    /// 64 bit
    HAR_AUDIO_BIT_DEPTH_64BIT = 64
} har_audio_bit_depth_t;

/** Number of channels
 */
typedef enum {
    /// 1 channel
    HAR_AUDIO_CHANNELS_1  = 1,
    /// 2 channels
    HAR_AUDIO_CHANNELS_2  = 2,
    /// 3 channels
    HAR_AUDIO_CHANNELS_3  = 3,
    /// 4 channels
    HAR_AUDIO_CHANNELS_4  = 4,
    /// 5 channels
    HAR_AUDIO_CHANNELS_5  = 5,
    /// 6 channels
    HAR_AUDIO_CHANNELS_6  = 6,
    /// 7 channels
    HAR_AUDIO_CHANNELS_7  = 7,
    /// 8 channels
    HAR_AUDIO_CHANNELS_8  = 8,
    /// 9 channels
    HAR_AUDIO_CHANNELS_9  = 9,
    /// 10 channels
    HAR_AUDIO_CHANNELS_10 = 10,
    /// 11 channels
    HAR_AUDIO_CHANNELS_11 = 11,
    /// 12 channels
    HAR_AUDIO_CHANNELS_12 = 12,
    /// 13 channels
    HAR_AUDIO_CHANNELS_13 = 13,
    /// 14 channels
    HAR_AUDIO_CHANNELS_14 = 14,
    /// 15 channels
    HAR_AUDIO_CHANNELS_15 = 15,
    /// 16 channels
    HAR_AUDIO_CHANNELS_16 = 16,
    /// 17 channels
    HAR_AUDIO_CHANNELS_17 = 17,
    /// 18 channels
    HAR_AUDIO_CHANNELS_18 = 18,
    /// 19 channels
    HAR_AUDIO_CHANNELS_19 = 19,
    /// 20 channels
    HAR_AUDIO_CHANNELS_20 = 20,
    /// 21 channels
    HAR_AUDIO_CHANNELS_21 = 21,
    /// 22 channels
    HAR_AUDIO_CHANNELS_22 = 22,
    /// 23 channels
    HAR_AUDIO_CHANNELS_23 = 23,
    /// 24 channels
    HAR_AUDIO_CHANNELS_24 = 24,
    /// 25 channels
    HAR_AUDIO_CHANNELS_25 = 25,
    /// 26 channels
    HAR_AUDIO_CHANNELS_26 = 26,
    /// 27 channels
    HAR_AUDIO_CHANNELS_27 = 27,
    /// 28 channels
    HAR_AUDIO_CHANNELS_28 = 28,
    /// 29 channels
    HAR_AUDIO_CHANNELS_29 = 29,
    /// 30 channels
    HAR_AUDIO_CHANNELS_30 = 30,
    /// 31 channels
    HAR_AUDIO_CHANNELS_31 = 31,
    /// 32 channels
    HAR_AUDIO_CHANNELS_32 = 32
} har_audio_channels_t;

typedef struct {
    char name[50];
    int id;
    char bus_address[MAX_ADDRESS_LEN];
} pal_device_t;

typedef struct {
    pal_device_t *data;
    size_t size;
    size_t capacity;
} IntVector;


typedef struct {
    IntVector *vec;
    char currentElement[50];
    char temp_data[256];
    int mode;
    int tid;
    int bus_address_found;
    pal_device_t currentDevice;
    XML_Parser parser;
} XMLData;


void init_vector(IntVector *vec);
void resize_vector(IntVector *vec);
void add_element(IntVector *vec, const pal_device_t *value);
void free_vector(IntVector *vec);
IntVector parse_device_id(const char* filename);
IntVector available_devices();
struct PlatformAudioDevices get_audio_device(const char* device_id);
void channel_map(uint8_t* ch_map, int channels);
uint32_t Open_Device(struct PlatformAudioDevices *har_pal);
void sample_write(FILE *file, struct chunk_fmt fmt);
int32_t StopAndCloseUsecase(struct PlatformAudioDevices *har_pal);
uint32_t buffer_size_bytes(struct PlatformAudioDevices *har_pal);
ssize_t try_play_bytes(struct PlatformAudioDevices *har_pal,void *buffer,int bytes);
uint32_t playback_rate(struct PlatformAudioDevices *har_pal);
int32_t set_volume(struct PlatformAudioDevices *har_pal, float volume);
