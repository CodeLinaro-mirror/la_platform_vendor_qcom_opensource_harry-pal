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

struct PlatformAudioDevices har_pal_init;

void sample_write(FILE *file, struct chunk_fmt fmt)
{
    char *buffer;
    uint32_t size;
    uint32_t num_read;
    ssize_t bytes;
    fprintf(stdout, "Enabling write\n");

    size = buffer_size_bytes(&har_pal_init);
    fprintf(stdout, "size = %d\n",size);
    buffer = malloc(size + 10);
    if (!buffer) {
        printf("Unable to allocate %d size\n", size);
    }

    playback_rate(&har_pal_init);
    do {
        num_read = fread(buffer, 1, size, file);
        if (num_read > 0) {
              if(!try_play_bytes(&har_pal_init, buffer,size)) {
                  fprintf(stdout, "Error playing sample\n");
                  break;
              }
        }
    } while (!streamClose && num_read > 0);

    free(buffer);
    fprintf(stdout, "Done Playing.\n");
}

void disable_usecase()
{
    int status =0;
    fprintf(stdout,"Enter Disable_usecase()\n");
    status = StopAndCloseUsecase(&har_pal_init);
    if(status) {
       fprintf(stdout, "StopAndCloseUsecase failed\n");
    }
    fprintf(stdout,"Exit Disable_usecase()\n");
    streamClose = 1;
}

static void sigint_handler(int sig)
{
   signal(sig, SIG_IGN);
   disable_usecase();
   exit(0);
}

int main(int argc, char *argv[])
{
    FILE *file;
    struct riff_wave_header riff_wave_header;
    struct chunk_header chunk_header;
    struct chunk_fmt chunk_fmt;
    const char *pal_dev;
    char *filename;
    int more_chunks = 1;
    ssize_t ret = 0;
    char ch[3];
    int status = 0;
    unsigned int usecase_id;
    int pcm_id;

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

     argv += 2;

    //Handling signals
    signal(SIGINT, sigint_handler);

    har_pal_init.card = 0;
    har_pal_init.rate = 48000;
    har_pal_init.format = 16;
    har_pal_init.start_threshold = 0;
    har_pal_init.stop_threshold = 0;
    har_pal_init.silence_threshold = 0;
    har_pal_init.silence_size = 0;
    har_pal_init.avail_min = 0;

    IntVector devices = available_devices();

    for(size_t i =0; i < devices.size; ++i) {
     fprintf(stdout, "Device Name: %s, Device Id: %d\n", devices.data[i].name, devices.data[i].id);
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
                har_pal_init.AudioDeviceId = devices.data[0].id;
                har_pal_init.channels = 2;
                fprintf(stdout, "har_pal_init.AudioDeviceId = %d, channels = %d\n", har_pal_init.AudioDeviceId, har_pal_init.channels);
                status = Open_Device(&har_pal_init);
                if (status) {
                    printf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&har_pal_init, -4000.0);
                sample_write(file,chunk_fmt);
                disable_usecase();
                fclose(file);
                    break;
            case 'w' :
                har_pal_init.AudioDeviceId = devices.data[1].id;
                har_pal_init.channels = 1;
                fprintf(stdout, "har_pal_init.AudioDeviceId = %d, channels = %d\n", har_pal_init.AudioDeviceId, har_pal_init.channels);
                status = Open_Device(&har_pal_init);
                if (status) {
                    fprintf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&har_pal_init, -4000.0);
                sample_write(file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;
            case 'r' :
                har_pal_init.AudioDeviceId = devices.data[2].id;
                har_pal_init.channels = 1;
                fprintf(stdout, "har_pal_init.AudioDeviceId = %d, channels = %d\n", har_pal_init.AudioDeviceId, har_pal_init.channels);
                status = Open_Device(&har_pal_init);
                if (status) {
                    fprintf(stdout, "Open_Device failed\n");
                    break;
                }
                set_volume(&har_pal_init, -4000.0);
                sample_write(file,chunk_fmt);
                disable_usecase();
                fclose(file);
                break;
            case 't' :
                har_pal_init.AudioDeviceId = devices.data[3].id;
                har_pal_init.channels = 1;
                fprintf(stdout, "har_pal_init.AudioDeviceId = %d, channels = %d\n", har_pal_init.AudioDeviceId, har_pal_init.channels);
                status = Open_Device(&har_pal_init);
                if (status) {
                        fprintf(stdout, "Open_Device failed\n");
                        break;
                }
                set_volume(&har_pal_init, -4000.0);
                sample_write(file,chunk_fmt);
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
    free_vector(&devices);
    return status;
    }
}

