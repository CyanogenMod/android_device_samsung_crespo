/*
 * Video for Linux Two header file for samsung
 *
 * Copyright (C) 2009, Dongsoo Nathaniel Kim<dongsoo45.kim@samsung.com>
 *
 * This header file contains several v4l2 APIs to be proposed to v4l2
 * community and until bein accepted, will be used restrictly in Samsung's
 * camera interface driver FIMC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Alternatively, Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LINUX_VIDEODEV2_SAMSUNG_H

#define V4L2_CID_CAM_FW_MINOR_VER               (V4L2_CID_PRIVATE_BASE + 24)
#define V4L2_CID_CAM_FW_MAJOR_VER               (V4L2_CID_PRIVATE_BASE + 25)
#define V4L2_CID_CAM_PRM_MINOR_VER              (V4L2_CID_PRIVATE_BASE + 26)
#define V4L2_CID_CAM_PRM_MAJOR_VER              (V4L2_CID_PRIVATE_BASE + 27)
#define V4L2_CID_CAMERA_VT_MODE                 (V4L2_CID_PRIVATE_BASE + 48)
#define V4L2_CID_CAMERA_VGA_BLUR                (V4L2_CID_PRIVATE_BASE + 49)
#define V4L2_CID_CAMERA_CAPTURE                 (V4L2_CID_PRIVATE_BASE + 50)

#define V4L2_CID_CAMERA_SCENE_MODE              (V4L2_CID_PRIVATE_BASE + 70)
#define V4L2_CID_CAMERA_FLASH_MODE              (V4L2_CID_PRIVATE_BASE + 71)
#define V4L2_CID_CAMERA_BRIGHTNESS              (V4L2_CID_PRIVATE_BASE + 72)
#define V4L2_CID_CAMERA_WHITE_BALANCE           (V4L2_CID_PRIVATE_BASE + 73)
#define V4L2_CID_CAMERA_EFFECT                  (V4L2_CID_PRIVATE_BASE + 74)
#define V4L2_CID_CAMERA_ISO                     (V4L2_CID_PRIVATE_BASE + 75)
#define V4L2_CID_CAMERA_METERING                (V4L2_CID_PRIVATE_BASE + 76)
#define V4L2_CID_CAMERA_CONTRAST                (V4L2_CID_PRIVATE_BASE + 77)
#define V4L2_CID_CAMERA_SATURATION              (V4L2_CID_PRIVATE_BASE + 78)
#define V4L2_CID_CAMERA_SHARPNESS               (V4L2_CID_PRIVATE_BASE + 79)
#define V4L2_CID_CAMERA_WDR                     (V4L2_CID_PRIVATE_BASE + 80)
#define V4L2_CID_CAMERA_ANTI_SHAKE              (V4L2_CID_PRIVATE_BASE + 81)
#define V4L2_CID_CAMERA_TOUCH_AF_START_STOP     (V4L2_CID_PRIVATE_BASE + 82)
#define V4L2_CID_CAMERA_SMART_AUTO              (V4L2_CID_PRIVATE_BASE + 83)
#define V4L2_CID_CAMERA_VINTAGE_MODE            (V4L2_CID_PRIVATE_BASE + 84)
#define V4L2_CID_CAMERA_GPS_LATITUDE            (V4L2_CID_CAMERA_CLASS_BASE + 30)
#define V4L2_CID_CAMERA_GPS_LONGITUDE           (V4L2_CID_CAMERA_CLASS_BASE + 31)
#define V4L2_CID_CAMERA_GPS_TIMESTAMP           (V4L2_CID_CAMERA_CLASS_BASE + 32)
#define V4L2_CID_CAMERA_GPS_ALTITUDE            (V4L2_CID_CAMERA_CLASS_BASE + 33)
#define V4L2_CID_CAMERA_ZOOM                    (V4L2_CID_PRIVATE_BASE + 90)
#define V4L2_CID_CAMERA_FACE_DETECTION          (V4L2_CID_PRIVATE_BASE + 91)
#define V4L2_CID_CAMERA_SMART_AUTO_STATUS       (V4L2_CID_PRIVATE_BASE + 92)
#define V4L2_CID_CAMERA_SET_AUTO_FOCUS          (V4L2_CID_PRIVATE_BASE + 93)
#define V4L2_CID_CAMERA_BEAUTY_SHOT             (V4L2_CID_PRIVATE_BASE + 94)
#define V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK       (V4L2_CID_PRIVATE_BASE + 95)
#define V4L2_CID_CAMERA_FACEDETECT_LOCKUNLOCK   (V4L2_CID_PRIVATE_BASE + 96)
#define V4L2_CID_CAMERA_OBJECT_POSITION_X       (V4L2_CID_PRIVATE_BASE + 97)
#define V4L2_CID_CAMERA_OBJECT_POSITION_Y       (V4L2_CID_PRIVATE_BASE + 98)
#define V4L2_CID_CAMERA_FOCUS_MODE              (V4L2_CID_PRIVATE_BASE + 99)
#define V4L2_CID_CAMERA_OBJ_TRACKING_STATUS     (V4L2_CID_PRIVATE_BASE + 100)
#define V4L2_CID_CAMERA_OBJ_TRACKING_START_STOP (V4L2_CID_PRIVATE_BASE + 101)
#define V4L2_CID_CAMERA_CAF_START_STOP          (V4L2_CID_PRIVATE_BASE + 102)
#define V4L2_CID_CAMERA_AUTO_FOCUS_RESULT       (V4L2_CID_PRIVATE_BASE + 103)
#define V4L2_CID_CAMERA_FRAME_RATE              (V4L2_CID_PRIVATE_BASE + 104)
#define V4L2_CID_CAMERA_ANTI_BANDING            (V4L2_CID_PRIVATE_BASE + 105)
#define V4L2_CID_CAMERA_SET_GAMMA               (V4L2_CID_PRIVATE_BASE + 106)
#define V4L2_CID_CAMERA_SET_SLOW_AE             (V4L2_CID_PRIVATE_BASE + 107)
#define V4L2_CID_CAMERA_BATCH_REFLECTION        (V4L2_CID_PRIVATE_BASE + 108)
#define V4L2_CID_CAMERA_EXIF_ORIENTATION        (V4L2_CID_PRIVATE_BASE + 109)
#define V4L2_CID_CAMERA_RESET                   (V4L2_CID_PRIVATE_BASE + 111)
#define V4L2_CID_CAMERA_CHECK_DATALINE          (V4L2_CID_PRIVATE_BASE + 112)
#define V4L2_CID_CAMERA_CHECK_DATALINE_STOP     (V4L2_CID_PRIVATE_BASE + 113)
#define V4L2_CID_CAMERA_GET_ISO                 (V4L2_CID_PRIVATE_BASE + 114)
#define V4L2_CID_CAMERA_GET_SHT_TIME            (V4L2_CID_PRIVATE_BASE + 115)
#define V4L2_CID_CAMERA_SENSOR_MODE             (V4L2_CID_PRIVATE_BASE + 116)
#define V4L2_CID_ESD_INT                        (V4L2_CID_PRIVATE_BASE + 117)

#endif /* __LINUX_VIDEODEV2_SAMSUNG_H */
