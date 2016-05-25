LOCAL_PATH := $(call my-dir)
FFMPEG_LIBS_PATH := $(LOCAL_PATH)/gen/lib

include $(CLEAR_VARS)  
LOCAL_MODULE := libavformat  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libavformat.so  
include $(PREBUILT_SHARED_LIBRARY) 

include $(CLEAR_VARS)  
LOCAL_MODULE := libavcodec  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libavcodec.so  
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)  
LOCAL_MODULE := libavdevice  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libavdevice.so
include $(PREBUILT_SHARED_LIBRARY)  
  
include $(CLEAR_VARS)  
LOCAL_MODULE := libavfilter  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libavfilter.so  
include $(PREBUILT_SHARED_LIBRARY)  
   
include $(CLEAR_VARS)  
LOCAL_MODULE := libswresample  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libswresample.so  
include $(PREBUILT_SHARED_LIBRARY)  
  
include $(CLEAR_VARS)  
LOCAL_MODULE := libswscale  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libswscale.so  
include $(PREBUILT_SHARED_LIBRARY)  

include $(CLEAR_VARS)  
LOCAL_MODULE := libavutil  
LOCAL_SRC_FILES :=  $(FFMPEG_LIBS_PATH)/libavutil.so  
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := smt_player
LOCAL_CFLAGS    := -fPIC -Werror -UNDEBUG -D_DEBUG
LOCAL_C_INCLUDES := $(LOCAL_PATH)/gen/include
LOCAL_SRC_FILES := smt_player.c \
				smt_render.c
LOCAL_LDFLAGS := -L$(FFMPEG_LIBS_PATH)
LOCAL_SHARED_LIBRARIES := libavformat libavcodec libavdevice libavfilter libswscale libswresample libavutil
LOCAL_LDLIBS    :=  -llog -landroid -lGLESv1_CM -lGLESv2 -llog  -ljnigraphics -lz -lOpenSLES
include $(BUILD_SHARED_LIBRARY)
