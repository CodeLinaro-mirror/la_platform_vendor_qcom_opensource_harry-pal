ifneq ($(AUDIO_USE_STUB_HAL), true)
ifeq ($(TARGET_USES_QCOM_MM_AUDIO), true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE            := device_manager.xml
LOCAL_MODULE_FILENAME   := device_manager.xml
LOCAL_MODULE_TAGS       := optional
LOCAL_MODULE_CLASS      := ETC
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)
LOCAL_SRC_FILES         := device_manager.xml
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := libhar-pal
LOCAL_MODULE_OWNER := qti
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:=  harry_pal_main.c

LOCAL_CFLAGS        := -D_ANDROID_
LOCAL_CFLAGS        += -Wall -Werror -Wno-unused-function -Wno-unused-variable
LOCAL_CFLAGS        += -DHAR_PAL_DEVICE=\"/vendor/etc/device_manager.xml\"

LOCAL_SHARED_LIBRARIES := \
                          libaudioroute \
                          libaudioutils \
                          libcutils \
                          libdl \
                          libexpat \
                          liblog \
                          lib_default_plugin_controls \
                          libar-pal

LOCAL_C_INCLUDES     := $(LOCAL_PATH)/
LOCAL_C_INCLUDES     := $(TOP)/vendor/qcom/opensource/pal
LOCAL_C_INCLUDES     += $(TOP)/system/media/audio/include
LOCAL_C_INCLUDES     += $(TOP)/system/media/audio_route/include
LOCAL_C_INCLUDES     += external/expat/lib \
LOCAL_C_INCLUDES     += system/media/audio_utils/include

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_USE_VNDK := true

LOCAL_C_INCLUDES     := $(TOP)/vendor/qcom/opensource/pal
LOCAL_C_INCLUDES     += $(TOP)/system/media/audio/include
LOCAL_C_INCLUDES     += $(TOP)/system/media/audio_route/include
LOCAL_C_INCLUDES     += $(TOP)/external/expat/lib/expat.h

LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-macro-redefined

LOCAL_SRC_FILES  := harry_pal_test.c

LOCAL_MODULE               := HarPalTest
LOCAL_MODULE_OWNER         := qti
LOCAL_MODULE_TAGS          := optional

LOCAL_SHARED_LIBRARIES := \
                          libhar-pal \
                          libexpat \
                          libcutils \
                          liblog

LOCAL_VENDOR_MODULE := true

include $(BUILD_EXECUTABLE)

endif #TARGET_USES_QCOM_MM_AUDIO
endif #AUDIO_USE_STUB_HAL
