LOCAL_PATH := $(call my-dir)

######mono######
include $(CLEAR_VARS)  
LOCAL_MODULE := mono
LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/libmono.so  
include $(PREBUILT_SHARED_LIBRARY)  

#########ecmd#########
include $(CLEAR_VARS)
LOCAL_MODULE	:= ecmd
LOCAL_SRC_FILES	:= ecmd.c
LOCAL_ARM_MODE	:= arm
include $(BUILD_STATIC_LIBRARY)

#########protobuf-lite#########
include $(CLEAR_VARS)
LOCAL_MODULE	:= protobuf-lite
LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/libprotobuf-lite.a
include $(PREBUILT_STATIC_LIBRARY)

#########lua#########
include $(CLEAR_VARS)
LOCAL_MODULE	:= lua
LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/liblua.a
include $(PREBUILT_STATIC_LIBRARY)

######xmono######

include $(CLEAR_VARS)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES := mono
LOCAL_STATIC_LIBRARIES := ecmd protobuf-lite lua
LOCAL_LDLIBS	+=	-L$(SYSROOT)/usr/lib -llog -lz
LOCAL_MODULE	:= xmono
LOCAL_SRC_FILES	:= xmono.cpp hook.cpp dis-cil.cpp helper.cpp xmono.pb.cc lua-mono.cpp
LOCAL_ARM_MODE	:= arm
include $(BUILD_SHARED_LIBRARY)

######inject#######
include $(CLEAR_VARS)
LOCAL_LDLIBS	+=	-L$(SYSROOT)/usr/lib -llog -lz
LOCAL_MODULE	:= inject
LOCAL_SRC_FILES	:= inject.cpp
LOCAL_ARM_MODE	:= arm
include $(BUILD_EXECUTABLE)

######Install######

include $(CLEAR_VARS)

dest_path := /data/local/tmp/xmono
all:
	-@adb shell "mkdir $(dest_path)" 2 > nul
	adb push $(NDK_APP_DST_DIR)/libxmono.so $(dest_path)/
	adb push $(NDK_APP_DST_DIR)/inject $(dest_path)/
	adb shell "su -c 'chmod 744 $(dest_path)/inject'"
	
