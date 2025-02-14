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
#define MAX_DEVICES 4
#define AWX_VOLUME             0x11112501
#define AWX_SPATIALIZATION     0x11112505
#define AWX_CYBER_MASTER_MUTE  0x11112526
#define AWX_WELCOME_MODE       0x11112540
#define TAG_MODULE_CUSTOM_AWX  0xC0000057
#define PADDING_8BYTE_ALIGN(x)  ((((x) + 7) & 7) ^ 7)

#define BUS_WS                 "BUS00_WS"
#define BUS_VWARN              "BUS01_no_ASIL"
#define BUS_ROAD_ADAS          "BUS02_Road_ADAS"
#define BUS_SWARN              "BUS03_ASIL"
#define BUS_SYS_NOTIFICATION   "BUS01_SYS_NOTIFICATION"
#define BUS_NAV_GUIDANCE       "BUS02_NAV_GUIDANCE"
#define BUS_PHONE              "BUS03_PHONE"
#define BUS_NAVIGATION2        "BUS0F_NAV_GUIDANCE2"

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
    int32_t device_id;
    char AudioDeviceId[MAX_ADDRESS_LEN];
    char bus_address[MAX_ADDRESS_LEN];
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t format;
    int32_t volume;
} PlatformAudioDevices;

//Structure for parsing XML
typedef struct {
    PlatformAudioDevices devices[MAX_DEVICES];
    int device_count;
    char current_element[50];
    char temp_data[50];
} XMLData;

//Enum to store Spatialization Value
typedef enum {
    FrontCenter = 0,
    FrontLeft = 1,
    FrontRight = 2,
    Center = 3,
    Rear = 4,
    RearLeft = 5,
    RearRight = 6,
    Right = 7,
    Left = 8
} Spatialization;

//Structure to handle  payload creation for Volume and Spatialization
typedef struct pal_awx_data
{
    uint16_t sub_func;
    uint16_t reserved;
    int32_t val[16];
}pal_awx_data_t;

//Bus Index corresponding to AWX Module
typedef enum pal_awx_bus_type
{
    BUS_MEDIA_WS_AWX,
    BUS_SYS_NOTIFICATION_AWX,
    BUS_NAVIGATION_AWX,
    BUS_PHONE_AWX,
    BUS_NAVIGATION2_AWX,
    BUS_VWARN_AWX,
    BUS_VROADADAS_AWX,
    BUS_RESERVED =0xF
}pal_awx_bus_type_t;

//Keeping both FVM and RBVM Bus to handle restore volume
const char bus_address[BUS_RESERVED][MAX_ADDRESS_LEN]={
    BUS_WS,                   //RBVM BUS
    BUS_SYS_NOTIFICATION,     //FVM BUS
    BUS_NAV_GUIDANCE,         //FVM BUS
    BUS_PHONE,                //FVM BUS
    BUS_NAVIGATION2,          //FVM BUS
    BUS_VWARN,                //RBVM BUS
    BUS_ROAD_ADAS             //RBVM BUS
};

//Structure to handle payload creation for MasterMute and WelcomeMode
typedef struct {
    int32_t value;
} Type2_param;

typedef struct {
    int FL;
    int FR;
    int RL;
    int RR;
} VectorValue;

typedef struct {
    char bus_addr[100];      //Bus Address for which volume needs to be restored
    float volValue;          //Value to restore the volume
} VolumeData;

typedef union {
    Type2_param paramVal;    //Indicates type2_param value (true/false, start/complete)
    VectorValue vector;      //Indicates value is a vector (FL,FR,RL,RR)
    VolumeData volume;       //Indicates volume data (Bus Address and Volume)
} DataValue;

typedef struct {
    char key[100];          //To store key
    DataValue value;        //union to hold actual data
} VehicleDataType;

typedef struct {
    const char* id;
    uint32_t (*handler)(VehicleDataType vehicledatatype); //Function pointer to handle ID
} IdHandler;

//Global lookup table to store ID-handler mappings
IdHandler idHandlerMap[100];
size_t idHandlerCount = 0;   //Number of registered IDs

static inline void place_markers(char const *name){
   int fd=open("/sys/kernel/boot_kpi/kpi_values", O_WRONLY);
   if (fd > 0)
   {
       char har_kpi[100] = {0};
       strlcpy(har_kpi, name, sizeof(har_kpi));
       write(fd, har_kpi, strlen(har_kpi));
       close(fd);
   }
}

bool parse_device_xml();
Spatialization stringToEnum(const char *str);
PlatformAudioDevices get_audio_device(const char* device_id);
PlatformAudioDevices load_default_config(const char* device);
void channel_map(uint8_t* ch_map, int channels);
int32_t Open_Device(PlatformAudioDevices *har_device);
void sample_write(PlatformAudioDevices *har_device, FILE *file, struct chunk_fmt fmt);
int32_t StopAndCloseUsecase(PlatformAudioDevices *har_device);
uint32_t buffer_size_bytes(PlatformAudioDevices *har_device);
ssize_t try_play_bytes(PlatformAudioDevices *har_device,void *buffer,int bytes);
uint32_t playback_rate(PlatformAudioDevices *har_device);
int32_t set_volume(PlatformAudioDevices *har_device, float volume);
uint32_t set_spatialization(PlatformAudioDevices *har_device, Spatialization value);
uint32_t set_custom_parameter(const char *id, VehicleDataType vehicledatatype);
