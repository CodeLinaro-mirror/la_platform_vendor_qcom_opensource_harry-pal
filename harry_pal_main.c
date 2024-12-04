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

#define INITIAL_CAPACITY 10

static pal_stream_handle_t *pal_stream_handle;
struct pal_buffer_config outBufCfg = {0, 0, 0};
static int pal_init_status = 0;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t stream_started = 0;

typedef struct {
    /////////////
    // Config data
    /////////////

    // ALSA Device name
    char *pDeviceName;

    // map_har_audio_rate
    har_audio_rate_t audioRate;

    // map_har_audio_bit_depth
    har_audio_bit_depth_t audioBitDepth;

    // map_har_channels
    har_audio_channels_t audioChannels;

    uint32_t frames;

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

    uint32_t deviceId;
    uint32_t cardId;

    uint32_t alsaStartThreshold;
    uint32_t alsaStopThreshold;

    float volume_level;
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

void init_vector(IntVector *vec) {
    vec->size = 0;
    vec->capacity = INITIAL_CAPACITY;
    vec->data = (pal_device_t*)malloc(vec->capacity * sizeof(pal_device_t));
    if (vec->data == NULL) {
        HAR_PAL_ERR(LOG_TAG, "Failed to allocate memory");
        exit(1);
    }
}

void resize_vector(IntVector *vec) {
    vec->capacity *= 2;
    vec->data = (pal_device_t *)realloc(vec->data, vec->capacity * sizeof(pal_device_t));
    if (vec->data == NULL) {
        HAR_PAL_ERR(LOG_TAG, "Failed to reallocate memory");
        exit(1);
    }
}

void add_element(IntVector *vec, const pal_device_t *value) {
    if (vec->size == vec->capacity) {
        resize_vector(vec);
    }
    vec->data[vec->size] = *value;
    vec->size++;
}

void free_vector(IntVector *vec) {
    free(vec->data);
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
}

void XMLCALL startElement(void *userData, const char *name, const char **attrs) {
    XMLData *data = (XMLData *)userData;
    memset(data->temp_data, 0, sizeof(data->temp_data));
    strlcpy(data->currentElement, name, sizeof(data->currentElement));
    HAR_PAL_VERBOSE(LOG_TAG, "Start element: %s", name);
    if (strcmp(name, "device") ==0 ) {
        memset(&data->currentDevice, 0, sizeof(data->currentDevice));
    }
}

void XMLCALL endElement(void *userData, const char *name) {
    XMLData *data = (XMLData *)userData;
    HAR_PAL_VERBOSE(LOG_TAG, "End Element: %s", name);
    if (strcmp(name, "device") == 0) {
        if (data->mode == 1) {
            HAR_PAL_VERBOSE(LOG_TAG,"Ending device id= %d",data->currentDevice.id);
            add_element(data->vec, &data->currentDevice);
        }
        else if (data->mode == 2 && data->currentDevice.id == data->tid) {
           if (data->bus_address_found == 0) {
               HAR_PAL_VERBOSE(LOG_TAG," device id= %d",data->currentDevice.id);
               data->bus_address_found = 1;
               add_element(data->vec, &data->currentDevice);
               XML_StopParser(data->parser, XML_FALSE);
           }
       }
    }

    memset(data->currentElement, 0, sizeof(data->currentElement));
}

void XMLCALL characterDataHandler(void *userData, const XML_Char *s, int len) {
    XMLData *data = (XMLData *)userData;
     if(len >= sizeof(data->temp_data)) {
                len = sizeof(data->temp_data) - 1;
        }

    strlcpy(data->temp_data, s, len + 1);
    if (strcmp(data->currentElement, "name") == 0) {
    memset(data->currentDevice.name, 0, sizeof(data->currentDevice.name));
    strlcpy(data->currentDevice.name, data->temp_data, sizeof(data->currentDevice.name));
    HAR_PAL_VERBOSE(LOG_TAG,"Parsed name= %s",data->currentDevice.name);
    } else if (strcmp(data->currentElement, "id") == 0) {
        data->currentDevice.id = atoi(data->temp_data);
        HAR_PAL_VERBOSE(LOG_TAG,"Parsed device id= %d",data->currentDevice.id);
    } else if (strcmp(data->currentElement, "Bus_Address") == 0) {
        if (data->currentDevice.id == data->tid) {
            memset(data->currentDevice.bus_address, 0,sizeof(data->currentDevice.bus_address));
            strlcpy(data->currentDevice.bus_address, data->temp_data,
                    sizeof(data->currentDevice.bus_address));
            HAR_PAL_VERBOSE(LOG_TAG,"Parsed Bus_address= %s",data->currentDevice.bus_address);
        }
    }
    memset(data->temp_data, 0, sizeof(data->temp_data));
}

/*PAL Devices are maintained in an xml file – device_manager.xml.
 *This functions further calls parse_device_id() which takes XML file as an input,
 *parses the same and returns the available devices list.
 */
IntVector available_devices() {
    const char *filename = HAR_PAL_DEVICE;
    HAR_PAL_DBG(LOG_TAG, "%s Enter",__func__);
    return parse_device_id(filename);
}

/*New API Introduced in PDK21.
 *This function parses and returns an AudioDevice for a specific device_id.
 */
struct PlatformAudioDevices get_audio_device(const char* device_id) {
    struct PlatformAudioDevices audio_device;
    audio_device.channels = 1;
    int id = -1;
    IntVector devices = available_devices();
    pthread_mutex_lock(&lock);

    if (strcmp("BUS00_WS",device_id) == 0) {
        HAR_PAL_ERR(LOG_TAG,"get_audio_device id= BUS00_WS");
        audio_device.channels = 2;
        id = 2;
    } else if (strcmp("BUS01_no_ASIL",device_id) == 0) {
        HAR_PAL_ERR(LOG_TAG,"get_audio_device id= BUS01_no_ASIL");
        id = 3;
    }else if (strcmp("BUS02_Road_ADAS",device_id) == 0) {
        HAR_PAL_ERR(LOG_TAG,"get_audio_device id= BUS02_Road_ADAS");
        id = 4;
    }else if (strcmp("BUS03_ASIL",device_id) == 0) {
        HAR_PAL_ERR(LOG_TAG,"get_audio_device id= BUS03_ASIL");
        id = 5;
    }

    strlcpy(audio_device.bus_address, device_id, MAX_ADDRESS_LEN);
    audio_device.AudioDeviceId = id;
    audio_device.card = 0;
    audio_device.rate = 48000;
    audio_device.format = 16;
    audio_device.start_threshold = 0;
    audio_device.stop_threshold = 0;
    audio_device.silence_threshold = 0;
    audio_device.silence_size = 0;
    audio_device.avail_min = 0;

    pthread_mutex_unlock(&lock);

    return audio_device;
}

IntVector parse_device_id(const char* filename) {
    IntVector vec;
    init_vector(&vec);

    XMLData data;
    data.vec = &vec;
    data.mode = 1;
    data.currentElement[0] = '\0';

    pthread_mutex_lock(&lock);

    // Create and configure the XML parser
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, &data);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterDataHandler);

    FILE *file = fopen(filename, "r");
    if (!file) {
        HAR_PAL_ERR(LOG_TAG, "Cannot open file: %s", filename);
        XML_ParserFree(parser);
        pthread_mutex_unlock(&lock);
        exit(EXIT_FAILURE);
    }

    // Read the file and parse it
    char buffer[BUFSIZ];
    size_t len;
    while ((len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (XML_Parse(parser, buffer, len, XML_FALSE) == XML_STATUS_ERROR) {
            HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
            XML_ParserFree(parser);
            fclose(file);
            free_vector(data.vec);
            free(data.vec);
            pthread_mutex_unlock(&lock);
            exit(EXIT_FAILURE);
       }
    }
    // Finalize Parsing
    if (XML_Parse(parser, NULL, 0, XML_TRUE) == XML_STATUS_ERROR) {
        HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s\n", XML_ErrorString(XML_GetErrorCode(parser)));
    }

    fclose(file);
    XML_ParserFree(parser);
    pthread_mutex_unlock(&lock);
    return vec;
}

//Function to parse and return bus address for a specific device_id
char* parse_bus_address(const char *filename, int target_id) {

   FILE *file = fopen(filename, "r");
   if (!file) {
       HAR_PAL_ERR(LOG_TAG, "Error opening file");
       return NULL;
   }
   // Initialize the XMLData structure
   XMLData data;
   data.vec = (IntVector *)malloc(sizeof(IntVector));
   init_vector(data.vec);
   data.mode = 2;
   data.tid = target_id;
   data.bus_address_found = 0;

   // Create and configure the XML parser
   XML_Parser parser = XML_ParserCreate(NULL);
   XML_SetUserData(parser, &data);
   XML_SetElementHandler(parser, startElement, endElement);
   XML_SetCharacterDataHandler(parser, characterDataHandler);
   // Read the file and parse it
   char buffer[BUFSIZ];
   size_t len;

   while ((len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
       if (XML_Parse(parser, buffer, len, XML_FALSE) == XML_STATUS_ERROR) {
           HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
           XML_ParserFree(parser);
           fclose(file);
           free_vector(data.vec);
           free(data.vec);
           return NULL;
       }
       // Check if the bus address was found
       if (data.bus_address_found) {
           break;
       }
   }
   // Finalize Parsing
   if (XML_Parse(parser, NULL, 0, XML_TRUE) == XML_STATUS_ERROR) {
       HAR_PAL_ERR(LOG_TAG, "XML Parse error: %s\n", XML_ErrorString(XML_GetErrorCode(parser)));
   }
   // Return the found bus address, or NULL if not found
   char *bus_address = NULL;
   if (data.vec->size > 0) {
       bus_address = (char *)malloc(MAX_ADDRESS_LEN);
       if (bus_address) {
           strlcpy(bus_address, data.vec->data[0].bus_address, MAX_ADDRESS_LEN);
       }
   }
    // Clean up
    XML_ParserFree(parser);
    fclose(file);
    free_vector(data.vec);
    free(data.vec);
    return bus_address;
}

/*Opens the given Audio Device, ensuring it is ready to play sound.
 *This function further calls pal_init(), pal_stream_open(),
 *pal_stream_get_buffer_size() and pal_stream_get_buffer_size().
 *Further it calls pal_stream_start() as well.
 */
uint32_t Open_Device(struct PlatformAudioDevices *har_pal) {

    int ret = -EINVAL;
    uint32_t err;
    uint32_t errval =0;
    uint32_t no_of_devices = 1;
    har_pal_out_device_id_t outDeviceId = 0;
    const char *filename = HAR_PAL_DEVICE;
    char *bus_addr = NULL;

    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }

    pthread_mutex_lock(&lock);
    if (pal_init_status == 0) {
        err = pal_init();
        if (err) {
            HAR_PAL_ERR(LOG_TAG,"pal_init failed");
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

    outDeviceId = har_pal->AudioDeviceId;
    if (outDeviceId < 0 || outDeviceId > 7) {
        HAR_PAL_ERR(LOG_TAG,"har_pal_out_device_id has to be in between 0 to 7."
                "setting the default value as 3");
        outDeviceId = 2;
    }

    HAR_PAL_VERBOSE(LOG_TAG, "har_pal_out_device_id = %d", outDeviceId);
    get_out_device_id(outDeviceId, &(pPvtData->devices));
    pPvtData->deviceId = pPvtData->devices.id;

    if (har_pal->rate >= HAR_AUDIO_RATE_8KHZ &&
                har_pal->rate <= HAR_AUDIO_RATE_192KHZ) {
        pPvtData->audioRate = har_pal->rate;
        HAR_PAL_VERBOSE(LOG_TAG, "audioRate = %d", pPvtData->audioRate);
    } else {
        HAR_PAL_ERR(LOG_TAG, "Invalid audio rate configured.Setting to default");
        pPvtData->audioRate = HAR_AUDIO_RATE_48KHZ;
    }

    if (har_pal->format >= HAR_AUDIO_BIT_DEPTH_8BIT &&
                har_pal->format <= HAR_AUDIO_BIT_DEPTH_64BIT) {
        pPvtData->audioBitDepth = har_pal->format;
        HAR_PAL_VERBOSE(LOG_TAG, "audioBitDepth = %d", pPvtData->audioBitDepth);
    } else {
        HAR_PAL_ERR(LOG_TAG, "Invalid audio type configured.Setting to default");
        pPvtData->audioBitDepth = HAR_AUDIO_BIT_DEPTH_16BIT;
    }

    if (har_pal->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_pal->channels <= HAR_AUDIO_CHANNELS_32) {
        pPvtData->audioChannels = har_pal->channels;
        HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", pPvtData->audioChannels);
    } else {
        HAR_PAL_ERR(LOG_TAG, "Invalid audio channels configured.Setting to default.");
        pPvtData->audioChannels = HAR_AUDIO_CHANNELS_2;
    }

    //Setting Stream Attributes
    memset(&pPvtData->stream_attr, 0, sizeof(pPvtData->stream_attr));
    pPvtData->stream_attr.type = PAL_STREAM_PLAYBACK_BUS;
    switch (outDeviceId) {
        case 2:
            bus_addr = parse_bus_address(filename, outDeviceId);
            break;
        case 3:
            bus_addr = parse_bus_address(filename, outDeviceId);
            break;
        case 4:
            bus_addr = parse_bus_address(filename, outDeviceId);
            break;
        case 5:
            bus_addr = parse_bus_address(filename, outDeviceId);
            break;
        default:
            HAR_PAL_ERR(LOG_TAG,"Invalid output device id");
            break;
    }

     if (bus_addr) {
         pPvtData->stream_attr.bus_addr = bus_addr;
         HAR_PAL_DBG(LOG_TAG, "Bus Address for device ID %d: %s", outDeviceId, bus_addr);
     } else {
         HAR_PAL_ERR(LOG_TAG, "Bus Address for device ID %d not found", outDeviceId);
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
        HAR_PAL_INFO(LOG_TAG,"Pal get buffer size failed (%x).Setting Default", ret);
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

    HAR_PAL_VERBOSE(LOG_TAG, "%s Calling pal_stream_start",__func__);
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
    free(bus_addr);
    free(pPvtData);
    pPvtData = NULL;
    pthread_mutex_unlock(&lock);
    HAR_PAL_DBG(LOG_TAG,"%s.Exit",__func__);
    return err;
}

/*This function converts a given number of audio frames to the corresponding number of bytes.
 *This is useful for determining the size of a buffer needed to store a specific number of frames.
 */
uint32_t buffer_size_bytes(struct PlatformAudioDevices *har_pal)
{
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }
    uint32_t buf_size;
    uint32_t frame_size;
    if (har_pal) {
        if (har_pal->format >= HAR_AUDIO_BIT_DEPTH_8BIT &&
                har_pal->format <= HAR_AUDIO_BIT_DEPTH_64BIT) {
            pPvtData->audioBitDepth = har_pal->format;
            HAR_PAL_VERBOSE(LOG_TAG, "audioBitDepth = %d", pPvtData->audioBitDepth);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio type configured.Setting to default");
            pPvtData->audioBitDepth = HAR_AUDIO_BIT_DEPTH_16BIT;
        }
        if (har_pal->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_pal->channels <= HAR_AUDIO_CHANNELS_32) {
            pPvtData->audioChannels = har_pal->channels;
            HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", pPvtData->audioChannels);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio channels configured.Setting to default.");
            pPvtData->audioChannels = HAR_AUDIO_CHANNELS_2;
        }
    }

    pPvtData->frames = ((outBufCfg.buf_size * 8) /
                            (pPvtData->audioChannels * pPvtData->audioBitDepth));
    frame_size = pPvtData->frames;

    buf_size = ((pPvtData->frames * pPvtData->audioChannels) *
                            (pPvtData->audioBitDepth/8));

    HAR_PAL_VERBOSE(LOG_TAG,"%s.Exit %d frames %d buffer_size",__func__, frame_size, buf_size);
    free(pPvtData);
    pPvtData = NULL;
    return buf_size;
}

/*This function takes buffer size from buffer_size_bytes() and
 *determines the playback rate by multiplying buffer size with rate.
 */
uint32_t playback_rate(struct PlatformAudioDevices *har_pal)
{
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }
    if (har_pal) {
        if (har_pal->rate >= HAR_AUDIO_RATE_8KHZ &&
                har_pal->rate <= HAR_AUDIO_RATE_192KHZ) {
            pPvtData->audioRate = har_pal->rate;
            HAR_PAL_VERBOSE(LOG_TAG, "audioRate = %d", pPvtData->audioRate);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio rate configured.Setting to default");
            pPvtData->audioRate = HAR_AUDIO_RATE_48KHZ;
        }
    }
    uint32_t buffer_size = buffer_size_bytes(&har_pal);
    uint32_t pb_rate = (buffer_size * pPvtData->audioRate);

    HAR_PAL_VERBOSE(LOG_TAG,"%s.Exit %d rate",__func__, pb_rate);
    free(pPvtData);
    pPvtData = NULL;
    return pb_rate;
}

/*This function play a slice of bytes to the given device.
 *This further calls pal_stream_write.
 */
ssize_t try_play_bytes(struct PlatformAudioDevices *har_pal, void *buffer, int bytes)
{
    ssize_t ret = 0;
    uint32_t status = 0;
    uint32_t errval = 0;
    struct pal_buffer palBuffer;
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }
    pthread_mutex_lock(&lock);

    memset(&palBuffer, 0, sizeof(struct pal_buffer));
    palBuffer.buffer = (uint8_t*) buffer;
    palBuffer.size = (size_t)bytes;
    palBuffer.offset = 0;

    HAR_PAL_VERBOSE(LOG_TAG, "%s Buffer Size = %d", __func__, bytes);

    ret = pal_stream_write(pal_stream_handle, &palBuffer);
    pthread_mutex_unlock(&lock);
    HAR_PAL_VERBOSE(LOG_TAG,"%s.Bytes Accepted = %d",__func__, ret);
    free(pPvtData);
    pPvtData = NULL;
    return ret;
}

/*This function sets volume of a particular stream.
 *This further calls pal_stream_set_volume to set volume.
 */
int32_t set_volume(struct PlatformAudioDevices *har_pal, float volume)
{
    int ret = 0;
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
      if (pPvtData == NULL) {
            HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
            return -1;
      }

    HAR_PAL_DBG(LOG_TAG,"%s.Enter",__func__);
    if (har_pal) {
       if (har_pal->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_pal->channels <= HAR_AUDIO_CHANNELS_32) {
            pPvtData->audioChannels = har_pal->channels;
            HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", pPvtData->audioChannels);
        } else {
            HAR_PAL_ERR(LOG_TAG, "Invalid audio channels configured.Setting to default.");
            pPvtData->audioChannels = HAR_AUDIO_CHANNELS_2;
        }
    }
    pthread_mutex_lock(&lock);
    if (volume) {
       struct pal_volume_data* volume_data = (struct pal_volume_data *) calloc(1,
                                          sizeof(uint32_t) + (pPvtData->audioChannels * sizeof(struct
                                                     pal_channel_vol_kv)));
       if (volume_data == NULL) {
           HAR_PAL_ERR(LOG_TAG,"Unable to set volume");
           return -1;
       } else {
            if (pPvtData->audioChannels == 1) {
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
    pthread_mutex_unlock(&lock);
    HAR_PAL_DBG(LOG_TAG,"%s.Exit ret %d",__func__, ret);
    free(pPvtData);
    pPvtData = NULL;
    return ret;
}

/*C implementation of close() API from Harry.
 *Further calls pal_stream_stop() and pal_stream_close().
 */
int32_t StopAndCloseUsecase(struct PlatformAudioDevices *har_pal)
{
    int32_t status = 0;
    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
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
    free(pPvtData);
    pPvtData = NULL;
    (void) har_pal;
    HAR_PAL_DBG(LOG_TAG,"%s.Exit",__func__);
    return status;
}