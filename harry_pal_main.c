/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "HAR_PAL"

#include "harry_pal_common.h"
#include "harry_pal_main.h"
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>

#define PCM_DEVICE_NAME_DEFAULT "default"
#define PERIOD_SIZE 240
#define PERIOD_COUNT 2
#define DEFAULT_CARD_ID 0

static pal_stream_handle_t *pal_stream_handle;
struct pal_buffer_config outBufCfg = {0, 0, 0};
static int pal_init_status = 0;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static PlatformAudioDevices platform_audio_devices_list[MAX_DEVICES];

uint8_t stream_started = 0;

typedef struct {
    // ALSA Device name
    char *pDeviceName;

    // map_har_audio_rate
    har_audio_rate_t audioRate;
    // map_har_audio_bit_depth
    har_audio_bit_depth_t audioBitDepth;
    // map_har_channels
    har_audio_channels_t audioChannels;
    uint32_t frames;
    uint32_t deviceId;
    uint32_t cardId;
    uint32_t buffer_size;

    /////////////
    // PAL data
    /////////////
    struct pal_device devices;
    struct pal_stream_attributes stream_attr;
    struct pal_channel_info temp;
    uint32_t outBufSize;
    uint32_t outBufCount;

    /////////////
    // ALSA Data
    /////////////
    uint32_t alsaPeriodSize;
    uint32_t alsaPeriodCount;
    uint32_t alsaStartThreshold;
    uint32_t alsaStopThreshold;
} pvt_data_t;

typedef enum {
    //OUTPUT DEVICES
    HAR_PAL_DEVICE_OUT_MIN = 0,
    HAR_PAL_DEVICE_OUT_NONE = 1,
    HAR_PAL_DEVICE_OUT_SPEAKER_WS = 2,
    HAR_PAL_DEVICE_OUT_SPEAKER_V_WARN = 3,
    HAR_PAL_DEVICE_OUT_SPEAKER_ROAD_ADAS = 4,
    HAR_PAL_DEVICE_OUT_SPEAKER_S_WARN = 5,
    HAR_PAL_DEVICE_OUT_SPEAKER_A2B = 6,
    HAR_PAL_DEVICE_OUT_SPEAKER_A2B2 = 7,
    // Add new OUT devices here, increment MAX and MIN below when you do so
    HAR_PAL_DEVICE_OUT_MAX = 8,
} har_pal_out_device_id_t;

void get_out_device_id (har_pal_out_device_id_t outDeviceId,
                        struct pal_device *devices)
{
    switch (outDeviceId) {
        case HAR_PAL_DEVICE_OUT_MIN:
            devices->id = PAL_DEVICE_OUT_MIN;
            break;

        case HAR_PAL_DEVICE_OUT_NONE:
            devices->id = PAL_DEVICE_NONE;
            break;

        case HAR_PAL_DEVICE_OUT_SPEAKER_WS:
            devices->id = PAL_DEVICE_OUT_SPEAKER;
            break;

        case HAR_PAL_DEVICE_OUT_SPEAKER_V_WARN:
            devices->id = PAL_DEVICE_OUT_SPEAKER;
            break;

        case HAR_PAL_DEVICE_OUT_SPEAKER_ROAD_ADAS:
            devices->id = PAL_DEVICE_OUT_SPEAKER;
            break;

       case HAR_PAL_DEVICE_OUT_SPEAKER_S_WARN:
            devices->id = PAL_DEVICE_OUT_SPEAKER;
            break;

        case HAR_PAL_DEVICE_OUT_SPEAKER_A2B:
            devices->id = PAL_DEVICE_OUT_A2B_SPKR;
            break;

        case HAR_PAL_DEVICE_OUT_SPEAKER_A2B2:
            devices->id = PAL_DEVICE_OUT_A2B_SPKR;
            break;

        default:
            HAR_PAL_ERR(LOG_TAG,"Invalid output device id");
            break;
    }
}

void channel_map(uint8_t* ch_map, int channels)
{
    switch (channels) {
        case 1:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            break;

        case 2:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            break;

        case 3:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            break;

        case 4:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_LB;
            ch_map[3] = PAL_CHMAP_CHANNEL_RB;
            break;

        case 5:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_LB;
            ch_map[3] = PAL_CHMAP_CHANNEL_RB;
            ch_map[4] = PAL_CHMAP_CHANNEL_C;
            break;

        case 6:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
            ch_map[4] = PAL_CHMAP_CHANNEL_LB;
            ch_map[5] = PAL_CHMAP_CHANNEL_RB;
            break;

        case 7:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_FR;
            ch_map[2] = PAL_CHMAP_CHANNEL_C;
            ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
            ch_map[4] = PAL_CHMAP_CHANNEL_LB;
            ch_map[5] = PAL_CHMAP_CHANNEL_RB;
            ch_map[6] = PAL_CHMAP_CHANNEL_RC;
            break;

        case 8:
            ch_map[0] = PAL_CHMAP_CHANNEL_FL;
            ch_map[1] = PAL_CHMAP_CHANNEL_C;
            ch_map[2] = PAL_CHMAP_CHANNEL_FR;
            ch_map[3] = PAL_CHMAP_CHANNEL_SL;
            ch_map[4] = PAL_CHMAP_CHANNEL_SR;
            ch_map[5] = PAL_CHMAP_CHANNEL_LB;
            ch_map[6] = PAL_CHMAP_CHANNEL_RB;
            ch_map[7] = PAL_CHMAP_CHANNEL_LFE;
            break;
    }
}

void XMLCALL start_element(void *userData, const char *name, const char **attr) {
    XMLData *data = (XMLData *)userData;
    memset(data->temp_data, 0, sizeof(data->temp_data));
    PlatformAudioDevices *current_device = &data->devices[data->device_count];

    strlcpy(data->current_element, name, sizeof(data->current_element));

    if (strcmp(name, "device") == 0) {
        for (int i = 0; attr[i]; i += 2) {
            if (strcmp(attr[i], "AudioDeviceId") == 0) {
                strlcpy(current_device->AudioDeviceId, attr[i + 1], sizeof(current_device->AudioDeviceId));
            }
        }
    } else if (strcmp(name, "pcm_config") == 0) {
        for (int i = 0; attr[i]; i += 2) {
            if (strcmp(attr[i], "sample_rate") == 0) {
                current_device->sample_rate = atoi(attr[i + 1]);
            } else if (strcmp(attr[i], "format") == 0) {
                current_device->format = atoi(attr[i + 1]);
            } else if (strcmp(attr[i], "channel") == 0) {
                current_device->channels = atoi(attr[i + 1]);
            }
        }
    }
}

void XMLCALL end_element(void *userData, const char *name) {
    XMLData *data = (XMLData *)userData;

    if (strcmp(name, "device") == 0) {
        data->device_count++;
    }

    memset(data->current_element, 0, sizeof(data->current_element));
}

void XMLCALL character_data_handler(void *userData, const char *s, int len) {
    XMLData *data = (XMLData *)userData;
    PlatformAudioDevices *current_device = &data->devices[data->device_count];

    if (strcmp(data->current_element, "name") == 0) {
        return;
    }
    if (len >= sizeof(data->temp_data)) {
        len = sizeof(data->temp_data) - 1;
    }

    strlcpy(data->temp_data, s, len + 1);

    if (strcmp(data->current_element, "id") == 0) {
        current_device->device_id = atoi(data->temp_data);
    } else if (strcmp(data->current_element, "Bus_Address") == 0) {
        strlcpy(current_device->bus_address, data->temp_data, sizeof(current_device->bus_address));
    } else if (strcmp(data->current_element, "Volume") == 0) {
        current_device->volume = atoi(data->temp_data);
    }

    memset(data->temp_data, 0, sizeof(data->temp_data));
}

/*New API Introduced in PDK21.
 *This function parses and returns an AudioDevice for a specific device_id.
 */
PlatformAudioDevices get_audio_device(const char* device_id) {
    int ret;
    PlatformAudioDevices audio_device;
    audio_device.device_id = -1;
    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      return audio_device;
    }

    HAR_PAL_DBG(LOG_TAG,"AudioDeviceId = %s",device_id);
    if (!parse_device_xml()){
        HAR_PAL_ERR(LOG_TAG,"Failed to get Device.Loading default config");
	audio_device = load_default_config(device_id);
        ret = pthread_mutex_unlock(&lock);
	if (ret !=0) {
          HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
        }
	return audio_device;
    }

    for (int i = 0; i < MAX_DEVICES; i++) {
        HAR_PAL_VERBOSE(LOG_TAG,"i = %d platform_audio_devices_list = %d",i,platform_audio_devices_list[i].device_id);
        if(strcmp(device_id,platform_audio_devices_list[i].AudioDeviceId) == 0){
            HAR_PAL_DBG(LOG_TAG,"AudioDeviceId = %s",device_id);
            ret = pthread_mutex_unlock(&lock);
	    if (ret !=0) {
              HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
            }

            return platform_audio_devices_list[i];
        }
    }

    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
    }
    return audio_device;
}

/*Function to parse the XML*/
bool parse_device_xml() {
    if (platform_audio_devices_list[0].AudioDeviceId[0] != '\0') {
        return true;
    }
    const char *filename = HAR_PAL_DEVICE;
    XMLData user_data = {0};

    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser){
        HAR_PAL_ERR(LOG_TAG, "Failed to create parser");
        return false;
    }
    XML_SetUserData(parser, &user_data);
    XML_SetElementHandler(parser, start_element, end_element);
    XML_SetCharacterDataHandler(parser, character_data_handler);

    FILE *file = fopen(filename, "r");
    if (!file) {
        HAR_PAL_ERR(LOG_TAG, "Cannot open file: %s", filename);
        XML_ParserFree(parser);
        return false;
    }

    char buffer[BUFSIZ];
    size_t len;
    while ((len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (XML_Parse(parser, buffer, len, XML_FALSE) == XML_STATUS_ERROR) {
            HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
            XML_ParserFree(parser);
            fclose(file);
            return false;
        }
    }
    // Finalize Parsing
    if (XML_Parse(parser, NULL, 0, XML_TRUE) == XML_STATUS_ERROR) {
        HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s\n", XML_ErrorString(XML_GetErrorCode(parser)));
    }

    for (int i = 0; i < user_data.device_count; i++) {
        platform_audio_devices_list[i] = user_data.devices[i];
        PlatformAudioDevices *device = &user_data.devices[i];
        HAR_PAL_DBG(LOG_TAG,
            "device_id: %d AudioDeviceId: %s bus_address: %s channels: %d Sample Rate: %d format: %d volume: %d\n",
            device->device_id, device->AudioDeviceId, device->bus_address, device->channels,
            device->sample_rate, device->format, device->volume
        );
    }

    fclose(file);
    XML_ParserFree(parser);
    return true;
}

/*Loads default configuration, if XML Parsing fails*/
PlatformAudioDevices load_default_config(const char* device) {
    PlatformAudioDevices audio_device;
    audio_device.channels = 1;


     if (strcmp("WELCOME_SEQUENCE",device) == 0) {
        HAR_PAL_INFO(LOG_TAG,"get_audio_device id= WELCOME_SEQUENCE");
	strlcpy(audio_device.bus_address, "BUS00_WS", MAX_ADDRESS_LEN);
	audio_device.device_id = 2;
        audio_device.channels = 2;
     } else if (strcmp("WARN",device) == 0) {
	HAR_PAL_INFO(LOG_TAG,"get_audio_device id= WARN");
        strlcpy(audio_device.bus_address, "BUS01_no_ASIL", MAX_ADDRESS_LEN);
        audio_device.device_id = 3;
     } else if (strcmp("ROAD-ADAS",device) == 0) {
	HAR_PAL_INFO(LOG_TAG,"get_audio_device id= ROAD-ADAS");
        strlcpy(audio_device.bus_address, "BUS02_Road_Adas", MAX_ADDRESS_LEN);
        audio_device.device_id = 4;
     } else if (strcmp("SWARN",device) == 0) {
	HAR_PAL_INFO(LOG_TAG,"get_audio_device id= SWARN");
        strlcpy(audio_device.bus_address, "BUS03_ASIL", MAX_ADDRESS_LEN);
        audio_device.device_id = 5;
     }

     audio_device.sample_rate = 48000;
     audio_device.volume = -3100;
     audio_device.format = 16;
     return audio_device;
}

/*Opens the given Audio Device, ensuring it is ready to play sound.
 *This function further calls pal_init(), pal_stream_open(),
 *pal_stream_get_buffer_size() and pal_stream_get_buffer_size().
 *Further it calls pal_stream_start() as well.
 */
int32_t Open_Device(PlatformAudioDevices *har_device) {

    int ret = -EINVAL;
    int32_t err = 0;
    uint32_t no_of_devices = 1;
    har_pal_out_device_id_t outDeviceId = 0;

    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      goto exit;
    }
    if (pal_init_status == 0) {
        err = pal_init();
        if (err) {
            HAR_PAL_ERR(LOG_TAG,"pal_init failed");
            goto exit;
        }
        pal_init_status++;
    }

    HAR_PAL_DBG(LOG_TAG, "%s Enter, pal_init_status =%d ",__func__, pal_init_status);

    if(pPvtData->pDeviceName == NULL)
        pPvtData->pDeviceName = PCM_DEVICE_NAME_DEFAULT;
    pPvtData->cardId = DEFAULT_CARD_ID;
    pPvtData->alsaPeriodCount = PERIOD_COUNT;
    pPvtData->alsaPeriodSize = PERIOD_SIZE;
    pPvtData->alsaStartThreshold = 0;
    pPvtData->alsaStopThreshold = 0;
    pPvtData->audioRate = HAR_AUDIO_RATE_48KHZ;
    pPvtData->audioBitDepth = HAR_AUDIO_BIT_DEPTH_16BIT;
    pPvtData->audioChannels = HAR_AUDIO_CHANNELS_2;

    outDeviceId = har_device->device_id;
    if (outDeviceId < 0 || outDeviceId > 7) {
        HAR_PAL_ERR(LOG_TAG,"har_pal_out_device_id has to be in between 0 to 7."
                "setting the default value as 3");
        outDeviceId = 2;
    }

    HAR_PAL_DBG(LOG_TAG, "har_pal_out_device_id = %d", outDeviceId);

    //need to discuss..
    get_out_device_id(outDeviceId, &(pPvtData->devices));
    pPvtData->deviceId = pPvtData->devices.id;

    if (har_device->sample_rate >= HAR_AUDIO_RATE_8KHZ &&
                har_device->sample_rate <= HAR_AUDIO_RATE_192KHZ) {
        pPvtData->audioRate = har_device->sample_rate;
        HAR_PAL_VERBOSE(LOG_TAG, "audioRate = %d", pPvtData->audioRate);
    }

    if (har_device->format >= HAR_AUDIO_BIT_DEPTH_8BIT &&
                har_device->format <= HAR_AUDIO_BIT_DEPTH_64BIT) {
        pPvtData->audioBitDepth = har_device->format;
        HAR_PAL_VERBOSE(LOG_TAG, "audioBitDepth = %d", pPvtData->audioBitDepth);
    }

    if (har_device->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_device->channels <= HAR_AUDIO_CHANNELS_32) {
        pPvtData->audioChannels = har_device->channels;
        HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", pPvtData->audioChannels);
    }

    //Setting Stream Attributes
    memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
    pPvtData->stream_attr.type = PAL_STREAM_PLAYBACK_BUS;

    if (har_device->bus_address[0] != '\0') {
        pPvtData->stream_attr.bus_addr = har_device->bus_address;
        HAR_PAL_DBG(LOG_TAG, "Bus Address for device ID %d: %s", outDeviceId, har_device->bus_address);
    } else {
        HAR_PAL_ERR(LOG_TAG, "Bus Address for device ID %d not found using BUS00_WS", outDeviceId);
        pPvtData->stream_attr.bus_addr = "BUS00_WS";
    }

    pPvtData->stream_attr.direction = PAL_AUDIO_OUTPUT;
    pPvtData->stream_attr.out_media_config.ch_info.channels =
                pPvtData->audioChannels;
    pPvtData->stream_attr.out_media_config.sample_rate = pPvtData->audioRate;
    channel_map(pPvtData->stream_attr.out_media_config.ch_info.ch_map,
                pPvtData->audioChannels);
    pPvtData->stream_attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
    pPvtData->stream_attr.ch_mask = 0x3;
    pPvtData->stream_attr.format = 0x1;

    switch (pPvtData->audioBitDepth) {
        case HAR_AUDIO_BIT_DEPTH_16BIT:
            pPvtData->stream_attr.out_media_config.bit_width = 16;
            break;
        case HAR_AUDIO_BIT_DEPTH_32BIT:
            pPvtData->stream_attr.out_media_config.bit_width = 32;
            break;
        case HAR_AUDIO_BIT_DEPTH_8BIT:
            pPvtData->stream_attr.out_media_config.bit_width = 8;
            break;
        default:
            pPvtData->stream_attr.out_media_config.bit_width = 16;
            break;
    }

    // setting device attributes
    memset(&pPvtData->devices, 0, sizeof(pPvtData->devices));
    pPvtData->devices.id = pPvtData->deviceId;
    pPvtData->devices.config.ch_info.channels = pPvtData->audioChannels;
    pPvtData->devices.config.sample_rate = pPvtData->audioRate;
    pPvtData->devices.config.aud_fmt_id = PAL_AUDIO_FMT_DEFAULT_PCM;
    channel_map(pPvtData->devices.config.ch_info.ch_map, pPvtData->audioChannels);

    HAR_PAL_VERBOSE(LOG_TAG, "Calling pal_stream_open");
    err = pal_stream_open(&pPvtData->stream_attr, no_of_devices, &pPvtData->devices, 0, NULL,
                            NULL, 0,  &pal_stream_handle);
    if (err) {
        HAR_PAL_ERR(LOG_TAG, "Error:Failed to open RBVM  stream");
        goto exit;
    }
    HAR_PAL_DBG(LOG_TAG, "Stream Opened succesfully\n");

    ret = pal_stream_get_buffer_size(pal_stream_handle,
                                         &pPvtData->outBufCount, &pPvtData->outBufSize);

    if(ret) {
        HAR_PAL_ERR(LOG_TAG,"Pal get buffer size failed (%x).Setting Default", ret);
        /*Fall-back to default buffer count and size*/
        pPvtData->outBufCount = 4;
        pPvtData->outBufSize = 1024 * 4;
    }

    HAR_PAL_DBG(LOG_TAG, "outBufSize %d outBufCount %d\n",
                  pPvtData->outBufSize, pPvtData->outBufCount);
    outBufCfg.buf_size = pPvtData->outBufSize;
    outBufCfg.buf_count = pPvtData->outBufCount;
    ret = pal_stream_set_buffer_size(pal_stream_handle, NULL, &outBufCfg);
    if (ret) {
        HAR_PAL_ERR(LOG_TAG,"Pal Stream set buffer size Error  (%x)\n", ret);
    }

    HAR_PAL_DBG(LOG_TAG, "%s Calling pal_stream_start",__func__);
    ret = pal_stream_start(pal_stream_handle);

    if (ret == 0)
           stream_started = 1;
    else {
        HAR_PAL_ERR(LOG_TAG,"Error:Failed to Start RBVM");
        goto exit;
    }

    goto exit;

exit:
    stream_started =0;
    free(pPvtData);
    pPvtData = NULL;
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
      return ret;
    }
    HAR_PAL_ERR(LOG_TAG,"%s.Exit",__func__);
    return err;
}

/*This function converts a given number of audio frames to the corresponding number of bytes.
 *This is useful for determining the size of a buffer needed to store a specific number of frames.
 */
uint32_t buffer_size_bytes(PlatformAudioDevices *har_device)
{
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }
    uint32_t buf_size;
    if (har_device) {
        if (har_device->format >= HAR_AUDIO_BIT_DEPTH_8BIT &&
                har_device->format <= HAR_AUDIO_BIT_DEPTH_64BIT) {
            pPvtData->audioBitDepth = har_device->format;
            HAR_PAL_VERBOSE(LOG_TAG, "audioBitDepth = %d", pPvtData->audioBitDepth);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio type configured.Setting to default");
            pPvtData->audioBitDepth = HAR_AUDIO_BIT_DEPTH_16BIT;
        }
        if (har_device->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_device->channels <= HAR_AUDIO_CHANNELS_32) {
            pPvtData->audioChannels = har_device->channels;
            HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", pPvtData->audioChannels);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio channels configured.Setting to default.");
            pPvtData->audioChannels = HAR_AUDIO_CHANNELS_2;
        }
    }

    pPvtData->frames = ((outBufCfg.buf_size * 8) /
                            (pPvtData->audioChannels * pPvtData->audioBitDepth));

    buf_size = ((pPvtData->frames * pPvtData->audioChannels) *
                            (pPvtData->audioBitDepth/8));

    HAR_PAL_VERBOSE(LOG_TAG,"%s.Exit %d frames %d buffer_size",__func__, pPvtData->frames, buf_size);
    free(pPvtData);
    pPvtData = NULL;
    return buf_size;
}

/*This function takes buffer size from buffer_size_bytes() and
 *determines the playback rate by multiplying buffer size with rate.
 */
uint32_t playback_rate(PlatformAudioDevices *har_device)
{
    uint32_t pb_rate = 0;
    if (har_device) {
        uint32_t buffer_size = buffer_size_bytes(&har_device);
        if (har_device->sample_rate >= HAR_AUDIO_RATE_8KHZ &&
                har_device->sample_rate <= HAR_AUDIO_RATE_192KHZ) {
            pb_rate = (buffer_size * har_device->sample_rate);
            HAR_PAL_VERBOSE(LOG_TAG, "audioRate = %d", har_device->sample_rate);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio rate configured.Setting to default");
            pb_rate = (buffer_size * HAR_AUDIO_RATE_48KHZ);
        }
    }

    HAR_PAL_VERBOSE(LOG_TAG,"%s.Exit %d rate",__func__, pb_rate);
    return pb_rate;
}

/*This function play a slice of bytes to the given device.
 *This further calls pal_stream_write.
 */
ssize_t try_play_bytes(PlatformAudioDevices *har_device, void *buffer, int bytes)
{
    ssize_t ret = 0;
    int err;
    struct pal_buffer palBuffer;
    err = pthread_mutex_lock(&lock);
    if (err !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      return err;
    }

    memset(&palBuffer, 0, sizeof(struct pal_buffer));
    palBuffer.buffer = (uint8_t*) buffer;
    palBuffer.size = (size_t)bytes;
    palBuffer.offset = 0;

    HAR_PAL_VERBOSE(LOG_TAG, "%s Buffer Size = %d", __func__, bytes);

    ret = pal_stream_write(pal_stream_handle, &palBuffer);

    err = pthread_mutex_unlock(&lock);
    if (err !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
      return err;
    }

    HAR_PAL_VERBOSE(LOG_TAG,"%s.Bytes Accepted = %d",__func__, ret);
    return ret;
}

/*This function sets volume of a particular stream.
 *This further calls pal_stream_set_volume to set volume.
 */
int32_t set_volume(PlatformAudioDevices *har_device, float volume)
{
    int ret = 0;
    uint32_t channels = HAR_AUDIO_CHANNELS_2;

    HAR_PAL_DBG(LOG_TAG,"%s.Enter",__func__);
    if (har_device) {
       if (har_device->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_device->channels <= HAR_AUDIO_CHANNELS_32) {
            channels = har_device->channels;
            HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", channels);
        }
    }

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      return ret;
    }

    if (volume) {
       struct pal_volume_data* volume_data = (struct pal_volume_data *) calloc(1,
                                          sizeof(uint32_t) + (channels * sizeof(struct
                                                     pal_channel_vol_kv)));
       if (volume_data == NULL) {
           HAR_PAL_ERR(LOG_TAG,"Unable to set volume");
           ret = pthread_mutex_unlock(&lock);
	   if (ret !=0) {
             HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
             return ret;
            }
           return -1;
       } else {
            if (channels == 1) {
                volume_data->no_of_volpair = 1;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = volume;
            } else {
                volume_data->no_of_volpair = 2;
                volume_data->volume_pair[0].channel_mask = PAL_CHMAP_CHANNEL_FL;
                volume_data->volume_pair[0].vol = volume;
                volume_data->volume_pair[1].channel_mask = PAL_CHMAP_CHANNEL_FR;
                volume_data->volume_pair[1].vol = volume;
            }

            ret = pal_stream_set_volume(pal_stream_handle, volume_data);

            if (ret) {
                HAR_PAL_ERR(LOG_TAG,"Pal Stream volume Error (%x)", ret);
            }
           free(volume_data);
           volume_data = NULL;
       }
    }
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    HAR_PAL_DBG(LOG_TAG,"%s.Exit ret %d",__func__, ret);
    return ret;
}

/*C implementation of close() API from Harry.
 *Further calls pal_stream_stop() and pal_stream_close().
 */
int32_t StopAndCloseUsecase(PlatformAudioDevices *har_device)
{
    int32_t status = 0;
    int ret = 0;

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      return ret;
    }
    HAR_PAL_DBG(LOG_TAG,"%s.Enter",__func__);

    if (pal_stream_handle) {
        status = pal_stream_stop(pal_stream_handle);
        if (status) {
            HAR_PAL_ERR(LOG_TAG,"pal_stream_stop failed");
        }
        status = pal_stream_close(pal_stream_handle);
        if (status) {
            HAR_PAL_ERR(LOG_TAG,"pal_stream_close failed");
        }

        stream_started = 0;

      pal_stream_handle = NULL;
    }
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    HAR_PAL_DBG(LOG_TAG,"%s.Exit",__func__);
    return status;
}

uint32_t set_spatialization(PlatformAudioDevices *har_device, Spatialization value) {
	int status = 0;
	HAR_PAL_DBG(LOG_TAG,"%s To be Implemented.WIP",__func__);
        return status;
}

uint32_t set_custom_parameter(PlatformAudioDevices *har_device, char *id, union value val) {
	int status = 0;
	HAR_PAL_DBG(LOG_TAG,"%s To be Implemented.WIP",__func__);
	return status;
}
