/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_SEC_H
#define ANDROID_HARDWARE_CAMERA_SEC_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
#include <videodev2_samsung.h>
#endif

#include "JpegEncoder.h"

#ifdef ENABLE_HDMI_DISPLAY
#include "hdmi_lib.h"
#endif

#include <camera/CameraHardwareInterface.h>

namespace android {
//Define this if the preview data is to be shared using memory mapped technique instead of passing physical address.
#define PREVIEW_USING_MMAP
//Define this if the JPEG images are obtained directly from camera sensor. Else on chip JPEG encoder will be used.
#define JPEG_FROM_SENSOR

//#define DUAL_PORT_RECORDING //Define this if 2 fimc ports are needed for recording.

//#define SEND_YUV_RECORD_DATA //Define this to copy YUV data to encoder instead of sharing the physical address.

#define INCLUDE_JPEG_THUMBNAIL 1 //Valid only for on chip JPEG encoder

#if defined PREVIEW_USING_MMAP
#define DUAL_PORT_RECORDING
#endif

#if defined JPEG_FROM_SENSOR
#define DIRECT_DELIVERY_OF_POSTVIEW_DATA //Define this if postview data is needed in buffer instead of zero copy.
#endif

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
#define LOG_CAMERA LOGD
#define LOG_CAMERA_PREVIEW LOGD

#define LOG_TIME_DEFINE(n) \
    struct timeval time_start_##n, time_stop_##n; unsigned long log_time_##n = 0;

#define LOG_TIME_START(n) \
    gettimeofday(&time_start_##n, NULL);

#define LOG_TIME_END(n) \
    gettimeofday(&time_stop_##n, NULL); log_time_##n = measure_time(&time_start_##n, &time_stop_##n);

#define LOG_TIME(n) \
    log_time_##n

#else
#define LOG_CAMERA(...)
#define LOG_CAMERA_PREVIEW(...)
#define LOG_TIME_DEFINE(n)
#define LOG_TIME_START(n)
#define LOG_TIME_END(n)
#define LOG_TIME(n)
#endif

#define JOIN(x, y) JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y) x ## y

#define FRONT_CAM VGA
#define BACK_CAM ISX006

#if !defined (FRONT_CAM) || !defined(BACK_CAM)
#error "Please define the Camera module"
#endif

#define ISX006_PREVIEW_WIDTH            640
#define ISX006_PREVIEW_HEIGHT           480
#define ISX006_SNAPSHOT_WIDTH           2560
#define ISX006_SNAPSHOT_HEIGHT          1920

#define ISX006_POSTVIEW_WIDTH           640
#define ISX006_POSTVIEW_WIDE_WIDTH      800
#define ISX006_POSTVIEW_HEIGHT          480
#define ISX006_POSTVIEW_BPP             16

#define ISX006_THUMBNAIL_WIDTH          320
#define ISX006_THUMBNAIL_HEIGHT         240
#define ISX006_THUMBNAIL_BPP            16

#define VGA_PREVIEW_WIDTH               640
#define VGA_PREVIEW_HEIGHT              480
#define VGA_SNAPSHOT_WIDTH              640
#define VGA_SNAPSHOT_HEIGHT             480

#define MAX_BACK_CAMERA_PREVIEW_WIDTH       JOIN(BACK_CAM,_PREVIEW_WIDTH)
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT      JOIN(BACK_CAM,_PREVIEW_HEIGHT)
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH      JOIN(BACK_CAM,_SNAPSHOT_WIDTH)
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT     JOIN(BACK_CAM,_SNAPSHOT_HEIGHT)
#define BACK_CAMERA_POSTVIEW_WIDTH          JOIN(BACK_CAM,_POSTVIEW_WIDTH)
#define BACK_CAMERA_POSTVIEW_WIDE_WIDTH     JOIN(BACK_CAM,_POSTVIEW_WIDE_WIDTH)
#define BACK_CAMERA_POSTVIEW_HEIGHT         JOIN(BACK_CAM,_POSTVIEW_HEIGHT)
#define BACK_CAMERA_POSTVIEW_BPP            JOIN(BACK_CAM,_POSTVIEW_BPP)
#define BACK_CAMERA_THUMBNAIL_WIDTH         JOIN(BACK_CAM,_THUMBNAIL_WIDTH)
#define BACK_CAMERA_THUMBNAIL_HEIGHT        JOIN(BACK_CAM,_THUMBNAIL_HEIGHT)
#define BACK_CAMERA_THUMBNAIL_BPP           JOIN(BACK_CAM,_THUMBNAIL_BPP)

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH      JOIN(FRONT_CAM,_PREVIEW_WIDTH)
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT     JOIN(FRONT_CAM,_PREVIEW_HEIGHT)
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH     JOIN(FRONT_CAM,_SNAPSHOT_WIDTH)
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT    JOIN(FRONT_CAM,_SNAPSHOT_HEIGHT)

#define DEFAULT_JPEG_THUMBNAIL_WIDTH        256
#define DEFAULT_JPEG_THUMBNAIL_HEIGHT       192

#define CAMERA_DEV_NAME   "/dev/video0"

#ifdef DUAL_PORT_RECORDING
#define CAMERA_DEV_NAME2  "/dev/video2"
#endif
#define CAMERA_DEV_NAME_TEMP "/data/videotmp_000"
#define CAMERA_DEV_NAME2_TEMP "/data/videotemp_002"


#define BPP             2
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define MAX_BUFFERS     8

/*
 * V 4 L 2   F I M C   E X T E N S I O N S
 *
 */
#define V4L2_CID_ROTATION                   (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PADDR_Y                    (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PADDR_CB                   (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PADDR_CR                   (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PADDR_CBCR                 (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_STREAM_PAUSE               (V4L2_CID_PRIVATE_BASE + 53)

#define V4L2_CID_CAM_JPEG_MAIN_SIZE         (V4L2_CID_PRIVATE_BASE + 32)
#define V4L2_CID_CAM_JPEG_MAIN_OFFSET       (V4L2_CID_PRIVATE_BASE + 33)
#define V4L2_CID_CAM_JPEG_THUMB_SIZE        (V4L2_CID_PRIVATE_BASE + 34)
#define V4L2_CID_CAM_JPEG_THUMB_OFFSET      (V4L2_CID_PRIVATE_BASE + 35)
#define V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET   (V4L2_CID_PRIVATE_BASE + 36)
#define V4L2_CID_CAM_JPEG_QUALITY           (V4L2_CID_PRIVATE_BASE + 37)

#define TPATTERN_COLORBAR           1
#define TPATTERN_HORIZONTAL         2
#define TPATTERN_VERTICAL           3

#define V4L2_PIX_FMT_YVYU           v4l2_fourcc('Y', 'V', 'Y', 'U')

/* FOURCC for FIMC specific */
#define V4L2_PIX_FMT_VYUY           v4l2_fourcc('V', 'Y', 'U', 'Y')
#define V4L2_PIX_FMT_NV16           v4l2_fourcc('N', 'V', '1', '6')
#define V4L2_PIX_FMT_NV61           v4l2_fourcc('N', 'V', '6', '1')
#define V4L2_PIX_FMT_NV12T          v4l2_fourcc('T', 'V', '1', '2')
/*
 * U S E R   D E F I N E D   T Y P E S
 *
 */

struct fimc_buffer {
    void    *start;
    size_t  length;
};

struct yuv_fmt_list {
    const char  *name;
    const char  *desc;
    unsigned int    fmt;
    int     depth;
    int     planes;
};

//s1 [Apply factory standard]
struct camsensor_date_info {
    unsigned int year;
    unsigned int month;
    unsigned int date;
};


class SecCamera {
public:

    enum CAMERA_ID {
        CAMERA_ID_BACK  = 0,
        CAMERA_ID_FRONT = 1,
    };

    enum AUTO_FOCUS {
        AUTO_FOCUS_OFF,
        AUTO_FOCUS_ON,
        AUTO_FOCUS_STATUS,
    };

    enum WHILTE_BALANCE {
        #ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
        WHITE_BALANCE_BASE,
        WHITE_BALANCE_AUTO,
        WHITE_BALANCE_DAYLIGHT,
        WHITE_BALANCE_CLOUDY,
        WHITE_BALANCE_INCANDESCENT,
        WHITE_BALANCE_FLUORESCENT,
        WHITE_BALANCE_MAX,
        #else
        WHITE_BALANCE_AUTO,
        WHITE_BALANCE_INDOOR3100,
        WHITE_BALANCE_OUTDOOR5100,
        WHITE_BALANCE_INDOOR2000,
        WHITE_BALANCE_HALT,
        WHITE_BALANCE_CLOUDY,
        WHITE_BALANCE_SUNNY,
        #endif
    };

    enum BRIGHTNESS {
        BRIGHTNESS_MINUS_4= 0,
        BRIGHTNESS_MINUS_3,
        BRIGHTNESS_MINUS_2,
        BRIGHTNESS_MINUS_1,
        BRIGHTNESS_NORMAL,
        BRIGHTNESS_PLUS_1,
        BRIGHTNESS_PLUS_2,
        BRIGHTNESS_PLUS_3,
        BRIGHTNESS_PLUS_4,
    };

    enum IMAGE_EFFECT {
        #ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
        IMAGE_EFFECT_BASE,
        IMAGE_EFFECT_NONE,
        IMAGE_EFFECT_BNW,
        IMAGE_EFFECT_SEPIA,
        IMAGE_EFFECT_AQUA,
        IMAGE_EFFECT_ANTIQUE,
        IMAGE_EFFECT_NEGATIVE,
        IMAGE_EFFECT_SHARPEN,
        IMAGE_EFFECT_MAX,
        #else
        IMAGE_EFFECT_ORIGINAL,
        IMAGE_EFFECT_ARBITRARY,
        IMAGE_EFFECT_NEGATIVE,
        IMAGE_EFFECT_FREEZE,
        IMAGE_EFFECT_EMBOSSING,
        IMAGE_EFFECT_SILHOUETTE,
        #endif
    };

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    enum SCENE_MODE {
        SCENE_MODE_BASE,
        SCENE_MODE_NONE,
        SCENE_MODE_PORTRAIT,
        SCENE_MODE_NIGHTSHOT,
        SCENE_MODE_BACK_LIGHT,
        SCENE_MODE_LANDSCAPE,
        SCENE_MODE_SPORTS,
        SCENE_MODE_PARTY_INDOOR,
        SCENE_MODE_BEACH_SNOW,
        SCENE_MODE_SUNSET,
        SCENE_MODE_DUSK_DAWN,
        SCENE_MODE_FALL_COLOR,
        SCENE_MODE_FIREWORKS,
        SCENE_MODE_TEXT,
        SCENE_MODE_CANDLE_LIGHT,
        SCENE_MODE_MAX,
    };

    enum FLASH_MODE {
        FLASH_MODE_BASE,
        FLASH_MODE_OFF,
        FLASH_MODE_AUTO,
        FLASH_MODE_ON,
        FLASH_MODE_TORCH,
        FLASH_MODE_MAX,
    };

    enum ISO {
        ISO_AUTO,
        ISO_50,
        ISO_100,
        ISO_200,
        ISO_400,
        ISO_800,
        ISO_1600,
        ISO_SPORTS,
        ISO_NIGHT,
        ISO_MOVIE,
        ISO_MAX,
    };

    enum METERING {
        METERING_BASE = 0,
        METERING_MATRIX,
        METERING_CENTER,
        METERING_SPOT,
        METERING_MAX,
    };

    enum CONTRAST {
        CONTRAST_MINUS_2 = 0,
        CONTRAST_MINUS_1,
        CONTRAST_NORMAL,
        CONTRAST_PLUS_1,
        CONTRAST_PLUS_2,
        CONTRAST_MAX,
    };

    enum SATURATION {
        SATURATION_MINUS_2= 0,
        SATURATION_MINUS_1,
        SATURATION_NORMAL,
        SATURATION_PLUS_1,
        SATURATION_PLUS_2,
        SATURATION_MAX,
    };

    enum SHARPNESS {
        SHARPNESS_MINUS_2 = 0,
        SHARPNESS_MINUS_1,
        SHARPNESS_NORMAL,
        SHARPNESS_PLUS_1,
        SHARPNESS_PLUS_2,
        SHARPNESS_MAX,
    };

    enum WDR {
        WDR_OFF,
        WDR_ON,
        WDR_MAX,
    };

    enum ANTI_SHAKE {
        ANTI_SHAKE_OFF,
        ANTI_SHAKE_ON,
        ANTI_SHAKE_MAX,
    };

    enum JPEG_QUALITY {
        JPEG_QUALITY_ECONOMY    = 0,
        JPEG_QUALITY_NORMAL     = 50,
        JPEG_QUALITY_SUPERFINE  = 100,
        JPEG_QUALITY_MAX,
    };

    enum ZOOM_LEVEL {
        ZOOM_LEVEL_0 = 0,
        ZOOM_LEVEL_1,
        ZOOM_LEVEL_2,
        ZOOM_LEVEL_3,
        ZOOM_LEVEL_4,
        ZOOM_LEVEL_5,
        ZOOM_LEVEL_6,
        ZOOM_LEVEL_7,
        ZOOM_LEVEL_8,
        ZOOM_LEVEL_9,
        ZOOM_LEVEL_10,
        ZOOM_LEVEL_11,
        ZOOM_LEVEL_12,
        ZOOM_LEVEL_MAX,
    };

    enum OBJECT_TRACKING {
        OBJECT_TRACKING_OFF,
        OBJECT_TRACKING_ON,
        OBJECT_TRACKING_MAX,
    };

    enum OBJECT_TRACKING_STAUS {
        OBJECT_TRACKING_STATUS_BASE,
        OBJECT_TRACKING_STATUS_PROGRESSING,
        OBJECT_TRACKING_STATUS_SUCCESS,
        OBJECT_TRACKING_STATUS_FAIL,
        OBJECT_TRACKING_STATUS_MISSING,
        OBJECT_TRACKING_STATUS_MAX,
    };

    enum SMART_AUTO {
        SMART_AUTO_OFF,
        SMART_AUTO_ON,
        SMART_AUTO_MAX,
    };

    enum BEAUTY_SHOT {
        BEAUTY_SHOT_OFF,
        BEAUTY_SHOT_ON,
        BEAUTY_SHOT_MAX,
    };

    enum VINTAGE_MODE {
        VINTAGE_MODE_BASE,
        VINTAGE_MODE_OFF,
        VINTAGE_MODE_NORMAL,
        VINTAGE_MODE_WARM,
        VINTAGE_MODE_COOL,
        VINTAGE_MODE_BNW,
        VINTAGE_MODE_MAX,
    };

    enum FOCUS_MODE {
        FOCUS_MODE_AUTO,
        FOCUS_MODE_MACRO,
        FOCUS_MODE_FACEDETECT,
        FOCUS_MODE_AUTO_DEFAULT,
        FOCUS_MODE_MACRO_DEFAULT,
        FOCUS_MODE_FACEDETECT_DEFAULT,
        FOCUS_MODE_MAX,
    };

    enum FACE_DETECT {
        FACE_DETECT_OFF,
        FACE_DETECT_NORMAL_ON,
        FACE_DETECT_BEAUTY_ON,
        FACE_DETECT_NO_LINE,
        FACE_DETECT_MAX,
    };

    enum AE_AWB_LOCK_UNLOCK  {
        AE_UNLOCK_AWB_UNLOCK = 0,
        AE_LOCK_AWB_UNLOCK,
        AE_UNLOCK_AWB_LOCK,
        AE_LOCK_AWB_LOCK,
        AE_AWB_MAX
    };

    enum FRAME_RATE {
        FRAME_RATE_AUTO = 0,
        FRAME_RATE_15   = 15,
        FRAME_RATE_30   = 30,
        FRAME_RATE_60   = 60,
        FRAME_RATE_120  = 120,
        FRAME_RATE_MAX
    };
    enum ANTI_BANDING {
        ANTI_BANDING_AUTO   = 0,
        ANTI_BANDING_50HZ   = 1,
        ANTI_BANDING_60HZ   = 2,
        ANTI_BANDING_OFF    = 3,
    };

    enum SMART_AUTO_SCENE {
        SMART_AUTO_STATUS_AUTO = 0,
        SMART_AUTO_STATUS_LANDSCAPE,
        SMART_AUTO_STATUS_PORTRAIT,
        SMART_AUTO_STATUS_MACRO,
        SMART_AUTO_STATUS_NIGHT,
        SMART_AUTO_STATUS_PORTRAIT_NIGHT,
        SMART_AUTO_STATUS_BACKLIT,
        SMART_AUTO_STATUS_PORTRAIT_BACKLIT,
        SMART_AUTO_STATUS_ANTISHAKE,
        SMART_AUTO_STATUS_PORTRAIT_ANTISHAKE,
        SMART_AUTO_STATUS_MAX,
    };

    enum GAMMA {
        GAMMA_OFF,
        GAMMA_ON,
        GAMMA_MAX,
    };

    enum SLOW_AE {
        SLOW_AE_OFF,
        SLOW_AE_ON,
        SLOW_AE_MAX,
    };

    /*VT call*/
    enum VT_MODE {
        VT_MODE_OFF,
        VT_MODE_ON,
        VT_MODE_MAX,
    };

    /*Camera sensor mode - Camcorder fix fps*/
    enum SENSOR_MODE {
        SENSOR_MODE_CAMERA,
        SENSOR_MODE_MOVIE,
    };

    /*Camera Shot mode*/
    enum SHOT_MODE {
        SHOT_MODE_SINGLE        = 0,
        SHOT_MODE_CONTINUOUS    = 1,
        SHOT_MODE_PANORAMA      = 2,
        SHOT_MODE_SMILE         = 3,
        SHOT_MODE_SELF          = 6,
    };

    enum BLUR_LEVEL {
        BLUR_LEVEL_0 = 0,
        BLUR_LEVEL_1,
        BLUR_LEVEL_2,
        BLUR_LEVEL_3,
        BLUR_LEVEL_MAX,
    };

    enum CHK_DATALINE {
        CHK_DATALINE_OFF,
        CHK_DATALINE_ON,
        CHK_DATALINE_MAX,
    };

    enum FACE_LOCK {
        FACE_LOCK_OFF,
        FACE_LOCK_ON,
        FIRST_FACE_TRACKING,
        FACE_LOCK_MAX
    };

    int m_touch_af_start_stop;
    int m_focus_mode;
    int m_iso;

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    struct gps_info_latiude {
        unsigned int    north_south;
        unsigned int    dgree;
        unsigned int    minute;
        unsigned int    second;
    } gpsInfoLatitude;
    struct gps_info_longitude {
        unsigned int    east_west;
        unsigned int    dgree;
        unsigned int    minute;
        unsigned int    second;
    } gpsInfoLongitude;
    struct gps_info_altitude {
        unsigned int    plus_minus;
        unsigned int    dgree;
        unsigned int    minute;
        unsigned int    second;
    } gpsInfoAltitude;
#endif


#endif

    SecCamera();
    ~SecCamera();

    static SecCamera* createInstance(void)
    {
        static SecCamera singleton;
        return &singleton;
    }
    status_t dump(int fd, const Vector<String16>& args);

    int             flagCreate(void) const;


    int             setCameraId(int camera_id);
    int             getCameraId(void);

    int             startPreview(void);
    int             stopPreview(void);
#ifdef DUAL_PORT_RECORDING
    int             startRecord(void);
    int             stopRecord(void);
    int             getRecord(void);
    unsigned int    getRecPhyAddrY(int);
    unsigned int    getRecPhyAddrC(int);
#endif
    int             flagPreviewStart(void);
    //int             getPreview (unsigned char *buffer, unsigned int buffer_size);
    int             getPreview(void);
    //int             getPreview(int *offset, int *size, unsigned char *buffer, unsigned int buffer_size);
    int             setPreviewSize(int width, int height, int pixel_format);
    int             getPreviewSize(int *width, int *height, int *frame_size);
    int             getPreviewMaxSize(int *width, int *height);
    int             getPreviewPixelFormat(void);
    int             setPreviewImage(int index, unsigned char *buffer, int size);


    int             getSnapshot(unsigned char *buffer, unsigned int buffer_size);
    int             setSnapshotSize(int width, int height);
    int             getSnapshotSize(int *width, int *height, int *frame_size);
    int             getSnapshotMaxSize(int *width, int *height);
    int             setSnapshotPixelFormat(int pixel_format);
    int             getSnapshotPixelFormat(void);

    unsigned char*  getJpeg(unsigned char *snapshot_data, int snapshot_size, int *size);
    unsigned char*  yuv2Jpeg(unsigned char *raw_data, int raw_size,
                                int *jpeg_size,
                                int width, int height, int pixel_format);

    int             setJpegThumbnailSize(int width, int height);
    int             getJpegThumbnailSize(int *width, int *height);

    int             setAutofocus(void);
    int             zoomIn(void);
    int             zoomOut(void);

    int             SetRotate(int angle);
    int             getRotate(void);

    int             setVerticalMirror(void);
    int             setHorizontalMirror(void);

    int             setWhiteBalance(int white_balance);
    int             getWhiteBalance(void);

    int             setBrightness(int brightness);
    int             getBrightness(void);

    int             setImageEffect(int image_effect);
    int             getImageEffect(void);
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    int             setSceneMode(int scene_mode);
    int             getSceneMode(void);

    int             setFlashMode(int flash_mode);
    int             getFlashMode(void);

    int             setMetering(int metering_value);
    int             getMetering(void);

    int             setISO(int iso_value);
    int             getISO(void);

    int             setContrast(int contrast_value);
    int             getContrast(void);

    int             setSaturation(int saturation_value);
    int             getSaturation(void);

    int             setSharpness(int sharpness_value);
    int             getSharpness(void);

    int             setWDR(int wdr_value);
    int             getWDR(void);

    int             setAntiShake(int anti_shake);
    int             getAntiShake(void);

    int             setJpegQuality(int jpeg_qality);
    int             getJpegQuality(void);

    int             setZoom(int zoom_level);
    int             getZoom(void);

    int             setObjectTracking(int object_tracking);
    int             getObjectTracking(void);
    int             getObjectTrackingStatus(void);

    int             setSmartAuto(int smart_auto);
    int             getSmartAuto(void);
    int             getAutosceneStatus(void);

    int             setBeautyShot(int beauty_shot);
    int             getBeautyShot(void);

    int             setVintageMode(int vintage_mode);
    int             getVintageMode(void);

    int             setFocusMode(int focus_mode);
    int             getFocusMode(void);

    int             setFaceDetect(int face_detect);
    int             getFaceDetect(void);

    int             setGPSLatitude(const char *gps_latitude);
    int             setGPSLongitude(const char *gps_longitude);
    int             setGPSAltitude(const char *gps_altitude);
    int             setGPSTimeStamp(const char *gps_timestamp);
    int             cancelAutofocus(void);
    int             setAEAWBLockUnlock(int ae_lockunlock, int awb_lockunlock);
    int             setFaceDetectLockUnlock(int facedetect_lockunlock);
    int             setObjectPosition(int x, int y);
    int             setObjectTrackingStartStop(int start_stop);
    int             setTouchAFStartStop(int start_stop);
    int             setCAFStatus(int on_off);
    int             getAutoFocusResult(void);
    int             setAntiBanding(int anti_banding);
    int             getPostview(void);
    int             setRecordingSize(int width, int height);
    int             setGamma(int gamma);
    int             setSlowAE(int slow_ae);
    int             setExifOrientationInfo(int orientationInfo);
    int             setBatchReflection(void);
    int             setSnapshotCmd(void);
    int             setCameraSensorReset(void);
    int             setSensorMode(int sensor_mode); /* Camcorder fix fps */
    int             setShotMode(int shot_mode);     /* Shot mode */
    /*VT call*/
    int             setVTmode(int vtmode);
    int             getVTmode(void);
    int             setBlur(int blur_level);
    int             getBlur(void);
    int             setDataLineCheck(int chk_dataline);
    int             getDataLineCheck(void);
    int             setDataLineCheckStop(void);
    int             setDefultIMEI(int imei);
    int             getDefultIMEI(void);
#endif

    void setFrameRate(int frame_rate);
//  void setJpegQuality(int quality);
    unsigned char*  getJpeg(int*, unsigned int*);
    int             getSnapshotAndJpeg(unsigned char *yuv_buf, unsigned char *jpeg_buf,
                                        unsigned int *output_size);
    int             getExif(unsigned char *pExifDst, unsigned char *pThumbSrc);

#ifdef JPEG_FROM_SENSOR
    void            getPostViewConfig(int*, int*, int*);
#endif
    void            getThumbnailConfig(int *width, int *height, int *size);

#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
    int             getPostViewOffset(void);
#endif
    int             getCameraFd(void);
    int             getJpegFd(void);
    void            SetJpgAddr(unsigned char *addr);
    unsigned int    getPhyAddrY(int);
    unsigned int    getPhyAddrC(int);
#ifdef SEND_YUV_RECORD_DATA
    void            getYUVBuffers(unsigned char **virYAddr, unsigned char **virCaddr, int index);
#endif
    void            pausePreview();
    int             initCamera(int index);
    void            DeinitCamera();
    static void     setJpegRatio(double ratio)
    {
        if((ratio < 0) || (ratio > 1))
            return;

        jpeg_ratio = ratio;
    }

    static double   getJpegRatio()
    {
        return jpeg_ratio;
    }

    static void     setInterleaveDataSize(int x)
    {
        interleaveDataSize = x;
    }

    static int      getInterleaveDataSize()
    {
        return interleaveDataSize;
    }

private:
    int             m_flag_init;

    int             m_camera_id;

    int             m_cam_fd;

    int             m_cam_fd_temp;
    int             m_cam_fd2_temp;
#ifdef DUAL_PORT_RECORDING
    int             m_cam_fd2;
    struct pollfd   m_events_c2;
    int             m_flag_record_start;
    struct          fimc_buffer m_buffers_c2[MAX_BUFFERS];
#endif

    int             m_preview_v4lformat;
    int             m_preview_width;
    int             m_preview_height;
    int             m_preview_max_width;
    int             m_preview_max_height;

    int             m_snapshot_v4lformat;
    int             m_snapshot_width;
    int             m_snapshot_height;
    int             m_snapshot_max_width;
    int             m_snapshot_max_height;

    int             m_angle;
    int             m_fps;
    int             m_autofocus;
    int             m_white_balance;
    int             m_brightness;
    int             m_image_effect;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    int             m_anti_banding;
    int             m_scene_mode;
    int             m_flash_mode;
//  int             m_iso;
    int             m_metering;
    int             m_contrast;
    int             m_saturation;
    int             m_sharpness;
    int             m_wdr;
    int             m_anti_shake;
//  int             m_jpeg_quality;
    int             m_zoom_level;
    int             m_object_tracking;
    int             m_smart_auto;
    int             m_beauty_shot;
    int             m_vintage_mode;
//  int             m_focus_mode;
    int             m_face_detect;
    int             m_object_tracking_start_stop;
    int             m_recording_width;
    int             m_recording_height;
    long            m_gps_latitude;
    long            m_gps_longitude;
    long            m_gps_altitude;
    long            m_gps_timestamp;
    int             m_vtmode;
    int             m_sensor_mode; /*Camcorder fix fps */
    int             m_shot_mode; /* Shot mode */
    int             m_exif_orientation;
    int             m_blur_level;
    int             m_chk_dataline;
    int             m_video_gamma;
    int             m_slow_ae;
    int             m_caf_on_off;
    int             m_default_imei;
    int             m_camera_af_flag;
#endif

    int             m_flag_camera_start;

    int             m_jpeg_fd;
    int             m_jpeg_thumbnail_width;
    int             m_jpeg_thumbnail_height;
    int             m_jpeg_quality;

    int             m_postview_offset;

    exif_attribute_t mExifInfo;

    struct fimc_buffer m_buffers_c[MAX_BUFFERS];
    struct pollfd   m_events_c;

    inline int      m_frameSize(int format, int width, int height);

    void            setExifChangedAttribute();
    void            setExifFixedAttribute();
    void            resetCamera();

    static double   jpeg_ratio;
    static int      interleaveDataSize;
};

extern unsigned long measure_time(struct timeval *start, struct timeval *stop);

}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_SEC_H
