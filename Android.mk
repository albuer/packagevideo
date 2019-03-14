LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=         \
        packagevideo.cpp \
        YuvSource.cpp \
        AvcSource.cpp

LOCAL_SHARED_LIBRARIES := \
        libstagefright libmedia liblog libutils libbinder libstagefright_foundation

LOCAL_C_INCLUDES:= \
        frameworks/av/media/libstagefright \
        frameworks/native/include/media/openmax \
        frameworks/native/include/media/hardware

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= packagevideo

include $(BUILD_EXECUTABLE)

