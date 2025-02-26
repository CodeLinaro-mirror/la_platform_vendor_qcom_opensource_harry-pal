/*
* Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/
/*
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/


#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <expat.h>
#include "harry_pal_main.h"
#include "harry_pal_common.h"

static int streamClose = 0;

PlatformAudioDevices platform_device;

void stream_close(int sig)
{
    /* allow the stream to be closed gracefully */
    signal(sig, SIG_IGN);
    streamClose = 1;
}

Spatialization stringToEnum(const char *str) {
    if (strcmp(str, "FC") == 0) {
        return FrontCenter;
    } else if (strcmp(str, "FL") == 0) {
        return FrontLeft;
    } else if (strcmp(str, "FR") == 0) {
        return FrontRight;
    } else if (strcmp(str, "C") == 0) {
        return Center;
    } else if (strcmp(str, "RC") == 0) {
        return Rear;
    } else if (strcmp(str, "RL") == 0) {
        return RearLeft;
    } else if (strcmp(str, "RR") == 0) {
        return RearRight;
    } else if (strcmp(str, "R") == 0) {
        return Right;
    } else if (strcmp(str, "L") == 0) {
        return Left;
    }
    else {
       return Center;
    }
}

void sample_write(PlatformAudioDevices *platform_device,FILE *file, struct chunk_fmt fmt)
{
    char *buffer;
    uint32_t size;
    size_t num_read;
    int32_t written;
    uint32_t bytes;

    fprintf(stdout, "Enabling write\n");
    place_markers("kpi_value HarPalTest start playback");

    size = buffer_size_bytes(platform_device);
    fprintf(stdout, "size = %d\n",size);
    buffer = malloc(size);

    if (!buffer) {
        printf("Unable to allocate %d size\n", size);
        fclose(file);
    }

    memset(buffer, 0 , size);
    bytes = size/4;
    fprintf(stdout, "bytes = %d\n",bytes);

    playback_rate(platform_device);

    /* catch ctrl-c to shutdown cleanly */
    signal(SIGINT, stream_close);

    do {
        num_read = fread(buffer, 1, bytes, file);
        if (num_read > 0) {
            written = try_play_bytes(platform_device,buffer,bytes);
            if (written < 0) {
                fprintf(stdout, "Write Failed\n");
                break;
            } else if (written != num_read) {
                fprintf(stdout, "Partial write: expected %zu, wrote %d\n", num_read, written);
            }
        }
    } while (!streamClose && num_read > 0);

    free(buffer);
    place_markers("kpi_value HarPalTest done playing");
    fprintf(stdout, "Done Playing.\n");
}

void disable_usecase()
{
    int status =0;
    fprintf(stdout,"Enter Disable_usecase()\n");
    status = StopAndCloseUsecase(&platform_device);
    if(status) {
        fprintf(stdout, "StopAndCloseUsecase failed\n");
    }
    fprintf(stdout,"Exit Disable_usecase()\n");
    streamClose = 1;
}


int main(int argc, char *argv[])
{
    FILE *file;
    struct riff_wave_header riff_wave_header;
    struct chunk_header chunk_header;
    struct chunk_fmt chunk_fmt;
    char *filename;
    int more_chunks = 1;
    char ch[3];
    char *id = "AUDIO_PARAM_ID_MUTE";
    char *key = "CYBER_MASTER_MUTE";
    int value = 0;
    float rvol = 0.0;
    const char *bus_address = NULL;
    int status = 0;
    float vol = -3100.0;
    Spatialization spt = Center;

      if (argc < 2) {
        printf("Usage: %s file.wav [-v volume : -9000(min) to 0(max)] [-spt spatialization : FC, FL, FR, C, RC, RR, RL , R, L] \n", argv[0]);
        return 1;
    }

    filename = argv[1];
    file = fopen(filename, "rb");
    if (!file) {
        printf("Unable to open file '%s'\n", filename);
        return 1;
    }

    fread(&riff_wave_header, sizeof(riff_wave_header), 1, file);
    if ((riff_wave_header.riff_id != ID_RIFF) ||
        (riff_wave_header.wave_id != ID_WAVE)) {
        printf("Error: '%s' is not a riff/wave file\n", filename);
        fclose(file);
        return 1;
    }

    do {
        fread(&chunk_header, sizeof(chunk_header), 1, file);

        switch (chunk_header.id) {
        case ID_FMT:
            fread(&chunk_fmt, sizeof(chunk_fmt), 1, file);
            /* If the format header is larger, skip the rest */
            if (chunk_header.sz > sizeof(chunk_fmt))
                fseek(file, chunk_header.sz - sizeof(chunk_fmt), SEEK_CUR);
            break;
        case ID_DATA:
            /* Stop looking for chunks */
            more_chunks = 0;
            break;
        default:
            /* Unknown chunk, skip bytes */
            fseek(file, chunk_header.sz, SEEK_CUR);
        }
    } while (more_chunks);

     /* parse command line arguments */
    argv += 2;
    while (*argv) {
        if (strcmp(*argv, "-v") == 0) {
            argv++;
            if (*argv)
                vol = atof(*argv);
        } else if (strcmp(*argv, "-spt") == 0) {
            argv++;
            if (*argv)
                spt = stringToEnum(*argv);
        } else if (strcmp(*argv, "-id") == 0) {
            argv++;
            if (*argv)
                id = *argv;
        } else if (strcmp(*argv, "-key") == 0) {
            argv++;
            if (*argv)
                key = *argv;
        } else if (strcmp(*argv, "-value") == 0) {
            argv++;
            if (*argv)
               value = atoi(*argv);
        } else if (strcmp(*argv, "-rvalue") == 0) {
            argv++;
            if (*argv)
               bus_address = *argv;
        }
          if (*argv)
            argv++;
    }

   VehicleDataType vehicledatatype;
   strlcpy(vehicledatatype.key, key, MAX_ADDRESS_LEN);

   if (strcmp(key, "CYBER_MASTER_MUTE") == 0 ||
        strcmp(key, "TCU_MUTE") == 0 ||
        strcmp(key, "STATIC_POWER_LIMITATION") == 0 ||
        strcmp(key, "DELIVERY_MODE") == 0 ||
        strcmp(key, "PLAYBACK_STATUS") == 0) {
          vehicledatatype.value.integer_data = value;
    } else if (strcmp(key, "RESTORE_VOLUME") == 0) {
         if (bus_address) {
            vehicledatatype.value.string_data = malloc(strlen(bus_address)+1);
            if (!vehicledatatype.value.string_data) {
               fprintf(stdout, "Unable to initialise memory\n");
            }
            strlcpy(vehicledatatype.value.string_data, bus_address, strlen(bus_address)+1);
         } else {
            vehicledatatype.value.string_data = "BUS00_WS,-3100; BUS01_no_ASIL,-3100;";
            fprintf(stdout, "Error: Bus address not provided using default\n");
         }
    } else {
       fprintf(stdout, "Error:Unsupported Key\n");
    }

    if (argc > 1) {
        fprintf(stdout, "1.Press 's' to start WelcomeSound Usecase\n"
                    "2.Press 'w' to start Warning Sound Usecase\n"
                    "3.Press 'r' to start ROAD_ADAS usecase\n"
                    "4.Press 't' to start Safety usecase\n"
                    "5.Press 'c' to close the usecase \n");
        while (!streamClose) {
           fgets(ch, 3, stdin);
           switch (ch[0]) {
            case 's' :
                platform_device = get_audio_device("WELCOME_SEQUENCE");
                fprintf(stdout, "platform_device.AudioDeviceId = %d, channels = %d\n", platform_device.device_id, platform_device.channels);
                status = Open_Device(&platform_device);
                if (status) {
                    printf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&platform_device, vol);
                set_spatialization(&platform_device, spt);
                set_custom_parameter(id, vehicledatatype);
                fprintf(stdout, "\n set_custom_parameter called\n");
                sample_write(&platform_device,file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;
            case 'w' :
                platform_device = get_audio_device("WARN");
                fprintf(stdout, "platform_device.device_id = %d channels = %d\n", platform_device.device_id, platform_device.channels);
                status = Open_Device(&platform_device);
                if (status) {
                    fprintf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&platform_device, vol);
                set_spatialization(&platform_device, spt);
                set_custom_parameter(id, vehicledatatype);
                fprintf(stdout, "\n set_custom_parameter called\n");
                sample_write(&platform_device,file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;
            case 'r' :
                platform_device = get_audio_device("ROAD-ADAS");
                fprintf(stdout, "platform_device.device_id = %d channels = %d\n", platform_device.device_id, platform_device.channels);
                status = Open_Device(&platform_device);
                if (status) {
                    fprintf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&platform_device, vol);
                set_spatialization(&platform_device, spt);
                set_custom_parameter(id, vehicledatatype);
                fprintf(stdout, "\n set_custom_parameter called\n");
                sample_write(&platform_device,file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;
            case 't' :
                platform_device = get_audio_device("SWARN");
                fprintf(stdout, "platform_device.device_id = %d channels = %d\n", platform_device.device_id, platform_device.channels);
                status = Open_Device(&platform_device);
                if (status) {
                    fprintf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&platform_device, vol);
                set_spatialization(&platform_device, spt);
                set_custom_parameter(id, vehicledatatype);
                fprintf(stdout, "\n set_custom_parameter called\n");
                sample_write(&platform_device,file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;

            case 'c'  :
                disable_usecase();
                fclose(file);
                break;
            default:
                fprintf(stdout, "Enter s,w,r to start the usecase or c to close the usecase\n");
                break;
            }
    }
    return status;
    }
}
