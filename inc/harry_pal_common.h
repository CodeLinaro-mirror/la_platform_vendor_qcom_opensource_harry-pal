/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef LOG_NDEBUG
#define LOG_NDEBUG 0
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef FEATURE_IPQ_OPENWRT
#include <audio_utils/log.h>
#else
#include <log/log.h>
#endif

#ifdef FEATURE_IPQ_OPENWRT
#include <audio_utils/log.h>
#else
#include <log/log.h>
#endif

#define HAR_PAL_LOG_ERR             (0x1)
#define HAR_PAL_LOG_INFO            (0x2)
#define HAR_PAL_LOG_DBG             (0x4)
#define HAR_PAL_LOG_VERBOSE         (0x8)

/*TODO make this dynamic*/
uint32_t har_pal_log_lvl = HAR_PAL_LOG_ERR|HAR_PAL_LOG_INFO|HAR_PAL_LOG_DBG;

#define HAR_PAL_ERR(log_tag, arg,...)                             \
    if (har_pal_log_lvl & HAR_PAL_LOG_ERR) {                      \
        ALOGE("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
#define HAR_PAL_DBG(log_tag,arg,...)                              \
    if (har_pal_log_lvl & HAR_PAL_LOG_DBG) {                      \
        ALOGD("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
#define HAR_PAL_INFO(log_tag,arg,...)                             \
    if (har_pal_log_lvl & HAR_PAL_LOG_INFO) {                     \
        ALOGI("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }
#define HAR_PAL_VERBOSE(log_tag,arg,...)                          \
    if (har_pal_log_lvl & HAR_PAL_LOG_VERBOSE) {                  \
        ALOGV("%s: %d: "  arg, __func__, __LINE__, ##__VA_ARGS__);\
    }

