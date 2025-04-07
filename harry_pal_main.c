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
#include <cutils/properties.h>

static int pal_init_status = 0;
static pal_stream_element_t welcome_handler;
static pal_stream_element_t no_asil_handler;
static pal_stream_element_t road_adas_handler;
static pal_stream_element_t asil_handler;

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

pal_stream_element_t* get_stream_handler(int busid)
{
    HAR_PAL_VERBOSE(LOG_TAG, "%s: busid = %d",__func__, busid)
    switch (busid)
    {
        case HAR_PAL_DEVICE_OUT_SPEAKER_WS:
            return &welcome_handler;
        case HAR_PAL_DEVICE_OUT_SPEAKER_V_WARN:
            return &no_asil_handler;
        case HAR_PAL_DEVICE_OUT_SPEAKER_ROAD_ADAS:
            return &road_adas_handler;
        case HAR_PAL_DEVICE_OUT_SPEAKER_S_WARN:
            return &asil_handler;
        default:
            return NULL;
    }
}

const Spatialization requested_zone_index[] = {
    FrontLeft, FrontRight, FrontCenter, RearLeft,
    RearRight, Rear, Left, Right, Center
};

const Spatialization MAPPING_TABLE[16][NUM_REQUESTED_ZONES] = {
    //0000
    {None, None, None, None, None, None, None, None, None},
    //0001 (RR)
    {RearRight, RearRight, Rear, RearRight, RearRight, Rear, Right, Right, Center},
    //0010 (RL)
    {RearLeft, RearLeft, Rear, RearLeft, RearLeft, Rear, Left, Left, Center},
    //0011 (RL + RR)
    {RearLeft, RearRight, Rear, RearLeft, RearRight, Rear, Left, Right, Center},
    //0100 (FR)
    {FrontRight, FrontRight, FrontCenter, FrontRight, FrontRight, FrontCenter, Right, Right, Center},
    //0101 (FR + RR)
    {FrontRight, FrontRight, FrontCenter, RearRight, RearRight, Rear, Right, Right, Center},
    //0110 (FR + RL)
    {FrontRight, FrontRight, FrontCenter, RearLeft, RearLeft, Rear, Left, Right, Center},
    //0111 (FR + RL + RR)
    {FrontRight, FrontRight, FrontCenter, RearLeft, RearRight, Rear, Left, Right, Center},
    //1000 (FL)
    {FrontLeft, FrontLeft, FrontCenter, FrontLeft, FrontLeft, FrontCenter, Left, Left, Center},
    //1001 (FL + RR)
    {FrontLeft, FrontLeft, FrontCenter, RearRight, RearRight, Rear, Left, Right, Center},
    //1010 (FL + RL)
    {FrontLeft, FrontLeft, FrontCenter, RearLeft, RearLeft, Rear, Left, Left, Center},
    //1011 (FL + RL + RR)
    {FrontLeft, FrontLeft, FrontCenter, RearLeft, RearRight, Rear, Left, Right, Center},
    //1100 (FL + FR)
    {FrontLeft, FrontRight, FrontCenter, FrontLeft, FrontRight, FrontCenter, Left, Right, Center},
    //1101 (FL + FR + RR)
    {FrontLeft, FrontRight, FrontCenter, RearRight, RearRight, Rear, Left, Right, Center},
    //1110 (FL + FR + RL)
    {FrontLeft, FrontRight, FrontCenter, RearLeft, RearLeft, Rear, Left, Right, Center},
    //1111 (FL + FR + RL + RR)
    {FrontLeft, FrontRight, FrontCenter, RearLeft, RearRight, Rear, Left, Right, Center}
};

static int wait_for_snd_card_to_online()
{
    int ret = 0;
    int retries = MAX_RETRY;
    int fd = -1;
    char buf[12];
    snd_card_status_t card_status = SND_CARD_STATUS_NONE;

    /* wait here till snd card is registered                               */
    /* maximum wait period = (MAX_RETRY * RETRY_INTERVAL_US) micro-seconds */
    do {
        if ((fd = open(SNDCARD_PATH, O_RDWR)) < 0) {
            HAR_PAL_ERR(LOG_TAG,": Failed to open snd sysfs node, will retry for %d times ...", (retries - 1));
        } else {
            memset(buf , 0 ,sizeof(buf));
            lseek(fd,0L,SEEK_SET);
            read(fd, buf, 1);
            close(fd);
            fd = -1;

            buf[sizeof(buf) - 1] = '\0';
            card_status = SND_CARD_STATUS_NONE;
            sscanf(buf , "%u", &card_status);

            if (card_status == SND_CARD_STATUS_ONLINE) {
                HAR_PAL_DBG(LOG_TAG,": snd sysfs node open successful");
                break;
            }
        }
        retries--;
        sleep(RETRY_INTERVAL);
    } while ( retries > 0);

    if (0 == retries) {
        HAR_PAL_ERR(LOG_TAG,": Failed to open snd sysfs node, exiting ... ");
        ret = -EIO;
    }

    return ret;
}

static int32_t auto_audio_ar_ext_wait_agm(void)
{
    char agm_state[PROPERTY_VALUE_MAX];
    while (1) {
        property_get("init.svc.vendor.agm-1-0", agm_state, AGM_DEFAULT_STATE);
        if (!strcmp(agm_state, "running")) {
            break;
        } else {
            HAR_PAL_INFO(LOG_TAG,"wait agm for 10 ms");
            usleep(AGM_INIT_SLEEP_WAIT*1000);
        }
    }
    return 0;
}

//Function to convert integer to Spatialization enum
Spatialization int_to_enum(int value) {
    HAR_PAL_DBG(LOG_TAG, "%s: value = %d",__func__,value);
    switch (value) {
        case 0: return FrontCenter;
        case 1: return FrontLeft;
        case 2: return FrontRight;
        case 3: return Center;
        case 4: return Rear;
        case 5: return RearLeft;
        case 6: return RearRight;
        case 7: return Right;
        case 8: return Left;
        default: return FrontCenter;
    }
}

//Function to convert OutcomeZone enum to string
const char *outcome_zone_to_string(Spatialization zone) {
    const char *outcome_zone_string[NUM_REQUESTED_ZONES] = {
            "FrontCenter", "FrontLeft", "FrontRight", "Center",
            "Rear", "RearLeft", "RearRight", "Right", "Left"
     };
    if (zone == None)
      return "None";

    return outcome_zone_string[zone];
}

//Function to get column index based on spatialization enum value
int get_column_index(Spatialization requested_zone) {

    for(int i = 0; i < NUM_REQUESTED_ZONES; i++) {
       if (requested_zone_index[i] == requested_zone) {
          return i;
       }
    }
    HAR_PAL_ERR(LOG_TAG,":s, Invalid column index",__func__);
    return -1;
}

//Function to get Bitmask based on SpkrGrpAvail
int get_bit_mask(SpkrGrpAvail *zones, int count){
    int bitmask =0;
    int ret =0;

    for (int i = 0; i < count; i++) {
        if (strcmp(zones[i].zone, "FL") ==0)
           bitmask |= (zones[i].value << FL_BIT);
        else if (strcmp(zones[i].zone, "FR") ==0)
           bitmask |= (zones[i].value << FR_BIT);
        else if (strcmp(zones[i].zone, "RL") ==0)
           bitmask |= (zones[i].value << RL_BIT);
        else if (strcmp(zones[i].zone, "RR") ==0)
           bitmask |= (zones[i].value);
    }
    return bitmask;
}

//Function to parse input string and return an array of SpkrGrpAvail
SpkrGrpAvail* parse_speaker_group_availability(const char *input, int *num_zones) {
    *num_zones = 0;
    char *tokptr;
    char *buffer = malloc(strlen(input) + 1);
    if (!buffer){
        HAR_PAL_ERR(LOG_TAG,"Unable to allocate memory");
        return NULL;
    }

     //alocate list for total zones
    SpkrGrpAvail *spkrgrpList = malloc(NUM_ZONES*sizeof(SpkrGrpAvail));
    if (!spkrgrpList){
        HAR_PAL_ERR(LOG_TAG,"Unable to allocate memory");
        free(buffer);
        return NULL;
    }

    strlcpy(buffer,input,strlen(input)+1);
    HAR_PAL_DBG(LOG_TAG,"parse_volume_string strlcpy %s\n",buffer);
    char *segment = strtok_r(buffer,";",&tokptr);
    while (segment != NULL) {
         if(sscanf(segment, " %[^,],%d",spkrgrpList[*num_zones].zone,&spkrgrpList[*num_zones].value) == 2){
            HAR_PAL_DBG(LOG_TAG,"name %s , data = %d",
                       spkrgrpList[*num_zones].zone,spkrgrpList[*num_zones].value);
            (*num_zones)++;
        } else {
            HAR_PAL_ERR(LOG_TAG,"Invalid segment: %s\n",segment);
        }
        segment = strtok_r(NULL,";",&tokptr);
    }
    free(buffer);
    return spkrgrpList;
}

VolumeData* parse_volume_string(const char *input, int *count){
    *count = 0;
    char *tokptr;
    char *buffer = malloc(strlen(input) + 1);
    if (!buffer){
        HAR_PAL_ERR(LOG_TAG,"Unable to allocate memory");
        return NULL;
    }

    //alocate list for total devices
    VolumeData *volList = malloc(10*sizeof(VolumeData));
    if (!volList){
        HAR_PAL_ERR(LOG_TAG,"Unable to allocate memory");
        free(buffer);
        return NULL;
    }

    strlcpy(buffer,input,strlen(input)+1);
    HAR_PAL_DBG(LOG_TAG,"parse_volume_string strlcpy %s\n",buffer);
    char *segment = strtok_r(buffer,";",&tokptr);
    while(segment != NULL){
        if(sscanf(segment, " %[^,],%f",volList[*count].bus_addr,&volList[*count].volValue) == 2){
            HAR_PAL_DBG(LOG_TAG,"name %s , data = %f",
                        volList[*count].bus_addr,volList[*count].volValue);
            (*count)++;
        } else {
            HAR_PAL_ERR(LOG_TAG,"Invalid segment: %s\n",segment);
        }
        segment = strtok_r(NULL,";",&tokptr);
    }
    free(buffer);
    return volList;
}

int checkIfAWXBus(char* address, pal_awx_bus_type_t* bus_index)
{
    int bus_match = 0 ;

    if (NULL != bus_index)
    {
        // Assing the BUS To MAX address
        *bus_index = BUS_RESERVED;
        // Check if the bus address is a valid AWX BUS_ADDRESS
        for (int i=0; i<BUS_RESERVED ; i++)
        {
            HAR_PAL_DBG(LOG_TAG,"%s address %s index %d\n", __func__,address,i);
            if (strncmp((char *)bus_address[i], (char *)address,strlen(address)) == 0)
            {
                bus_match = 1;
                // Assing the index value and send it
                *bus_index =  (pal_awx_bus_type_t)(i);
                HAR_PAL_DBG(LOG_TAG,"%s address %s index %d\n", __func__,address,*bus_index);
                break;
            }
        }
    }
    HAR_PAL_DBG(LOG_TAG, "%s, bus_match = %d",__func__, bus_match);
    return bus_match;
}

//Handler function for MasterMute status
uint32_t handleMasterMute(VehicleDataType vehicledatatype) {

    int status = 0;
    int ret = 0;
    uint8_t* payloadInfo = NULL;
    pal_param_payload* pal_payload = NULL;
    effect_pal_payload_t* effect_payload = NULL;
    pal_effect_custom_payload_t* customPayload = NULL;
    Type2_param* MasterMute = NULL;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;

    HAR_PAL_DBG(LOG_TAG, "%s:VehicleDataType: Key = %s, Value  = %d",
             __func__,
             vehicledatatype.key,
             vehicledatatype.value.integer_data);


     ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
      return ret;
    }

    uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                                 sizeof(pal_effect_custom_payload_t) + sizeof(Type2_param);

    size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

    if (strcmp(vehicledatatype.key, "CYBER_MASTER_MUTE") == 0 ||
        strcmp(vehicledatatype.key, "TCU_MUTE") == 0 ||
        strcmp(vehicledatatype.key, "STATIC_POWER_LIMITATION") == 0 ||
        strcmp(vehicledatatype.key, "DELIVERY_MODE") == 0 ) {
        //allocate memory for payload
        payloadInfo = (uint8_t*) calloc(1, payload_size + padBytes);
        if (!payloadInfo) {
           HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
           ret = pthread_mutex_unlock(&lock);
           if (ret !=0) {
              HAR_PAL_ERR(LOG_TAG,":%s pthread_mutex_unlock failed", __func__);
              return ret;
           }
           return -ENOMEM;
        }

        //Creating payload
        pal_payload = (pal_param_payload*) payloadInfo;
        effect_payload = (effect_pal_payload_t*) (payloadInfo + sizeof(pal_param_payload));
        customPayload = (pal_effect_custom_payload_t*) (payloadInfo + sizeof(pal_param_payload) + sizeof(effect_pal_payload_t));

        pal_payload->payload_size = sizeof(effect_pal_payload_t) + sizeof(pal_effect_custom_payload_t)
                                    + sizeof(Type2_param);

        effect_payload->isTKV = PARAM_NONTKV;

        effect_payload->tag = TAG_MODULE_CUSTOM_AWX;
        effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) + sizeof(Type2_param);
        customPayload->paramId = AWX_CYBER_MASTER_MUTE;
        MasterMute = (Type2_param*)customPayload->data;
        MasterMute->value = vehicledatatype.value.integer_data;

        status = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) pal_payload, payload_size,
                              pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);

    } else {
        HAR_PAL_ERR(LOG_TAG,"Unknown VehicleDataType");
        status = -1;
        goto cleanup;
    }

cleanup:
    if (payloadInfo) {
        free(payloadInfo);
        payloadInfo = NULL;
    }
    pal_payload = NULL;
    effect_payload = NULL;
    customPayload = NULL;
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    if (status != 0) {
        HAR_PAL_ERR(LOG_TAG,"Set master mute failed with error: %d\n", status);
    } else {
        HAR_PAL_INFO(LOG_TAG,"Set Master Mute Sucessfull");
    }

    return status;
}

//Hanlder function to handle WelcomeSequence Status
uint32_t handleWelcomeSequence(VehicleDataType vehicledatatype) {

    int status = 0;
    int ret = 0;
    uint8_t* payloadInfo = NULL;
    pal_param_payload* pal_payload = NULL;
    effect_pal_payload_t* effect_payload = NULL;
    pal_effect_custom_payload_t* customPayload = NULL;
    Type2_param* WelcomeState = NULL;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;

    HAR_PAL_DBG(LOG_TAG, "%s:VehicleDataType: Key = %s, Value  = %d",
             __func__,
             vehicledatatype.key,
             vehicledatatype.value.integer_data);


     ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
      return ret;
    }

    uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                                 sizeof(pal_effect_custom_payload_t) + sizeof(Type2_param);;

    size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

    if (strcmp(vehicledatatype.key,"PLAYBACK_STATUS") == 0) {
        //allocate memory for payload
        payloadInfo = (uint8_t*) calloc(1, payload_size + padBytes);
        if (!payloadInfo) {
           HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
           ret = pthread_mutex_unlock(&lock);
           if (ret !=0) {
              HAR_PAL_ERR(LOG_TAG,":%s pthread_mutex_unlock failed", __func__);
              return ret;
           }
           return -ENOMEM;
        }

        //Creating payload
        pal_payload = (pal_param_payload*) payloadInfo;
        effect_payload = (effect_pal_payload_t*) (payloadInfo + sizeof(pal_param_payload));
        customPayload = (pal_effect_custom_payload_t*) (payloadInfo + sizeof(pal_param_payload) + sizeof(effect_pal_payload_t));

        pal_payload->payload_size = sizeof(effect_pal_payload_t) + sizeof(pal_effect_custom_payload_t) + sizeof(Type2_param);

        effect_payload->isTKV = PARAM_NONTKV;

        effect_payload->tag = TAG_MODULE_CUSTOM_AWX;
        effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) + sizeof(Type2_param);
        customPayload->paramId = AWX_WELCOME_MODE;
        WelcomeState = (Type2_param*)customPayload->data;
        WelcomeState->value = vehicledatatype.value.integer_data;

        status = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) pal_payload, payload_size,
                              pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);

    } else {
        HAR_PAL_ERR(LOG_TAG,"Unknown VehicleDataType");
        status = -1;
        goto cleanup;
    }

cleanup:
    if (payloadInfo) {
        free(payloadInfo);
        payloadInfo = NULL;
    }
    pal_payload = NULL;
    effect_payload = NULL;
    customPayload = NULL;
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    if (status != 0) {
        HAR_PAL_ERR(LOG_TAG,"Set welcome sequence failed with error: %d\n", status);
    } else {
        HAR_PAL_INFO(LOG_TAG,"Set Welcome Sequence Sucessfull");
    }

    return status;
}

//Handler function to handle restore volume
uint32_t handleRestoreVolume(VehicleDataType vehicledatatype) {

    int status = 0;
    int ret =0;
    float vol_mb = 0.0;
    const char* bus_address = NULL;
    const char *key = NULL;
    uint8_t* payloadInfo = NULL;
    pal_param_payload* pal_payload = NULL;
    effect_pal_payload_t* effect_payload = NULL;
    pal_effect_custom_payload_t* customPayload = NULL;
    pal_awx_data_t* RestoreVolume = NULL;
    pal_awx_bus_type_t bus_index = BUS_RESERVED;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;
    int count;

    key = vehicledatatype.key;

    HAR_PAL_DBG(LOG_TAG, "%s:key = %s string= %s",__func__,key,vehicledatatype.value.string_data);
    VolumeData *list = parse_volume_string(vehicledatatype.value.string_data,&count);
    HAR_PAL_DBG(LOG_TAG,"parse_volume_string after list\n");

    if(list) {
        ret = pthread_mutex_lock(&lock);
        if (ret !=0) {
            HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
            return ret;
        }

        uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                                    sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);

        size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

        if (strcmp(vehicledatatype.key,"RESTORE_VOLUME") == 0) {
            //allocate memory for payload
            payloadInfo = (uint8_t*) calloc(1, payload_size + padBytes);
            if (!payloadInfo) {
               HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
               ret = pthread_mutex_unlock(&lock);
               if (ret !=0) {
                  HAR_PAL_ERR(LOG_TAG,":%s pthread_mutex_unlock failed", __func__);
                  return ret;
               }
               return -ENOMEM;
            }

            //Creating payload
            pal_payload = (pal_param_payload*) payloadInfo;
            effect_payload = (effect_pal_payload_t*) (payloadInfo + sizeof(pal_param_payload));
            customPayload = (pal_effect_custom_payload_t*) (payloadInfo +
                                    sizeof(pal_param_payload) + sizeof(effect_pal_payload_t));

            pal_payload->payload_size = sizeof(effect_pal_payload_t) +
                              sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);

            effect_payload->isTKV = PARAM_NONTKV;

            effect_payload->tag = TAG_MODULE_CUSTOM_AWX;
            effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);
            customPayload->paramId = AWX_VOLUME;
            RestoreVolume = (pal_awx_data_t*)customPayload->data;

            for (int i = 0;i < count; i++) {
                bus_address = list[i].bus_addr;
                vol_mb = list[i].volValue;

                if (bus_address == NULL) {
                    HAR_PAL_ERR(LOG_TAG, "%s:Invalid Bus Address",__func__);
                    return -1;
                }

                int is_awx_bus = checkIfAWXBus(bus_address, &bus_index) && bus_index != BUS_RESERVED;

                HAR_PAL_DBG(LOG_TAG,
                  "%s:is_awx_bus = %d,VehicleDataType: Key = %s, Value: Bus_address = %s, Volume = %f",
                            __func__, is_awx_bus, vehicledatatype.key, bus_address,vol_mb);

                RestoreVolume->sub_func = ((uint16_t)1 << bus_index);
                RestoreVolume->reserved = 0;
                RestoreVolume->val[0] = vol_mb;

                status = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) pal_payload, payload_size,
                                  pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);
            }
        } else {
            HAR_PAL_ERR(LOG_TAG,"Unknown VehicleDataType");
            status = -1;
            goto cleanup;
        }
    } else {
        HAR_PAL_ERR(LOG_TAG,"list empty");
    }
cleanup:
    if (payloadInfo) {
        free(payloadInfo);
        payloadInfo = NULL;
    }
    pal_payload = NULL;
    effect_payload = NULL;
    customPayload = NULL;
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
        HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
        return ret;
    }

    if (status != 0) {
        HAR_PAL_ERR(LOG_TAG,"Restore Volume  failed with error: %d\n", status);
    } else {
        HAR_PAL_INFO(LOG_TAG,"Restore Volume Successful");
    }

    return status;

}

uint32_t handleSpkrVar(VehicleDataType vehicledatatype) {
    int ret =0;
    int num_kvp = 1;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
       HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
       return ret;
    }

    uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                           sizeof(pal_key_vector_t) + num_kvp * sizeof(pal_key_value_pair_t);
    size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

    uint8_t *payloadPtr = (uint8_t*) calloc(num_kvp, payload_size + padBytes);
    if (!payloadPtr) {
       HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
       ret = pthread_mutex_unlock(&lock);
       if (ret !=0) {
            HAR_PAL_ERR(LOG_TAG,":%s pthread_mutex_unlock failed %d", __func__,ret);
            return ret;
       }
       return -ENOMEM;
    }

    pal_param_payload *palPayload = (pal_param_payload *)payloadPtr;
    palPayload->payload_size = sizeof(effect_pal_payload_t) + sizeof(pal_key_vector_t) +
                               num_kvp * sizeof(pal_key_value_pair_t);

    effect_pal_payload_t *effectPalPayload =
            (effect_pal_payload_t *)(payloadPtr + sizeof(pal_param_payload));
    effectPalPayload->isTKV = PARAM_TKV;
    effectPalPayload->tag = TAG_MODULE_SPKR_VAR;
    effectPalPayload->payloadSize =
            sizeof(pal_key_vector_t) + num_kvp * sizeof(pal_key_value_pair_t);

    pal_key_vector_t *palKeyVector = (pal_key_vector_t *)(payloadPtr + sizeof(pal_param_payload) +
                                                          sizeof(effect_pal_payload_t));

    palKeyVector->num_tkvs = num_kvp;
    palKeyVector->kvp[0].key = TAG_MODULE_SPKR_VAR;
    //Need to change once key-value string is agreed with customer
    palKeyVector->kvp[0].value = DEFAULT_SPKR_VAR;

    ret = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) palPayload, payload_size,
                              pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);

    if (ret) {
        HAR_PAL_ERR(LOG_TAG,"pal_stream_set_param failed %d",ret);
    } else {
        HAR_PAL_INFO(LOG_TAG,"pal_stream_set_param Successful");
    }

    ret = pthread_mutex_unlock(&lock);
    if (ret !=0)
        HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);

    if (payloadPtr) {
        free(payloadPtr);
        payloadPtr = NULL;
    }
    palPayload = NULL;
    effectPalPayload = NULL;
    palKeyVector = NULL;
    return ret;
}

uint32_t handleSpkrVarFallbck(VehicleDataType vehicledatatype) {
    int ret =0;
    int num_kvp = 2;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;

    uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                           sizeof(pal_key_vector_t) + num_kvp * sizeof(pal_key_value_pair_t);
    size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

    uint8_t *payloadPtr = (uint8_t*) calloc(1, payload_size + padBytes);
    if (!payloadPtr) {
       HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
       return -ENOMEM;
    }

    pal_param_payload *palPayload = (pal_param_payload *)payloadPtr;
    palPayload->payload_size = sizeof(effect_pal_payload_t) + sizeof(pal_key_vector_t) +
                               num_kvp * sizeof(pal_key_value_pair_t);

    effect_pal_payload_t *effectPalPayload =
            (effect_pal_payload_t *)(payloadPtr + sizeof(pal_param_payload));
    effectPalPayload->isTKV = PARAM_TKV;
    effectPalPayload->tag = TAG_MODULE_SPKR_FALLBACK;
    effectPalPayload->payloadSize =
            sizeof(pal_key_vector_t) + num_kvp * sizeof(pal_key_value_pair_t);

    pal_key_vector_t *palKeyVector = (pal_key_vector_t *)(payloadPtr + sizeof(pal_param_payload) +
                                      sizeof(effect_pal_payload_t));

    palKeyVector->num_tkvs = num_kvp;
    palKeyVector->kvp[0].key = TAG_MODULE_SPKR_FALLBACK;
    palKeyVector->kvp[0].value = SPKR_FALLBACK_ON;
    palKeyVector->kvp[1].key = TAG_MODULE_SPKR_VAR;
    palKeyVector->kvp[1].value = DEFAULT_SPKR_VAR;

    ret = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) palPayload, payload_size,
                              pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);

    if (ret) {
        HAR_PAL_ERR(LOG_TAG,"pal_set_param failed for ASIL Bus: %d\n",ret);
    } else {
        HAR_PAL_INFO(LOG_TAG,"pal_set_param successful for ASIL Bus");
    }
    if (payloadPtr) {
        free(payloadPtr);
        payloadPtr = NULL;
    }
    palPayload = NULL;
    effectPalPayload = NULL;
    palKeyVector = NULL;
    return ret;
}

//Handler function to handle Spkr Group Availability
uint32_t handleSpkrGrpAvailability(VehicleDataType vehicledatatype) {

    int status = 0;
    int ret =0;
    int isLocked = 0;
    float vol_mb = 0.0;
    const char* bus_address = NULL;
    const char *key = NULL;
    uint8_t* payloadInfo = NULL;
    pal_param_payload* pal_payload = NULL;
    effect_pal_payload_t* effect_payload = NULL;
    pal_effect_custom_payload_t* customPayload = NULL;
    pal_awx_data_t* retrievedData = NULL;
    pal_awx_bus_type_t bus_index = BUS_RESERVED;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;
    int count;
    PlatformAudioDevices audio_device;

    key = vehicledatatype.key;

    HAR_PAL_DBG(LOG_TAG, "%s:key = %s string= %s",__func__,key,vehicledatatype.value.string_data);

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
       HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
       return ret;
    } else {
       isLocked = 1;
    }

     uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                                    sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);

     size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

     if (strcmp(vehicledatatype.key,"SPEAKER_GROUP_AVAILABILITY") == 0) {
        //allocate memory for payload
        payloadInfo = (uint8_t*) calloc(1, payload_size + padBytes);
        if (!payloadInfo) {
           HAR_PAL_ERR(LOG_TAG,":%s Memory allocation failure",__func__);
           ret = pthread_mutex_unlock(&lock);
           if (ret !=0) {
              HAR_PAL_ERR(LOG_TAG,":%s pthread_mutex_unlock failed", __func__);
              return ret;
           }
           return -ENOMEM;
     }

      SpkrGrpAvail *zones = parse_speaker_group_availability(vehicledatatype.value.string_data,&count);

       if (zones) {
         int bitmask = get_bit_mask(zones, count);
         if (bitmask == 0) {
          HAR_PAL_ERR(LOG_TAG,"%s: None of the speaker group available",__func__);
          status = -1;
          goto cleanup;
         } else if (bitmask > 0 && bitmask <= 3) {
            status = handleSpkrVarFallbck(vehicledatatype);
         } else {
            HAR_PAL_INFO(LOG_TAG,"%s:ASIL Bus Routing to Front Group Speaker",__func__);
         }

         if (status != 0) {
           HAR_PAL_ERR(LOG_TAG,"Speaker group routing failed for ASIL Bus: %d\n",status);
         } else {
           HAR_PAL_INFO(LOG_TAG,"Speaker group routing successful for ASIL Bus");
         }

        //Creating payload
        pal_payload = (pal_param_payload*) payloadInfo;
        effect_payload = (effect_pal_payload_t*) (payloadInfo + sizeof(pal_param_payload));
        customPayload = (pal_effect_custom_payload_t*) (payloadInfo +
                  sizeof(pal_param_payload) + sizeof(effect_pal_payload_t));

        pal_payload->payload_size = sizeof(effect_pal_payload_t) +
                          sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);

        effect_payload->isTKV = PARAM_NONTKV;

        effect_payload->tag = TAG_MODULE_CUSTOM_AWX;
        effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);
        customPayload->paramId = AWX_SPATIALIZATION;

        status = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) pal_payload, payload_size,
                            pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_READ, NULL);

        if (status != 0) {
           HAR_PAL_ERR(LOG_TAG,"%s: get param failed with error: %d\n",__func__, status);
           goto cleanup;
        }

        pal_awx_data_t* retrievedData = (pal_awx_data_t*)customPayload->data;
        int32_t value_VWarn = retrievedData->val[BUS_VWARN_AWX];
        int32_t value_RoadADAS = retrievedData->val[BUS_VROADADAS_AWX];
        Spatialization requested_zone_VWARN = int_to_enum(value_VWarn);
        HAR_PAL_INFO(LOG_TAG, "Requested Zone for VWARN : %s\n",
                        outcome_zone_to_string(requested_zone_VWARN));
        Spatialization requested_zone_RoadADAS = int_to_enum(value_RoadADAS);
        HAR_PAL_INFO(LOG_TAG, "Requested Zone for ROAD-ADAS : %s\n",
                        outcome_zone_to_string(requested_zone_RoadADAS));


        ret = pthread_mutex_unlock(&lock);
        if (ret !=0) {
          HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
          return ret;
        } else {
           isLocked = 0;
        }

        Spatialization outcome_VWARN = MAPPING_TABLE[bitmask][get_column_index(requested_zone_VWARN)];
        HAR_PAL_INFO(LOG_TAG, "Outcome Zone for VWARN :%d, %s\n",
                         outcome_VWARN, outcome_zone_to_string(outcome_VWARN));
        audio_device = load_default_config("WARN");
        status = set_spatialization(&audio_device, outcome_VWARN);

        Spatialization outcome_RoadADAS = MAPPING_TABLE[bitmask][get_column_index(requested_zone_RoadADAS)];
        HAR_PAL_INFO(LOG_TAG, "Outcome Zone for ROAD-ADAS:%d, %s\n",
                         outcome_RoadADAS, outcome_zone_to_string(outcome_RoadADAS));
        audio_device = load_default_config("ROAD-ADAS");
        status = set_spatialization(&audio_device, outcome_RoadADAS);
       } else {
            HAR_PAL_ERR(LOG_TAG,"list empty");
            status = -1;
            goto cleanup;
       }
     } else {
         HAR_PAL_ERR(LOG_TAG,"Unknown VehicleDataType");
         status = -1;
         goto cleanup;
     }

cleanup:
    if (payloadInfo) {
        free(payloadInfo);
        payloadInfo = NULL;
    }
    pal_payload = NULL;
    effect_payload = NULL;
    customPayload = NULL;

    if (isLocked){
        ret = pthread_mutex_unlock(&lock);
        if (ret !=0) {
            HAR_PAL_ERR(LOG_TAG," %s pthread_mutex_unlock failed", __func__);
            return ret;
        }
     }

    if (status != 0) {
        HAR_PAL_ERR(LOG_TAG,"Speaker group routing failed with error: %d\n", status);
    } else {
        HAR_PAL_INFO(LOG_TAG,"Speaker group routing successful");
    }

    return status;
}

//Function to register ID-handler mappings
void registerIdHandler(const char* id, void (*handler)(VehicleDataType)) {

    bool isRegistered = false;
    for(size_t i =0;i < idHandlerCount; i++) {
        if (strcmp(id, idHandlerMap[i].id) == 0) {
               isRegistered = true;
               break;
        }
    }

    if(!isRegistered) {
       if (idHandlerCount < sizeof(idHandlerMap)/sizeof(idHandlerMap[0])) {
           idHandlerMap[idHandlerCount].id = id;
           idHandlerMap[idHandlerCount].handler = handler;
           idHandlerCount++;
           HAR_PAL_DBG(LOG_TAG,"Registerd new Id = %s, Count = %d",id, idHandlerCount);
       } else {
           HAR_PAL_ERR(LOG_TAG,"IdHandlerMap is Full");
       }
    } else {
        HAR_PAL_DBG(LOG_TAG,"ID %s is already registered", id);
    }
}

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

    //Initialize the lookup table with known IDs
    registerIdHandler("AUDIO_PARAM_ID_MUTE", handleMasterMute);
    registerIdHandler("AUDIO_PARAM_ID_WS", handleWelcomeSequence);
    registerIdHandler("AUDIO_PARAM_ID_RES_VOL", handleRestoreVolume);
    registerIdHandler("AUDIO_PARAM_ID_SPKR", handleSpkrGrpAvailability);
    registerIdHandler("AUDIO_PARAM_ID_SPKR_VAR", handleSpkrVar);

    place_markers("kpi_value get_audio_device enter");
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
    place_markers("kpi_value get_audio_device exit");
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
        strlcpy(audio_device.bus_address, "BUS02_Road_ADAS", MAX_ADDRESS_LEN);
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

void get_buffer_size(pal_stream_element_t* handler, struct pal_stream_attributes *sattr) {

    int channels = sattr->out_media_config.ch_info.channels;
    int bytes_per_sample = audio_bytes_per_sample((audio_format_t)sattr->format);
    if (handler == &welcome_handler) {
        handler->outBufCfg.buf_count = PERIOD_COUNT;
        handler->outBufCfg.buf_size = DEEP_BUFFER_PERIOD_SIZE * bytes_per_sample * channels;
    } else if (handler == &no_asil_handler) {
        handler->outBufCfg.buf_count = PERIOD_COUNT;
        handler->outBufCfg.buf_size = LOW_LATENCY_PERIOD_SIZE * bytes_per_sample * channels;
    } else if (handler == &road_adas_handler) {
        handler->outBufCfg.buf_count = PERIOD_COUNT;
        handler->outBufCfg.buf_size = LOW_LATENCY_PERIOD_SIZE * bytes_per_sample * channels;
    } else if (handler == &asil_handler) {
        handler->outBufCfg.buf_count = PERIOD_COUNT;
        handler->outBufCfg.buf_size = LOW_LATENCY_PERIOD_SIZE * bytes_per_sample * channels;
    } else {
        HAR_PAL_ERR(LOG_TAG,"%s: Invalid handler. Setting default buffer config",__func__);
        handler->outBufCfg.buf_count = DEFAULT_BUF_COUNT;
        handler->outBufCfg.buf_size = DEFAULT_BUF_SIZE;
    }
}

/*Opens the given Audio Device, ensuring it is ready to play sound.
 *This function further calls pal_init(), pal_stream_open(),
 *pal_stream_get_buffer_size() and pal_stream_get_buffer_size().
 *Further it calls pal_stream_start() as well.
 */
int32_t Open_Device(PlatformAudioDevices *har_device) {

    int ret = -EINVAL;
    int isLocked = 0;
    int32_t err = 0;
    uint32_t no_of_devices = 1;
    har_pal_out_device_id_t outDeviceId = 0;
    pal_stream_element_t* stream_handler = NULL;

    pvt_data_t *pPvtData = calloc(1, sizeof(pvt_data_t));
    if (pPvtData == NULL) {
        HAR_PAL_ERR(LOG_TAG,"Memory Allocation Failed\n");
        return -1;
    }
    place_markers("kpi_value open_device enter");

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"%s pthread_mutex_lock failed", __func__);
      goto exit;
    }
    isLocked = 1;

    if (pal_init_status == 0) {
        ret = set_agm_status(0);
        if(ret) {
            HAR_PAL_ERR(LOG_TAG,"set_agm_status got failed ret = %d", ret);
        }
        ret = wait_for_snd_card_to_online();
          if (ret) {
             HAR_PAL_ERR(LOG_TAG,"Not found any SND card online\n");
             return ret;
           }
        auto_audio_ar_ext_wait_agm();
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

    HAR_PAL_DBG(LOG_TAG, "Calling pal_stream_open for device id =%d", har_device->device_id);
    stream_handler = get_stream_handler(har_device->device_id);
    if (stream_handler !=NULL) {
       err = pal_stream_open(&pPvtData->stream_attr, no_of_devices, &pPvtData->devices, 0, NULL,
                       NULL, 0,  &stream_handler->pal_stream_handle);
    } else {
        HAR_PAL_ERR(LOG_TAG, "Error: pal_stream_handle is NULL");
        goto exit;
    }
    if (err) {
        HAR_PAL_ERR(LOG_TAG, "Error:Failed to open RBVM  stream");
        goto exit;
    }

    HAR_PAL_DBG(LOG_TAG, "Stream Opened succesfully\n");

    get_buffer_size(stream_handler, &(pPvtData->stream_attr));

    HAR_PAL_DBG(LOG_TAG, "outBufSize %d outBufCount %d\n",
                  stream_handler->outBufCfg.buf_size , stream_handler->outBufCfg.buf_count);

    ret = pal_stream_set_buffer_size(stream_handler->pal_stream_handle, NULL, &stream_handler->outBufCfg);
    if (ret) {
        HAR_PAL_ERR(LOG_TAG,"Pal Stream set buffer size Error  (%x)\n", ret);
    }
    HAR_PAL_DBG(LOG_TAG, "%s Calling pal_stream_start",__func__);
    ret = pal_stream_start(stream_handler->pal_stream_handle);

    if (ret == 0)
           stream_started = 1;
    else {
        HAR_PAL_ERR(LOG_TAG,"Error:Failed to Start RBVM");
        goto exit;
    }

    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
        HAR_PAL_ERR(LOG_TAG," %s pthread_mutex_unlock failed", __func__);
        return ret;
    }
    isLocked = 0;

    if (outDeviceId == HAR_PAL_DEVICE_OUT_SPEAKER_S_WARN){
        VehicleDataType vehicledatatype = {
            .key = {0},
            .value.integer_data = 1
        };
        handleSpkrVar(vehicledatatype);
    }

exit:
    stream_started =0;
    free(pPvtData);
    pPvtData = NULL;
    if (isLocked){
        ret = pthread_mutex_unlock(&lock);
        if (ret !=0) {
            HAR_PAL_ERR(LOG_TAG," %s pthread_mutex_unlock failed", __func__);
            return ret;
        }
    }
    place_markers("kpi_value open_device exit");
    HAR_PAL_ERR(LOG_TAG,"%s.Exit",__func__);
    return err;
}

/*This function converts a given number of audio frames to the corresponding number of bytes.
 *This is useful for determining the size of a buffer needed to store a specific number of frames.
 */
uint32_t buffer_size_bytes(PlatformAudioDevices *har_device)
{
    int ret;
    pal_stream_element_t* stream_handler = NULL;
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

    stream_handler = get_stream_handler(har_device->device_id);
    if (stream_handler !=NULL) {
      pPvtData->frames = ((stream_handler->outBufCfg.buf_size * 8) /
                            (pPvtData->audioChannels * pPvtData->audioBitDepth));
    } else {
        HAR_PAL_ERR(LOG_TAG,"%s. stream handler is null. Setting to Default",__func__);
        pPvtData->frames = ((DEFAULT_BUF_SIZE * DEFAULT_BUF_COUNT * 8) /
                            (pPvtData->audioChannels * pPvtData->audioBitDepth));
    }

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
        uint32_t buffer_size = buffer_size_bytes(har_device);
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
    pal_stream_element_t* stream_handler = NULL;
    err = pthread_mutex_lock(&lock);
    if (err !=0) {
      HAR_PAL_ERR(LOG_TAG,"%s pthread_mutex_lock failed", __func__);
      return err;
    }

    memset(&palBuffer, 0, sizeof(struct pal_buffer));
    palBuffer.buffer = (uint8_t*) buffer;
    palBuffer.size = (size_t)bytes;
    palBuffer.offset = 0;

    HAR_PAL_VERBOSE(LOG_TAG, "%s Buffer Size = %d for device id=%d", __func__, bytes, har_device->device_id);
    stream_handler = get_stream_handler(har_device->device_id);
    if (stream_handler == NULL) {
        ret = -1;
        HAR_PAL_ERR(LOG_TAG,": %s pal_stream_handle is NULL", __func__);
        err = pthread_mutex_unlock(&lock);
        if (err !=0) {
            HAR_PAL_ERR(LOG_TAG,"%s pthread_mutex_lock failed", __func__);
            return err;
          }
        return ret;
    } else {
        if(get_agm_status()) {
            HAR_PAL_ERR(LOG_TAG,"%s: Agm is crashed.",__func__);
            ret = -EINVAL;
            goto exit;
        }
        ret = pal_stream_write(stream_handler->pal_stream_handle, &palBuffer);
    }
exit:
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
    pal_stream_element_t* stream_handler = NULL;

    HAR_PAL_DBG(LOG_TAG,"%s.Enter, volume = %f for device id=%d",__func__, volume, har_device->device_id);
    if (har_device) {
       if (har_device->channels >= HAR_AUDIO_CHANNELS_1 &&
                har_device->channels <= HAR_AUDIO_CHANNELS_32) {
            channels = har_device->channels;
            HAR_PAL_VERBOSE(LOG_TAG, "audioChannels = %d", channels);
        }
    }

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG," %s pthread_mutex_lock failed", __func__);
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
             HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
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
            stream_handler = get_stream_handler(har_device->device_id);
            if (stream_handler == NULL) {
                ret = -1;
                HAR_PAL_ERR(LOG_TAG,"%s no stream for device %d", __func__, har_device->device_id);
                free(volume_data);
                volume_data = NULL;
                pthread_mutex_unlock(&lock);
                return ret;
            } else {
               ret = pal_stream_set_volume(stream_handler->pal_stream_handle, volume_data);
            }
            if (ret) {
                HAR_PAL_ERR(LOG_TAG,"Pal Stream volume Error (%x)", ret);
            }
            free(volume_data);
            volume_data = NULL;
       }
    }
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
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
    int err;
    pal_stream_element_t* stream_handler = NULL;
    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_lock failed", __func__);
      return ret;
    }
    place_markers("kpi_value close_device enter");
    HAR_PAL_DBG(LOG_TAG,"%s.Enter for device id %d",__func__,har_device->device_id);
    stream_handler = get_stream_handler(har_device->device_id);
    if (stream_handler == NULL) {
        ret = -1;
        HAR_PAL_ERR(LOG_TAG," %s no stream for device %d", __func__, har_device->device_id);
        err = pthread_mutex_unlock(&lock);
        if (err !=0) {
            HAR_PAL_ERR(LOG_TAG,"%s pthread_mutex_lock failed", __func__);
            return err;
        }
        return ret;
    } else {
        HAR_PAL_DBG(LOG_TAG, "%s:pal_stream_handle = %p",__func__,stream_handler->pal_stream_handle);
        status = pal_stream_stop(stream_handler->pal_stream_handle);
        if (status) {
           HAR_PAL_ERR(LOG_TAG,"pal_stream_stop failed");
        }

        status = pal_stream_close(stream_handler->pal_stream_handle);
        if (status) {
           HAR_PAL_ERR(LOG_TAG,"pal_stream_close failed");
        }

        stream_started = 0;
        stream_handler->pal_stream_handle = NULL;
        stream_handler = NULL;
    }
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"%s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    HAR_PAL_DBG(LOG_TAG,"%s.Exit",__func__);
    place_markers("kpi_value close_device exit");
    return status;
}

/*C implementation of set_spatialization() API from Harry.
 *Further creates payload for spatialization and calls pal_gef_rw_param.
 */
uint32_t set_spatialization(PlatformAudioDevices *har_device, Spatialization value) {

    int status = 0;
    int ret = 0;
    char bus_addr[MAX_ADDRESS_LEN + 1];
    uint8_t* payloadInfo = NULL;
    pal_param_payload* pal_payload = NULL;
    effect_pal_payload_t* effect_payload = NULL;
    pal_effect_custom_payload_t* customPayload = NULL;
    pal_awx_data_t* spt_data = NULL;
    pal_awx_bus_type_t bus_index = BUS_RESERVED;
    pal_device_id_t pal_device = PAL_DEVICE_OUT_SPEAKER;

    if (har_device) {
        if (har_device->bus_address[0] !='\0') {
            strlcpy(bus_addr, har_device->bus_address,MAX_ADDRESS_LEN);

        } else {
            HAR_PAL_ERR(LOG_TAG, "%s : Invalid bus address",__func__);
            goto cleanup;
        }
    }

    //checking for AWX bus
    int is_awx_bus = checkIfAWXBus(bus_addr, &bus_index) && bus_index != BUS_RESERVED;

    //calculate payload_size
    uint32_t payload_size = sizeof(pal_param_payload) + sizeof(effect_pal_payload_t) +
                              sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);

    //assign default value as 0 (FrontCenter)
    if (value < FrontCenter || value > Left) {
        HAR_PAL_VERBOSE(LOG_TAG, "%s: Setting default value", __func__);
        value = FrontCenter;
    }

    HAR_PAL_DBG(LOG_TAG,"%s:Spatialization= %d,bus_addr= %s,awx_bus= %d,bus_idx= %d,payloadsize=%d",
              __func__,
              value,
              bus_addr,
              is_awx_bus,
              bus_index,
              payload_size);

    size_t padBytes = PADDING_8BYTE_ALIGN(payload_size);

    ret = pthread_mutex_lock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,"HAR_PAL: %s pthread_mutex_lock failed", __func__);
      return ret;
    }

    //allocate memory for payload
    payloadInfo = (uint8_t*) calloc(1, payload_size + padBytes);
    if (!payloadInfo) {
        HAR_PAL_ERR(LOG_TAG,": %s Memory allocation failure",__func__);
        ret = pthread_mutex_unlock(&lock);
        if (ret !=0) {
           HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
           return ret;
         }
        return -ENOMEM;
    }

    //Creating payload
    pal_payload = (pal_param_payload*) payloadInfo;
    effect_payload = (effect_pal_payload_t*) (payloadInfo + sizeof(pal_param_payload));
    customPayload = (pal_effect_custom_payload_t*) (payloadInfo + sizeof(pal_param_payload)
                       + sizeof(effect_pal_payload_t));

    pal_payload->payload_size = sizeof(effect_pal_payload_t) + sizeof(pal_effect_custom_payload_t)
                                  + sizeof(pal_awx_data_t);

    effect_payload->isTKV = PARAM_NONTKV;

    // check for AWX BUS to handle spatialization appropriately
    if (is_awx_bus) {
        effect_payload->tag = TAG_MODULE_CUSTOM_AWX;
        effect_payload->payloadSize = sizeof(pal_effect_custom_payload_t) + sizeof(pal_awx_data_t);
        customPayload->paramId = AWX_SPATIALIZATION;
        spt_data = (pal_awx_data_t*)customPayload->data;
        spt_data->sub_func = ((uint16_t)1 << bus_index);
        spt_data->reserved = 0;
        spt_data->val[0] = value;
        HAR_PAL_VERBOSE(LOG_TAG,"AWX Bus idx= %d,bus_addr= %s,awx_bus= %d,paramId= %x,tagid= %x",
                  bus_index,
                  bus_addr,
                  is_awx_bus,
                  customPayload->paramId,
                  effect_payload->tag);
    } else {
        HAR_PAL_INFO(LOG_TAG, "Skip Spatialization for bus_addr = %s", bus_addr);
        goto cleanup;
    }

    status = pal_gef_rw_param(PAL_PARAM_ID_UIEFFECT, (void *) pal_payload, payload_size,
                              pal_device, PAL_STREAM_PLAYBACK_BUS, GEF_PARAM_WRITE, NULL);
cleanup:
    if (payloadInfo) {
        free(payloadInfo);
        payloadInfo = NULL;
    }
    pal_payload = NULL;
    effect_payload = NULL;
    customPayload = NULL;
    spt_data = NULL;
    ret = pthread_mutex_unlock(&lock);
    if (ret !=0) {
      HAR_PAL_ERR(LOG_TAG,": %s pthread_mutex_unlock failed", __func__);
      return ret;
    }

    if (status != 0) {
        HAR_PAL_ERR(LOG_TAG,"Error setting spatialization with error: %d\n", status);
    } else {
        HAR_PAL_INFO(LOG_TAG,"Set Spatialization successful");
    }

    return status;
}

uint32_t set_custom_parameter(const char *id, VehicleDataType vehicledatatype) {

    uint32_t result = 0;

    if (id == NULL || strlen(id) == 0) {
       HAR_PAL_ERR(LOG_TAG,"Invalid ID");
       return -1;
    }
    HAR_PAL_DBG(LOG_TAG,"%s : ID = %s",__func__, id);
    //Lookup the ID in the lookup table
    for (size_t i =0; i < idHandlerCount; i++) {
        if (strcmp(id, idHandlerMap[i].id) == 0) {
            //Call corresponding handler and get its return value
            result = idHandlerMap[i].handler(vehicledatatype);
            break;
        }
    }

    if (result != 0) {
        HAR_PAL_ERR(LOG_TAG, "set_custom_parameter failed with error: %d", result);
    } else {
        HAR_PAL_DBG(LOG_TAG, "set_custom_parameter successful");
    }

    return result;
}
