ifeq ($(TARGET_DEVICE),crespo)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := s3c-keypad.kcm
include $(BUILD_KEY_CHAR_MAP)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := melfas-touchkey.kcm
include $(BUILD_KEY_CHAR_MAP)

include $(call all-makefiles-under,$(LOCAL_PATH))
endif
