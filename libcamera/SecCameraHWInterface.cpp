/*
**
** Copyright 2008, The Android Open Source Project
** Copyright@ Samsung Electronics Co. LTD
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

//#define LOG_NDEBUG 0
#define LOG_TAG "CameraHardwareSec"
#include <utils/Log.h>

#include "SecCameraHWInterface.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#if defined(BOARD_USES_OVERLAY)
#include <hardware/overlay.h>
#include <ui/Overlay.h>
#define CACHEABLE_BUFFERS       0x1
#define ALL_BUFFERS_FLUSHED     -66
#endif

#ifdef SEND_YUV_RECORD_DATA
#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)
#define RECORD_HEAP_SIZE (ALIGN_TO_8KB(ALIGN_TO_128B(1280) *        \
            ALIGN_TO_32B(720)) + ALIGN_TO_8KB(ALIGN_TO_128B(1280) * \
            ALIGN_TO_32B(720 / 2)))
#endif

#define VIDEO_COMMENT_MARKER_H          0xFFBE
#define VIDEO_COMMENT_MARKER_L          0xFFBF
#define VIDEO_COMMENT_MARKER_LENGTH     4
#define JPEG_EOI_MARKER                 0xFFD9
#define HIBYTE(x) (((x) >> 8) & 0xFF)
#define LOBYTE(x) ((x) & 0xFF)

/* TBD: placeholder values, to be adjusted */
#define BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR       "0.50,1.9,Infinity"
#define BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR      "0.10,0.30,Infinity"
#define BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR   "0.50,0.50,Infinity"
#define FRONT_CAMERA_FOCUS_DISTANCES_STR           "0.30,0.30,Infinity"

namespace android {

struct addrs {
    unsigned int addr_y;
    unsigned int addr_cbcr;
    unsigned int buf_index;
    unsigned int reserved;
};

struct addrs_cap {
    unsigned int addr_y;
    unsigned int width;
    unsigned int height;
};

CameraHardwareSec::CameraHardwareSec(int cameraId)
        : mParameters(),
          mPreviewHeap(0),
          mRawHeap(0),
          mRecordHeap(0),
          mJpegHeap(0),
          mSecCamera(NULL),
          mPreviewRunning(false),
          mPreviewFrameSize(0),
          mRawFrameSize(0),
          mPreviewFrameRateMicrosec(33000),
          mCameraSensorName(NULL),
#if defined(BOARD_USES_OVERLAY)
          mUseOverlay(false),
          mOverlayBufferIdx(0),
#endif
          mNotifyCb(0),
          mDataCb(0),
          mDataCbTimestamp(0),
          mCallbackCookie(0),
          mMsgEnabled(0),
          mCurrentPreviewFrame(0),
          mRecordRunning(false)
#ifdef JPEG_FROM_SENSOR
          ,
          mPostViewWidth(0),
          mPostViewHeight(0),
          mPostViewSize(0)
#endif
{
    LOGV("%s :", __func__);
    int ret = 0;
    mNoHwHandle = 0;

    mSecCamera = SecCamera::createInstance();

    if (mSecCamera == NULL) {
        LOGE("ERR(%s):Fail on mSecCamera object creation", __func__);
    }

    ret = mSecCamera->initCamera(cameraId);
    if (ret < 0) {
        LOGE("ERR(%s):Fail on mSecCamera init", __func__);
    }

    if (mSecCamera->flagCreate() == 0) {
        LOGE("ERR(%s):Fail on mSecCamera->flagCreate()", __func__);
    }

#ifndef PREVIEW_USING_MMAP
    int previewHeapSize = sizeof(struct addrs) * kBufferCount;

    LOGV("mPreviewHeap : MemoryHeapBase(previewHeapSize(%d))", previewHeapSize);
    mPreviewHeap = new MemoryHeapBase(previewHeapSize);
    if (mPreviewHeap->getHeapID() < 0) {
            LOGE("ERR(%s): Preview heap creation fail", __func__);
                mPreviewHeap.clear();
    }
#endif

#ifdef SEND_YUV_RECORD_DATA
    int recordHeapSize = RECORD_HEAP_SIZE;
#else
    int recordHeapSize = sizeof(struct addrs) * kBufferCount;
#endif
    LOGV("mRecordHeap : MemoryHeapBase(recordHeapSize(%d))", recordHeapSize);
    mRecordHeap = new MemoryHeapBase(recordHeapSize);
    if (mRecordHeap->getHeapID() < 0) {
            LOGE("ERR(%s): Record heap creation fail", __func__);
                mRecordHeap.clear();
    }

#ifdef JPEG_FROM_SENSOR
    mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    LOGV("mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
            mPostViewWidth,mPostViewHeight,mPostViewSize);
#endif

#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
    int rawHeapSize = mPostViewSize;
#else
    int rawHeapSize = sizeof(struct addrs_cap);
#endif
    LOGV("mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
    mRawHeap = new MemoryHeapBase(rawHeapSize);
    if (mRawHeap->getHeapID() < 0) {
            LOGE("ERR(%s): Raw heap creation fail", __func__);
                mRawHeap.clear();
    }

    initDefaultParameters(cameraId);
}

void CameraHardwareSec::initDefaultParameters(int cameraId)
{
    if (mSecCamera == NULL) {
        LOGE("ERR(%s):mSecCamera object is NULL", __func__);
        return;
    }

    CameraParameters p;
    CameraParameters ip;

    mCameraSensorName = mSecCamera->getCameraSensorName();
    LOGV("CameraSensorName: %s", mCameraSensorName);

    int preview_max_width   = 0;
    int preview_max_height  = 0;
    int snapshot_max_width  = 0;
    int snapshot_max_height = 0;

    ip.set("camera-id", cameraId);
    /* set camera ID & reset camera */
    mSecCamera->setCameraId(cameraId);
    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
              "640x480,720x480,352x288,176x144");
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
              "2560x1920,2048x1536,1600x1200,1024x768,640x480");
    } else {
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
              "640x480");
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
              "640x480");
    }

    // If these fail, then we are using an invalid cameraId and we'll leave the
    // sizes at zero to catch the error.
    if (mSecCamera->getPreviewMaxSize(&preview_max_width,
                                      &preview_max_height) < 0)
        LOGE("getPreviewMaxSize fail (%d / %d) \n",
             preview_max_width, preview_max_height);
    if (mSecCamera->getSnapshotMaxSize(&snapshot_max_width,
                                       &snapshot_max_height) < 0)
        LOGE("getSnapshotMaxSize fail (%d / %d) \n",
             snapshot_max_width, snapshot_max_height);

#ifdef PREVIEW_USING_MMAP
    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
#else
    p.setPreviewFormat("yuv420sp_custom");
#endif
    p.setPreviewSize(preview_max_width, preview_max_height);
    p.setPreviewFrameRate(30);

    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.setPictureSize(snapshot_max_width, snapshot_max_height);
    p.set(CameraParameters::KEY_JPEG_QUALITY, "100"); // maximum quality
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
          CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
          "160x120,0x0");
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "7500,30000");

    String8 parameterString;

    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString = CameraParameters::FOCUS_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::FOCUS_MODE_INFINITY);
        parameterString.append(",");
        parameterString.append(CameraParameters::FOCUS_MODE_MACRO);
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              parameterString.string());
        p.set(CameraParameters::KEY_FOCUS_MODE,
              CameraParameters::FOCUS_MODE_AUTO);
        p.set(CameraParameters::KEY_FOCUS_DISTANCES,
              BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
    } else {
        parameterString = CameraParameters::FOCUS_MODE_FIXED;
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              parameterString.string());
        p.set(CameraParameters::KEY_FOCUS_MODE,
              CameraParameters::FOCUS_MODE_FIXED);
        p.set(CameraParameters::KEY_FOCUS_DISTANCES,
              FRONT_CAMERA_FOCUS_DISTANCES_STR);
    }

    parameterString = CameraParameters::EFFECT_NONE;
    parameterString.append(",");
    parameterString.append(CameraParameters::EFFECT_MONO);
    parameterString.append(",");
    parameterString.append(CameraParameters::EFFECT_NEGATIVE);
    parameterString.append(",");
    parameterString.append(CameraParameters::EFFECT_SEPIA);
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, parameterString.string());

    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString = CameraParameters::FLASH_MODE_ON;
        parameterString.append(",");
        parameterString.append(CameraParameters::FLASH_MODE_OFF);
        parameterString.append(",");
        parameterString.append(CameraParameters::FLASH_MODE_AUTO);
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
              parameterString.string());
        p.set(CameraParameters::KEY_FLASH_MODE,
              CameraParameters::FLASH_MODE_OFF);

        parameterString = CameraParameters::SCENE_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_PORTRAIT);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_LANDSCAPE);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_NIGHT);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_BEACH);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SNOW);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SUNSET);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_FIREWORKS);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SPORTS);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_PARTY);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_CANDLELIGHT);
        p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
              parameterString.string());

        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(3780,30000)");

    } else {
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(7500,30000)");
    }

    parameterString = CameraParameters::WHITE_BALANCE_AUTO;
    parameterString.append(",");
    parameterString.append(CameraParameters::WHITE_BALANCE_INCANDESCENT);
    parameterString.append(",");
    parameterString.append(CameraParameters::WHITE_BALANCE_FLUORESCENT);
    parameterString.append(",");
    parameterString.append(CameraParameters::WHITE_BALANCE_DAYLIGHT);
    parameterString.append(",");
    parameterString.append(CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
          parameterString.string());

    ip.set("sharpness-min", 0);
    ip.set("sharpness-max", 4);
    ip.set("saturation-min", 0);
    ip.set("saturation-max", 4);
    ip.set("contrast-min", 0);
    ip.set("contrast-max", 4);
#endif

    // These values must be multiples of 16, so we can't do 427x320, which is the exact size on
    // screen we want to display at. 480x360 doesn't work either since it's a multiple of 8.
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

    p.set(CameraParameters::KEY_ROTATION, 0);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);
    p.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);

    ip.set("sharpness", 2);
    ip.set("contrast", 2);
    ip.set("saturation", 2);
    ip.set("iso", "auto");
    ip.set("metering", "center");

    ip.set("wdr", 0);
    ip.set("chk_dataline", 0);
    if (cameraId == SecCamera::CAMERA_ID_FRONT) {
        ip.set("vtmode", 0);
        ip.set("blur", 0);
    }
#else
    ip.set("image-effects", "original");
#endif

    p.set(CameraParameters::KEY_FOCAL_LENGTH, "3.79");

    p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "1.0");

    mParameters = p;
    mInternalParameters = ip;
}

CameraHardwareSec::~CameraHardwareSec()
{
    LOGV("%s :", __func__);

    mSecCamera->DeinitCamera();

    if (mRawHeap != NULL)
        mRawHeap.clear();

    if (mJpegHeap != NULL)
        mJpegHeap.clear();

    if (mPreviewHeap != NULL)
        mPreviewHeap.clear();

    if (mRecordHeap != NULL)
        mRecordHeap.clear();

#if defined(BOARD_USES_OVERLAY)
    if (mUseOverlay) {
        mOverlay->destroy();
        mUseOverlay = false;
        mOverlay = NULL;
    }
#endif

    mSecCamera = NULL;

    singleton.clear();
}

sp<IMemoryHeap> CameraHardwareSec::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHardwareSec::getRawHeap() const
{
    return mRawHeap;
}

//Kamat added: New code as per eclair framework
void CameraHardwareSec::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void *user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void CameraHardwareSec::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHardwareSec::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHardwareSec::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------

int CameraHardwareSec::previewThread()
{
    int index;

    index = mSecCamera->getPreview();
    if (index < 0) {
        LOGE("ERR(%s):Fail on SecCamera->getPreview()", __func__);
        return UNKNOWN_ERROR;
    }

    nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);

#ifdef PREVIEW_USING_MMAP
    int width, height, frame_size, offset;

    mSecCamera->getPreviewSize(&width, &height, &frame_size);

    offset = (frame_size + 16) * index;
        sp<MemoryBase> buffer = new MemoryBase(mPreviewHeap, offset, frame_size);

    unsigned int phyYAddr = mSecCamera->getPhyAddrY(index);
    unsigned int phyCAddr = mSecCamera->getPhyAddrC(index);

    if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
        LOGE("ERR(%s):Fail on SecCamera. Invalid PhyAddr, Y addr = %0x C addr = %0x",
                __func__, phyYAddr, phyCAddr);
        return UNKNOWN_ERROR;
    }
    memcpy(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset + frame_size    ), &phyYAddr, 4);
    memcpy(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset + frame_size + 4), &phyCAddr, 4);

#if defined(BOARD_USES_OVERLAY)
    if (mUseOverlay) {
        int ret;
        mOverlayBufferIdx ^= 1;
        memcpy(static_cast<unsigned char*>(mPreviewHeap->base()) + offset + frame_size + sizeof(phyYAddr) + sizeof(phyCAddr),
                    &mOverlayBufferIdx, sizeof(mOverlayBufferIdx));

        ret = mOverlay->queueBuffer((void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset + frame_size)));

        if (ret == ALL_BUFFERS_FLUSHED) {
            goto OverlayEnd;
        } else if (ret == -1) {
            LOGE("ERR(%s):overlay queueBuffer fail", __func__);
            goto OverlayEnd;
        }

        overlay_buffer_t overlay_buffer;
        ret = mOverlay->dequeueBuffer(&overlay_buffer);

        if (ret == ALL_BUFFERS_FLUSHED) {
            goto OverlayEnd;
        } else if (ret == -1) {
            LOGE("ERR(%s):overlay dequeueBuffer fail", __func__);
            goto OverlayEnd;
        }
    }

OverlayEnd:
#endif

#else
    unsigned int phyYAddr = mSecCamera->getPhyAddrY(index);
    unsigned int phyCAddr = mSecCamera->getPhyAddrC(index);

    if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
        LOGE("ERR(%s):Fail on SecCamera getPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
        return UNKNOWN_ERROR;
    }
    struct addrs *addrs = (struct addrs *)mPreviewHeap->base();

    sp<MemoryBase> buffer = new MemoryBase(mPreviewHeap, index * sizeof(struct addrs), sizeof(struct addrs));
    addrs[index].addr_y = phyYAddr;
    addrs[index].addr_cbcr = phyCAddr;
#endif //PREVIEW_USING_MMAP

    // Notify the client of a new frame. //Kamat --eclair
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, buffer, mCallbackCookie);
    }

    if (mRecordRunning == true) {
#ifdef SEND_YUV_RECORD_DATA
        int width, height, frame_size;
        unsigned char *virYAddr;
        unsigned char *virCAddr;

        mSecCamera->getPreviewSize(&width, &height, &frame_size);
        mSecCamera->getYUVBuffers(&virYAddr, &virCAddr, index);
        sp<MemoryBase> buffer = new MemoryBase(mRecordHeap, 0, frame_size);
        //memcpy(mRecordHeap->base(), (void*)virYAddr, width * height);
        //memcpy(mRecordHeap->base() + (width*height),(void*)virCAddr, width * height * 0.5);
        memcpy(mRecordHeap->base(), (void*)virYAddr, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
        memcpy(mRecordHeap->base() + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)),
                (void*)virCAddr, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2)));
#else
#ifdef DUAL_PORT_RECORDING
        int index = mSecCamera->getRecord();

        if (index < 0) {
            LOGE("ERR(%s):Fail on SecCamera->getRecord()", __func__);
            return UNKNOWN_ERROR;
        }

        unsigned int phyYAddr = mSecCamera->getRecPhyAddrY(index);
        unsigned int phyCAddr = mSecCamera->getRecPhyAddrC(index);

        if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
            LOGE("ERR(%s):Fail on SecCamera getRectPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
            return UNKNOWN_ERROR;
        }
#endif//DUAL_PORT_RECORDING
        struct addrs *addrs = (struct addrs *)mRecordHeap->base();

        sp<MemoryBase> buffer = new MemoryBase(mRecordHeap, index * sizeof(struct addrs), sizeof(struct addrs));
        addrs[index].addr_y = phyYAddr;
        addrs[index].addr_cbcr = phyCAddr;
#endif
        // Notify the client of a new frame. //Kamat --eclair
        if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
            //nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, buffer, mCallbackCookie);
        }
#ifdef DUAL_PORT_RECORDING
    } else if (mRecordRunning == false) {
        if (mSecCamera->stopRecord() < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->stopRecord()", __func__);
            return UNKNOWN_ERROR;
        }
#endif
    }

    // Wait for it...
    if (mTimeStart.tv_sec == 0 && mTimeStart.tv_usec == 0) {
        gettimeofday(&mTimeStart, NULL);
    } else {
        gettimeofday(&mTimeStop, NULL);
        long time = measure_time(&mTimeStart, &mTimeStop);
        int delay = (mPreviewFrameRateMicrosec > time) ? mPreviewFrameRateMicrosec - time : 0;

        usleep(delay);
        //LOG_CAMERA_PREVIEW("delay = %d time = %ld us\n ", delay, time);
        gettimeofday(&mTimeStart, NULL);
    }

    return NO_ERROR;
}

status_t CameraHardwareSec::startPreview()
{
    int ret = 0;        //s1 [Apply factory standard]

    LOGV("%s :", __func__);

    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }

    memset(&mTimeStart, 0, sizeof(mTimeStart));
    memset(&mTimeStop, 0, sizeof(mTimeStop));

    mSecCamera->stopPreview();

    ret  = mSecCamera->startPreview();
    LOGV("%s : return startPreview %d", __func__, ret);

    if (ret < 0) {
        LOGE("ERR(%s):Fail on mSecCamera->startPreview()", __func__);
        if (mMsgEnabled & CAMERA_MSG_ERROR) {
                mNotifyCb(CAMERA_MSG_ERROR, -2, 0, mCallbackCookie);
        }
        return -1; //UNKNOWN_ERROR;
    }

#ifdef PREVIEW_USING_MMAP
    if (mPreviewHeap != NULL)
        mPreviewHeap.clear();

    int width, height, frame_size;

    mSecCamera->getPreviewSize(&width, &height, &frame_size);

    int previewHeapSize = (frame_size + 16) * kBufferCount;

    LOGD("MemoryHeapBase(fd(%d), size(%d), width(%d), height(%d))", (int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), width, height);
    mPreviewHeap = new MemoryHeapBase((int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), (uint32_t)0);
#endif

#ifdef JPEG_FROM_SENSOR
    mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    LOGV("CameraHardwareSec: mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",mPostViewWidth,mPostViewHeight,mPostViewSize);
#endif

#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
    int rawHeapSize = mPostViewSize;
#else
    int rawHeapSize = sizeof(struct addrs_cap);
#endif
    LOGV("CameraHardwareSec: mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
    mRawHeap = new MemoryHeapBase(rawHeapSize);
    if (mRawHeap->getHeapID() < 0) {
        LOGE("ERR(%s): Raw heap creation fail", __func__);
            mRawHeap.clear();
    }

    mPreviewRunning = true;
    mPreviewThread = new PreviewThread(this);

    return NO_ERROR;
}

#if defined(BOARD_USES_OVERLAY)
bool CameraHardwareSec::useOverlay()
{
    return true;
}

status_t CameraHardwareSec::setOverlay(const sp<Overlay> &overlay)
{
    LOGV("%s :", __func__);

    int overlayWidth  = 0;
    int overlayHeight = 0;
    int overlayFrameSize = 0;

    if (overlay == NULL) {
        goto setOverlayFail;
    }

    if (overlay->getHandleRef()== NULL && mUseOverlay == true) {
        if (mOverlay != 0)
            mOverlay->destroy();

        mOverlay = NULL;
        mUseOverlay = false;

        return NO_ERROR;
    }

    if (overlay->getStatus() != NO_ERROR) {
        LOGE("ERR(%s):overlay->getStatus() fail", __func__);
        goto setOverlayFail;
    }

    mSecCamera->getPreviewSize(&overlayWidth, &overlayHeight, &overlayFrameSize);

    if (overlay->setCrop(0, 0, overlayWidth, overlayHeight) != NO_ERROR) {
        LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __func__, overlayWidth, overlayHeight);
        goto setOverlayFail;
    }

    mOverlay = overlay;
    mUseOverlay = true;

    return NO_ERROR;

setOverlayFail :
    if (mOverlay != 0)
        mOverlay->destroy();
    mOverlay = 0;

    mUseOverlay = false;

    return UNKNOWN_ERROR;
}
#endif

void CameraHardwareSec::stopPreview()
{
    LOGV("%s :", __func__);

    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();

    if (!mNoHwHandle)
        if (mSecCamera->stopPreview() < 0)
            LOGE("ERR(%s):Fail on mSecCamera->stopPreview()", __func__);

    mPreviewRunning = false;
}

bool CameraHardwareSec::previewEnabled()
{
    LOGV("%s : %d", __func__, mPreviewThread != 0);
    return mPreviewThread != 0;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSec::startRecording()
{
    LOGV("%s :", __func__);

#ifdef DUAL_PORT_RECORDING
        if (mSecCamera->startRecord() < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->startRecord()", __func__);
            return UNKNOWN_ERROR;
        }
#endif

    mRecordRunning = true;
    return NO_ERROR;
}

void CameraHardwareSec::stopRecording()
{
    LOGV("%s :", __func__);

    mRecordRunning = false;
}

bool CameraHardwareSec::recordingEnabled()
{
    LOGV("%s :", __func__);

    return mRecordRunning;
}

void CameraHardwareSec::releaseRecordingFrame(const sp<IMemory>& mem)
{
    LOG_CAMERA_PREVIEW("%s :", __func__);

//    ssize_t offset; size_t size;
//    sp<MemoryBase>       mem1 = mem;
//    sp<MemoryHeapBase> heap = mem->getMemory(&offset, &size);
//    sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);

//    mem1.clear();
//    heap.clear();
}

// ---------------------------------------------------------------------------

int CameraHardwareSec::beginAutoFocusThread(void *cookie)
{
    LOGV("%s :", __func__);
    CameraHardwareSec *c = (CameraHardwareSec *)cookie;
    return c->autoFocusThread();
}

int CameraHardwareSec::autoFocusThread()
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    int count =0;
    int af_status =0 ;
#endif

    LOGV("%s :", __func__);
    if (mSecCamera->setAutofocus() < 0) {
        LOGE("ERR(%s):Fail on mSecCamera->setAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    af_status = mSecCamera->getAutoFocusResult();

    if (af_status == 0x01) {
        LOGV("%s : AF Success!!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
    } else if (af_status == 0x02) {
        LOGV("%s : AF Cancelled !!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS) {
           /* CAMERA_MSG_FOCUS only takes a bool.  true for
            * finished and false for failure.  cancel is still
            * considered a true result.
            */
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
        }
    } else {
        LOGV("%s : AF Fail !!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
    }
#else
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
#endif
    return NO_ERROR;
}

status_t CameraHardwareSec::autoFocus()
{
    LOGV("%s :", __func__);
    Mutex::Autolock lock(mLock);
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

/* 2009.10.14 by icarus for added interface */
status_t CameraHardwareSec::cancelAutoFocus()
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    LOGV("%s :", __func__);

    if (mSecCamera->cancelAutofocus() < 0) {
        LOGE("ERR(%s):Fail on mSecCamera->cancelAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }
#endif
    return NO_ERROR;
}

int CameraHardwareSec::save_jpeg( unsigned char *real_jpeg, int jpeg_size)
{
    FILE *yuv_fp = NULL;
    char filename[100], *buffer = NULL;

    /* file create/open, note to "wb" */
    yuv_fp = fopen("/data/camera_dump.jpeg", "wb");
    if (yuv_fp == NULL) {
        LOGE("Save jpeg file open error");
        return -1;
    }

    LOGV("[BestIQ]  real_jpeg size ========>  %d\n", jpeg_size);
    buffer = (char *) malloc(jpeg_size);
    if (buffer == NULL) {
        LOGE("Save YUV] buffer alloc failed");
        if (yuv_fp)
            fclose(yuv_fp);

        return -1;
    }

    memcpy(buffer, real_jpeg, jpeg_size);

    fflush(stdout);

    fwrite(buffer, 1, jpeg_size, yuv_fp);

    fflush(yuv_fp);

    if (yuv_fp)
            fclose(yuv_fp);
    if (buffer)
            free(buffer);

    return 0;
}

/*static*/ int CameraHardwareSec::beginPictureThread(void *cookie)
{
    LOGV("%s :", __func__);
    CameraHardwareSec *c = (CameraHardwareSec *)cookie;
    return c->pictureThread();
}

void CameraHardwareSec::save_postview(const char *fname, uint8_t *buf, uint32_t size)
{
    int nw;
    int cnt = 0;
    uint32_t written = 0;

    LOGD("opening file [%s]\n", fname);
    int fd = open(fname, O_RDWR | O_CREAT);
    if (fd < 0) {
        LOGE("failed to create file [%s]: %s", fname, strerror(errno));
    return;
    }

    LOGD("writing %d bytes to file [%s]\n", size, fname);
    while (written < size) {
        nw = ::write(fd, buf + written, size - written);
        if (nw < 0) {
            LOGE("failed to write to file %d [%s]: %s",written,fname, strerror(errno));
            break;
        }
        written += nw;
        cnt++;
    }
    LOGD("done writing %d bytes to file [%s] in %d passes\n",size, fname, cnt);
    ::close(fd);
}

bool CameraHardwareSec::scaleDownYuv422(char *srcBuf, uint32_t srcWidth, uint32_t srcHeight,
                                        char *dstBuf, uint32_t dstWidth, uint32_t dstHeight)
{
    int32_t step_x, step_y;
    int32_t iXsrc, iXdst;
    int32_t x, y, src_y_start_pos, dst_pos, src_pos;

    if (dstWidth % 2 != 0 || dstHeight % 2 != 0){
        LOGE("scale_down_yuv422: invalid width, height for scaling");
        return false;
    }

    step_x = srcWidth / dstWidth;
    step_y = srcHeight / dstHeight;

    dst_pos = 0;
    for (uint32_t y = 0; y < dstHeight; y++) {
        src_y_start_pos = (y * step_y * (srcWidth * 2));

        for (uint32_t x = 0; x < dstWidth; x += 2) {
            src_pos = src_y_start_pos + (x * (step_x * 2));

            dstBuf[dst_pos++] = srcBuf[src_pos    ];
            dstBuf[dst_pos++] = srcBuf[src_pos + 1];
            dstBuf[dst_pos++] = srcBuf[src_pos + 2];
            dstBuf[dst_pos++] = srcBuf[src_pos + 3];
        }
    }

    return true;
}

bool CameraHardwareSec::YUY2toNV21(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight)
{
    int32_t        x, y, src_y_start_pos, dst_cbcr_pos, dst_pos, src_pos;
    unsigned char *srcBufPointer = (unsigned char *)srcBuf;
    unsigned char *dstBufPointer = (unsigned char *)dstBuf;

    dst_pos = 0;
    dst_cbcr_pos = srcWidth*srcHeight;
    for (uint32_t y = 0; y < srcHeight; y++) {
        src_y_start_pos = (y * (srcWidth * 2));

        for (uint32_t x = 0; x < (srcWidth * 2); x += 2) {
            src_pos = src_y_start_pos + x;

            dstBufPointer[dst_pos++] = srcBufPointer[src_pos];
        }
    }
    for (uint32_t y = 0; y < srcHeight; y += 2) {
        src_y_start_pos = (y * (srcWidth * 2));

        for (uint32_t x = 0; x < (srcWidth * 2); x += 4) {
            src_pos = src_y_start_pos + x;

            dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 3];
            dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 1];
        }
    }

    return true;
}


int CameraHardwareSec::pictureThread()
{
    LOGV("%s :", __func__);

    int jpeg_size = 0;
    int ret = NO_ERROR;
    unsigned char *jpeg_data = NULL;
    int postview_offset = 0;
    unsigned char *postview_data = NULL;

    //unsigned int addr;
    unsigned char *addr = NULL;
    int mPostViewWidth, mPostViewHeight, mPostViewSize;
    int mThumbWidth, mThumbHeight, mThumbSize;
    int cap_width, cap_height, cap_frame_size;

    unsigned int output_size = 0;

    mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mSecCamera->getThumbnailConfig(&mThumbWidth, &mThumbHeight, &mThumbSize);
    int postviewHeapSize = mPostViewSize;
    mSecCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
    int mJpegHeapSize;
    if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK)
        mJpegHeapSize = cap_frame_size * SecCamera::getJpegRatio();
    else
        mJpegHeapSize = cap_frame_size;

    LOG_TIME_DEFINE(0)
    LOG_TIME_START(0)
#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
    sp<MemoryBase> buffer = new MemoryBase(mRawHeap, 0, mPostViewSize + 8);
#else
    sp<MemoryBase> buffer = new MemoryBase(mRawHeap, 0, sizeof(struct addrs_cap));
#endif

    struct addrs_cap *addrs = (struct addrs_cap *)mRawHeap->base();

#ifdef JPEG_FROM_SENSOR
    addrs[0].width = mPostViewWidth;
    addrs[0].height = mPostViewHeight;
    LOGV("[5B] mPostViewWidth = %d mPostViewHeight = %d\n",mPostViewWidth,mPostViewHeight);
#else
    mParameters.getPictureSize((int*)&addrs[0].width, (int*)&addrs[0].height);
#endif

    sp<MemoryHeapBase> JpegHeap = new MemoryHeapBase(mJpegHeapSize);
    sp<MemoryHeapBase> PostviewHeap = new MemoryHeapBase(mPostViewSize);
    sp<MemoryHeapBase> ThumbnailHeap = new MemoryHeapBase(mThumbSize);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        LOG_TIME_DEFINE(1)
        LOG_TIME_START(1)

        int picture_size, picture_width, picture_height;
        mSecCamera->getSnapshotSize(&picture_width, &picture_height, &picture_size);
        int picture_format = mSecCamera->getSnapshotPixelFormat();

        unsigned int phyAddr;
#ifdef JPEG_FROM_SENSOR
        // Modified the shutter sound timing for Jpeg capture
        if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK)
            mSecCamera->setSnapshotCmd();
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
        if (mMsgEnabled & CAMERA_MSG_SHUTTER) {
            mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
        }
#endif

        if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK){
            jpeg_data = mSecCamera->getJpeg(&jpeg_size, &phyAddr);
            if (jpeg_data == NULL) {
                LOGE("ERR(%s):Fail on SecCamera->getSnapshot()", __func__);
                ret = UNKNOWN_ERROR;
            }
        } else {
            if (mSecCamera->getSnapshotAndJpeg((unsigned char*)PostviewHeap->base(),
                    (unsigned char*)JpegHeap->base(), &output_size) < 0)
                return UNKNOWN_ERROR;
            LOGE("snapshotandjpeg done\n");
        }
#else
        phyAddr = mSecCamera->getSnapshotAndJpeg();
        if (phyAddr < 0)
            return UNKNOWN_ERROR;

        jpeg_data = mSecCamera->yuv2Jpeg((unsigned char*)phyAddr, 0, &jpeg_size,
                                        picture_width, picture_height, picture_format);
#endif

#ifndef DIRECT_DELIVERY_OF_POSTVIEW_DATA

        addrs[0].addr_y = phyAddr;
#endif

        LOG_TIME_END(1)
        LOG_CAMERA("getSnapshotAndJpeg interval: %lu us", LOG_TIME(1));
    }

    int JpegImageSize, JpegExifSize;
    bool isLSISensor = false;

    isLSISensor = !strncmp((const char*)mCameraSensorName, "S5K4ECGX", 8);
    if (isLSISensor)
        LOGE("== Camera Sensor Detect LSI SOC 5M ==\n");
    else
        LOGE("== Camera Sensor Detect Sony SOC 5M ==\n");

    if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) {
        if(isLSISensor) {
            // LSI 5M SOC
            SplitFrame(jpeg_data, SecCamera::getInterleaveDataSize(),
                       SecCamera::getJpegLineLength(),
                       mPostViewWidth * 2, mPostViewWidth,
                       JpegHeap->base(), &JpegImageSize,
                       PostviewHeap->base(), &mPostViewSize);
        } else {
            decodeInterleaveData(jpeg_data, SecCamera::getInterleaveDataSize(), mPostViewWidth, mPostViewHeight,
                                &JpegImageSize, JpegHeap->base(), PostviewHeap->base());

        }
        scaleDownYuv422((char *)PostviewHeap->base(), mPostViewWidth, mPostViewHeight,
                        (char *)ThumbnailHeap->base(), mThumbWidth, mThumbHeight);
    }

#ifdef POSTVIEW_CALLBACK
    sp<MemoryBase> postview = new MemoryBase(PostviewHeap, 0, postviewHeapSize);
#endif
    memcpy(mRawHeap->base(),PostviewHeap->base(), postviewHeapSize);

#if defined(BOARD_USES_OVERLAY)
   /* Put postview image to Overlay */
    unsigned int index = 0;
    unsigned int offset = ((mPostViewWidth*mPostViewHeight*3/2) + 16) * index;
    unsigned int overlay_header[4];
    mOverlayBufferIdx ^= 1;
    overlay_header[0]= mSecCamera->getPhyAddrY(index);
    overlay_header[1]= overlay_header[0] + mPostViewWidth*mPostViewHeight;
    overlay_header[2]= mOverlayBufferIdx;

    YUY2toNV21(mRawHeap->base(), (void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + offset),
                mPostViewWidth, mPostViewHeight);

    memcpy(static_cast<unsigned char*>(mPreviewHeap->base()) + offset + (mPostViewWidth*mPostViewHeight * 3 / 2),
            overlay_header, 16);

    ret = mOverlay->queueBuffer((void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + offset +
                                (mPostViewWidth*mPostViewHeight * 3 / 2)));

    if (ret == ALL_BUFFERS_FLUSHED) {
        goto PostviewOverlayEnd;
    } else if (ret == -1) {
        LOGE("ERR(%s):overlay queueBuffer fail", __func__);
        goto PostviewOverlayEnd;
    }

    overlay_buffer_t overlay_buffer;
    ret = mOverlay->dequeueBuffer(&overlay_buffer);

    if (ret == ALL_BUFFERS_FLUSHED) {
        goto PostviewOverlayEnd;
    } else if (ret == -1) {
        LOGE("ERR(%s):overlay dequeueBuffer fail", __func__);
        goto PostviewOverlayEnd;
    }

PostviewOverlayEnd:
#endif
    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        mDataCb(CAMERA_MSG_RAW_IMAGE, buffer, mCallbackCookie);
    }
#ifdef POSTVIEW_CALLBACK
    if (mMsgEnabled & CAMERA_MSG_POSTVIEW_FRAME) {
        int postviewHeapSize = mPostViewSize;
        sp<MemoryHeapBase> mPostviewHeap = new MemoryHeapBase(postviewHeapSize);

        postview_data = jpeg_data + postview_offset;
        sp<MemoryBase> postview = new MemoryBase(mPostviewHeap, 0, postviewHeapSize);

        if (postview_data != NULL)
            memcpy(mPostviewHeap->base(), postview_data, postviewHeapSize);

        mDataCb(CAMERA_MSG_POSTVIEW_FRAME, postview, mCallbackCookie);
    }
#endif
    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) {
            sp<MemoryHeapBase> ExifHeap = new MemoryHeapBase(EXIF_FILE_SIZE + JPG_STREAM_BUF_SIZE);
            JpegExifSize = mSecCamera->getExif((unsigned char *)ExifHeap->base(),
                    (unsigned char *)ThumbnailHeap->base());

            LOGE("JpegExifSize=%d", JpegExifSize);

            if (JpegExifSize < 0)
                return UNKNOWN_ERROR;

            unsigned char *ExifStart = (unsigned char *)JpegHeap->base() + 2;
            unsigned char *ImageStart = ExifStart + JpegExifSize;

            memmove(ImageStart, ExifStart, JpegImageSize - 2);
            memcpy(ExifStart, ExifHeap->base(), JpegExifSize);
            sp<MemoryBase> mem = new MemoryBase(JpegHeap, 0, JpegImageSize + JpegExifSize);

            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
        } else {
            sp<MemoryBase> mem = new MemoryBase(JpegHeap , 0, output_size);
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
        }
    }

    LOG_TIME_END(0)
    LOG_CAMERA("pictureThread interval: %lu us", LOG_TIME(0));

    return ret;
}

status_t CameraHardwareSec::takePicture()
{
    LOGV("%s :", __func__);

    stopPreview();
    mNoHwHandle = 0;

    if (createThread(beginPictureThread, this) == false)
        return -1;

    return NO_ERROR;
}

status_t CameraHardwareSec::cancelPicture()
{
    return NO_ERROR;
}

bool CameraHardwareSec::CheckVideoStartMarker(unsigned char *pBuf)
{
    if (!pBuf) {
        LOGE("CheckVideoStartMarker() => pBuf is NULL\n");
        return false;
    }

    if (HIBYTE(VIDEO_COMMENT_MARKER_H) == * pBuf      && LOBYTE(VIDEO_COMMENT_MARKER_H) == *(pBuf + 1) &&
        HIBYTE(VIDEO_COMMENT_MARKER_L) == *(pBuf + 2) && LOBYTE(VIDEO_COMMENT_MARKER_L) == *(pBuf + 3))
        return true;

    return false;
}

bool CameraHardwareSec::CheckEOIMarker(unsigned char *pBuf)
{
    if (!pBuf) {
        LOGE("CheckEOIMarker() => pBuf is NULL\n");
        return false;
    }

    // EOI marker [FF D9]
    if (HIBYTE(JPEG_EOI_MARKER) == *pBuf && LOBYTE(JPEG_EOI_MARKER) == *(pBuf + 1))
        return true;

    return false;
}

bool CameraHardwareSec::FindEOIMarkerInJPEG(unsigned char *pBuf, int dwBufSize, int *pnJPEGsize)
{
    if (NULL == pBuf || 0 >= dwBufSize) {
        LOGE("FindEOIMarkerInJPEG() => There is no contents.");
        return false;
    }

    unsigned char *pBufEnd = pBuf + dwBufSize;

    while (pBuf < pBufEnd) {
        if (CheckEOIMarker(pBuf++))
            return true;

        (*pnJPEGsize)++;
    }

    return false;
}

bool CameraHardwareSec::SplitFrame(unsigned char *pFrame, int dwSize,
                    int dwJPEGLineLength, int dwVideoLineLength, int dwVideoHeight,
                    void *pJPEG, int *pdwJPEGSize,
                    void *pVideo, int *pdwVideoSize)
{
    LOGV("===========SplitFrame Start==============");

    if (NULL == pFrame || 0 >= dwSize) {
        LOGE("There is no contents.\r\n");
        return false;
    }

    if (0 == dwJPEGLineLength || 0 == dwVideoLineLength) {
        LOGE("There in no input information for decoding interleaved jpeg");
        return false;
    }

    unsigned char *pSrc = pFrame;
    unsigned char *pSrcEnd = pFrame + dwSize;

    unsigned char *pJ = (unsigned char *)pJPEG;
    int dwJSize = 0;
    unsigned char *pV = (unsigned char *)pVideo;
    int dwVSize = 0;

    bool bRet = false;
    bool isFinishJpeg = false;

    while (pSrc < pSrcEnd) {
        // Check video start marker
        if (CheckVideoStartMarker(pSrc)) {
            // Copy video data
            if (pV) {
                memcpy(pV, pSrc + VIDEO_COMMENT_MARKER_LENGTH, dwVideoLineLength);
                pV += dwVideoLineLength;
                dwVSize += dwVideoLineLength;
            }
            pSrc += dwVideoLineLength + VIDEO_COMMENT_MARKER_LENGTH;
       } else {
            // Copy pure JPEG data
            int size = 0;
            int dwCopyBufLen = dwJPEGLineLength <= pSrcEnd-pSrc ? dwJPEGLineLength : pSrcEnd - pSrc;

            if (FindEOIMarkerInJPEG((unsigned char *)pSrc, dwCopyBufLen, &size)) {
                isFinishJpeg = true;
                size += 2;  // to count EOF marker size
            } else {
                if ((dwCopyBufLen == 1) && (pJPEG < pJ)) {
                    unsigned char checkBuf[2] = { *(pJ - 1), *pSrc };

                    if (CheckEOIMarker(checkBuf))
                        isFinishJpeg = true;
                }
                size = dwCopyBufLen;
            }

            memcpy(pJ, pSrc, size);

            dwJSize += size;

            pJ += dwCopyBufLen;
            pSrc += dwCopyBufLen;
        }

        if (isFinishJpeg)
            break;
    }

    if (isFinishJpeg) {
        bRet = true;
        if(pdwJPEGSize)
            *pdwJPEGSize = dwJSize;
        if(pdwVideoSize)
            *pdwVideoSize = dwVSize;
    } else {
        LOGE("DecodeInterleaveJPEG_WithOutDT() => Can not find EOI");
        bRet = false;
        if(pdwJPEGSize)
            *pdwJPEGSize = 0;
        if(pdwVideoSize)
            *pdwVideoSize = 0;
    }
    LOGV("===========SplitFrame end==============");

    return bRet;
}

int CameraHardwareSec::decodeInterleaveData(unsigned char *pInterleaveData,
                                                 int interleaveDataSize,
                                                 int yuvWidth,
                                                 int yuvHeight,
                                                 int *pJpegSize,
                                                 void *pJpegData,
                                                 void *pYuvData)
{
    if (pInterleaveData == NULL)
        return false;

    bool ret = true;
    unsigned int *interleave_ptr = (unsigned int *)pInterleaveData;
    unsigned char *jpeg_ptr = (unsigned char *)pJpegData;
    unsigned char *yuv_ptr = (unsigned char *)pYuvData;
    unsigned char *p;
    int jpeg_size = 0;
    int yuv_size = 0;

    int i = 0;

    LOGV("decodeInterleaveData Start~~~");
    while (i < interleaveDataSize) {
        if ((*interleave_ptr == 0xFFFFFFFF) || (*interleave_ptr == 0x02FFFFFF) ||
                (*interleave_ptr == 0xFF02FFFF)) {
            // Padding Data
//            LOGE("%d(%x) padding data\n", i, *interleave_ptr);
            interleave_ptr++;
            i += 4;
        }
        else if ((*interleave_ptr & 0xFFFF) == 0x05FF) {
            // Start-code of YUV Data
//            LOGE("%d(%x) yuv data\n", i, *interleave_ptr);
            p = (unsigned char *)interleave_ptr;
            p += 2;
            i += 2;

            // Extract YUV Data
            if (pYuvData != NULL) {
                memcpy(yuv_ptr, p, yuvWidth * 2);
                yuv_ptr += yuvWidth * 2;
                yuv_size += yuvWidth * 2;
            }
            p += yuvWidth * 2;
            i += yuvWidth * 2;

            // Check End-code of YUV Data
            if ((*p == 0xFF) && (*(p + 1) == 0x06)) {
                interleave_ptr = (unsigned int *)(p + 2);
                i += 2;
            } else {
                ret = false;
                break;
            }
        } else {
            // Extract JPEG Data
//            LOGE("%d(%x) jpg data, jpeg_size = %d bytes\n", i, *interleave_ptr, jpeg_size);
            if (pJpegData != NULL) {
                memcpy(jpeg_ptr, interleave_ptr, 4);
                jpeg_ptr += 4;
                jpeg_size += 4;
            }
            interleave_ptr++;
            i += 4;
        }
    }
    if (ret) {
        if (pJpegData != NULL) {
            // Remove Padding after EOI
            for (i = 0; i < 3; i++) {
                if (*(--jpeg_ptr) != 0xFF) {
                    break;
                }
                jpeg_size--;
            }
            *pJpegSize = jpeg_size;

        }
        // Check YUV Data Size
        if (pYuvData != NULL) {
            if (yuv_size != (yuvWidth * yuvHeight * 2)) {
                ret = false;
            }
        }
    }
    LOGV("decodeInterleaveData End~~~");
    return ret;
}

status_t CameraHardwareSec::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);

    if (mSecCamera != 0) {
        mSecCamera->dump(fd, args);
        mParameters.dump(fd, args);
        mInternalParameters.dump(fd, args);
        snprintf(buffer, 255, " preview frame(%d), size (%d), running(%s)\n", mCurrentPreviewFrame, mPreviewFrameSize, mPreviewRunning?"true": "false");
        result.append(buffer);
    } else {
        result.append("No camera client yet.\n");
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t CameraHardwareSec::setParameters(const CameraParameters& params)
{
    LOGV("%s :", __func__);

    Mutex::Autolock lock(mLock);

    status_t ret = NO_ERROR;

    mParameters = params;

    // set camera id
    int new_camera_id = mInternalParameters.getInt("camera-id");
    if (0 <= new_camera_id) {
        if (mSecCamera->setCameraId(new_camera_id) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setCameraId(camera_id(%d))", __func__, new_camera_id);
            ret = UNKNOWN_ERROR;
        }
    }

    // preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;
    params.getPreviewSize(&new_preview_width, &new_preview_height);
    const char *new_str_preview_format = params.getPreviewFormat();

    if (0 < new_preview_width && 0 < new_preview_height && new_str_preview_format != NULL) {
        int new_preview_format = 0;

        if (!strcmp(new_str_preview_format,
                    CameraParameters::PIXEL_FORMAT_RGB565))
            new_preview_format = V4L2_PIX_FMT_RGB565;
        else if (!strcmp(new_str_preview_format,
                         CameraParameters::PIXEL_FORMAT_YUV420SP))
            new_preview_format = V4L2_PIX_FMT_NV21; //Kamat
        else if (!strcmp(new_str_preview_format, "yuv420sp_custom"))
            new_preview_format = V4L2_PIX_FMT_NV12T; //Kamat
        else if (!strcmp(new_str_preview_format, "yuv420p"))
            new_preview_format = V4L2_PIX_FMT_YUV420;
        else if (!strcmp(new_str_preview_format, "yuv422i"))
            new_preview_format = V4L2_PIX_FMT_YUYV;
        else if (!strcmp(new_str_preview_format, "yuv422p"))
            new_preview_format = V4L2_PIX_FMT_YUV422P;
        else
            new_preview_format = V4L2_PIX_FMT_NV21; //for 3rd party

        if (mSecCamera->setPreviewSize(new_preview_width, new_preview_height, new_preview_format) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setPreviewSize(width(%d), height(%d), format(%d))",
                    __func__, new_preview_width, new_preview_height, new_preview_format);
            ret = UNKNOWN_ERROR;
        }
#if defined(BOARD_USES_OVERLAY)
        if (mUseOverlay == true && mOverlay != 0) {
            if (mOverlay->setCrop(0, 0, new_preview_width, new_preview_height) != NO_ERROR)     {
                LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail",
                        __func__, new_preview_width, new_preview_height);
            }
        }
#endif
    }

    int new_picture_width  = 0;
    int new_picture_height = 0;

    params.getPictureSize(&new_picture_width, &new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height) {
        if (mSecCamera->setSnapshotSize(new_picture_width, new_picture_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setSnapshotSize(width(%d), height(%d))",
                    __func__, new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        }
    }

    // picture format
    const char *new_str_picture_format = params.getPictureFormat();

    if (new_str_picture_format != NULL) {
        int new_picture_format = 0;

        if (!strcmp(new_str_picture_format, CameraParameters::PIXEL_FORMAT_RGB565))
            new_picture_format = V4L2_PIX_FMT_RGB565;
        else if (!strcmp(new_str_picture_format, CameraParameters::PIXEL_FORMAT_YUV420SP))
            new_picture_format = V4L2_PIX_FMT_NV21; //Kamat: Default format
        else if (!strcmp(new_str_picture_format, "yuv420sp_custom"))
            new_picture_format = V4L2_PIX_FMT_NV12T;
        else if (!strcmp(new_str_picture_format, "yuv420p"))
            new_picture_format = V4L2_PIX_FMT_YUV420;
        else if (!strcmp(new_str_picture_format, "yuv422i"))
            new_picture_format = V4L2_PIX_FMT_YUYV;
        else if (!strcmp(new_str_picture_format, "uyv422i_custom")) //Zero copy UYVY format
            new_picture_format = V4L2_PIX_FMT_UYVY;
        else if (!strcmp(new_str_picture_format, "uyv422i")) //Non-zero copy UYVY format
            new_picture_format = V4L2_PIX_FMT_UYVY;
        else if (!strcmp(new_str_picture_format, CameraParameters::PIXEL_FORMAT_JPEG))
#ifdef JPEG_FROM_SENSOR
            new_picture_format = V4L2_PIX_FMT_YUYV;
#endif
        else if (!strcmp(new_str_picture_format, "yuv422p"))
            new_picture_format = V4L2_PIX_FMT_YUV422P;
        else
            new_picture_format = V4L2_PIX_FMT_NV21; //for 3rd party

        if (mSecCamera->setSnapshotPixelFormat(new_picture_format) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setSnapshotPixelFormat(format(%d))", __func__, new_picture_format);
            ret = UNKNOWN_ERROR;
        }
    }

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    //JPEG image quality
    int new_jpeg_quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);

    if (new_jpeg_quality < 1 || new_jpeg_quality > 100) {
        LOGE("ERR(%s): Invalid quality(%d))", __func__, new_jpeg_quality);

        new_jpeg_quality = 100;

        mParameters.set(CameraParameters::KEY_JPEG_QUALITY, "100");
    }

    mSecCamera->setJpegQuality(new_jpeg_quality);
#else
    //JPEG image quality
    int new_jpeg_quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);

    if (new_jpeg_quality < 0) {
    LOGW("JPEG-image quality is not specified or is negative, defaulting to 100");
        new_jpeg_quality = 100;
        mParameters.set(CameraParameters::KEY_JPEG_QUALITY, "100");
    }
    mSecCamera->setJpegQuality(new_jpeg_quality);
#endif

    // JPEG thumbnail size
    int new_jpeg_thumbnail_width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int new_jpeg_thumbnail_height= params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);

    if (0 < new_jpeg_thumbnail_width && 0 < new_jpeg_thumbnail_height) {
        if (mSecCamera->setJpegThumbnailSize(new_jpeg_thumbnail_width, new_jpeg_thumbnail_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setJpegThumbnailSize(width(%d), height(%d))", __func__, new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
            ret = UNKNOWN_ERROR;
        }
    }

    // frame rate
    int new_frame_rate = params.getPreviewFrameRate();

    if (new_frame_rate < 5 || new_frame_rate > 30) {
        new_frame_rate = 30;
    }

    mParameters.setPreviewFrameRate(new_frame_rate);
    // Calculate how long to wait between frames.
    mPreviewFrameRateMicrosec = (int)(1000000.0f / float(new_frame_rate));

    LOGD("frame rate:%d, mPreviewFrameRateMicrosec:%d", new_frame_rate, mPreviewFrameRateMicrosec);

    mSecCamera->setFrameRate(new_frame_rate);

    //vt mode
    int new_vtmode = mInternalParameters.getInt("vtmode");

    if (0 <= new_vtmode) {
        if (mSecCamera->setVTmode(new_vtmode) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setVTMode(%d)", __func__, new_vtmode);
            ret = UNKNOWN_ERROR;
        }
    }

    // rotation
    int new_rotation = params.getInt(CameraParameters::KEY_ROTATION);
    int new_exif_rotation = 1;

    if (0 <= new_rotation) {
        LOGV("%s : set orientation:%d\n", __func__, new_rotation);
        if (mSecCamera->setExifOrientationInfo(new_rotation) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setExifOrientationInfo(%d)", __func__, new_rotation);
            ret = UNKNOWN_ERROR;
        }
    }

    // scene mode
    const char *new_scene_mode_str = params.get(CameraParameters::KEY_SCENE_MODE);

    LOGV("%s : new_scene_mode_str %s", __func__,new_scene_mode_str);

    if (new_scene_mode_str != NULL) {
        int  new_scene_mode = -1;

        const char *new_iso_str = NULL;
        const char *new_metering_str = NULL;
        int new_exposure_compensation = 0;
        const char *new_white_str = NULL;
        int new_sharpness = 0;
        int new_saturation = 0;
        const char *new_focus_mode_str = NULL;
        const char *new_flash_mode_str = NULL;

        if (!strcmp(new_scene_mode_str, CameraParameters::SCENE_MODE_AUTO)) {
            new_scene_mode = SecCamera::SCENE_MODE_NONE;

        } else {
            // defaults for the scene modes
            mInternalParameters.set("iso", "auto");
            mInternalParameters.set("metering", "center");
            mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
                            SecCamera::BRIGHTNESS_NORMAL);
            mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
                            CameraParameters::WHITE_BALANCE_AUTO);
            mInternalParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
            mInternalParameters.set("saturation", SecCamera::SATURATION_NORMAL);
            if (new_camera_id == SecCamera::CAMERA_ID_BACK) {
                mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                                CameraParameters::FOCUS_MODE_AUTO);
            }
            mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
                            "7500,30000");

            if (!strcmp(new_scene_mode_str,
                       CameraParameters::SCENE_MODE_PORTRAIT)) {
                new_scene_mode = SecCamera::SCENE_MODE_PORTRAIT;

                mInternalParameters.set("sharpness",
                                        SecCamera::SHARPNESS_MINUS_1);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_LANDSCAPE)) {
                new_scene_mode = SecCamera::SCENE_MODE_LANDSCAPE;

                mInternalParameters.set("metering", "matrix");
                mInternalParameters.set("sharpness", SecCamera::SHARPNESS_PLUS_1);
                mInternalParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_SPORTS)) {
                new_scene_mode = SecCamera::SCENE_MODE_SPORTS;

                mInternalParameters.set("iso", "sports");
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_PARTY)) {
                new_scene_mode = SecCamera::SCENE_MODE_PARTY_INDOOR;

                mInternalParameters.set("iso", "200");
                mInternalParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
            } else if ((!strcmp(new_scene_mode_str,
                                CameraParameters::SCENE_MODE_BEACH)) ||
                        (!strcmp(new_scene_mode_str,
                                 CameraParameters::SCENE_MODE_SNOW))) {
                new_scene_mode = SecCamera::SCENE_MODE_BEACH_SNOW;

                mInternalParameters.set("iso", "50");
                mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
                                SecCamera::BRIGHTNESS_PLUS_2);
                mInternalParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_SUNSET)) {
                new_scene_mode = SecCamera::SCENE_MODE_SUNSET;

                mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
                                CameraParameters::WHITE_BALANCE_DAYLIGHT);
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_NIGHT)) {
                new_scene_mode = SecCamera::SCENE_MODE_NIGHTSHOT;
                mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
                                "3780,30000");

                mInternalParameters.set("iso", "night");
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_FIREWORKS)) {
                new_scene_mode = SecCamera::SCENE_MODE_FIREWORKS;

                mInternalParameters.set("iso", "50");
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else if (!strcmp(new_scene_mode_str,
                               CameraParameters::SCENE_MODE_CANDLELIGHT)) {
                new_scene_mode = SecCamera::SCENE_MODE_CANDLE_LIGHT;

                mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
                                CameraParameters::WHITE_BALANCE_DAYLIGHT);
                mParameters.set(CameraParameters::KEY_FLASH_MODE,
                                CameraParameters::FLASH_MODE_OFF);
            } else {
                LOGE("%s::unmatched scene_mode(%s)",
                        __func__, new_scene_mode_str); //action, night-portrait, theatre, steadyphoto
                ret = UNKNOWN_ERROR;
            }
        }
        new_iso_str = mInternalParameters.get("iso");
        new_metering_str = mInternalParameters.get("metering");
        new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
        new_white_str = params.get(CameraParameters::KEY_WHITE_BALANCE);
        new_sharpness = mInternalParameters.getInt("sharpness");
        new_saturation = mInternalParameters.getInt("saturation");
        if (new_camera_id == SecCamera::CAMERA_ID_BACK) {
            new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);
        }
        new_flash_mode_str = params.get(CameraParameters::KEY_FLASH_MODE);

        // 1. ISO
        if (new_iso_str != NULL) {
            int  new_iso = -1;
            if (!strcmp(new_iso_str, "auto"))
                new_iso = SecCamera::ISO_AUTO;
            else if (!strcmp(new_iso_str, "50"))
                new_iso = SecCamera::ISO_50;
            else if (!strcmp(new_iso_str, "100"))
                new_iso = SecCamera::ISO_100;
            else if (!strcmp(new_iso_str, "200"))
                new_iso = SecCamera::ISO_200;
            else if (!strcmp(new_iso_str, "400"))
                new_iso = SecCamera::ISO_400;
            else if (!strcmp(new_iso_str, "800"))
                new_iso = SecCamera::ISO_800;
            else if (!strcmp(new_iso_str, "1600"))
                new_iso = SecCamera::ISO_1600;
            else if (!strcmp(new_iso_str, "sports"))
                new_iso = SecCamera::ISO_SPORTS;
            else if (!strcmp(new_iso_str, "night"))
                new_iso = SecCamera::ISO_NIGHT;
            else if (!strcmp(new_iso_str, "movie"))
                new_iso = SecCamera::ISO_MOVIE;
            else {
                LOGE("%s::unmatched iso(%d)", __func__, new_iso);
                ret = UNKNOWN_ERROR;
            }

           if (0 <= new_iso) {
                if (mSecCamera->setISO(new_iso) < 0) {
                    LOGE("%s::mSecCamera->setISO(%d) fail", __func__, new_iso);
                    ret = UNKNOWN_ERROR;
                }
            }
        }

        // 2. metering
        if (new_metering_str != NULL) {
            int  new_metering = -1;
            if (!strcmp(new_metering_str, "matrix"))
                new_metering = SecCamera::METERING_MATRIX;
            else if (!strcmp(new_metering_str, "center"))
                new_metering = SecCamera::METERING_CENTER;
            else if (!strcmp(new_metering_str, "spot"))
                new_metering = SecCamera::METERING_SPOT;
            else {
                LOGE("%s::unmatched metering(%s)", __func__, new_metering_str);
                ret = UNKNOWN_ERROR;
            }
            if (0 <= new_metering) {
                if (mSecCamera->setMetering(new_metering) < 0) {
                    LOGE("%s::mSecCamera->setMetering(%d) fail", __func__, new_metering);
                    ret = UNKNOWN_ERROR;
                }
            }
        }

        // 3. brightness
        int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
        int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);

        if ((min_exposure_compensation <= new_exposure_compensation) &&
            (max_exposure_compensation >= new_exposure_compensation)) {
            if (mSecCamera->setBrightness(new_exposure_compensation) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setBrightness(brightness(%d))", __func__, new_exposure_compensation);
                ret = UNKNOWN_ERROR;
            }
        }

        // 4. whitebalance
        if (new_white_str != NULL) {
            int new_white = -1;

            if (!strcmp(new_white_str, CameraParameters::WHITE_BALANCE_AUTO))
                new_white = SecCamera::WHITE_BALANCE_AUTO;
            else if (!strcmp(new_white_str,
                             CameraParameters::WHITE_BALANCE_DAYLIGHT))
                new_white = SecCamera::WHITE_BALANCE_DAYLIGHT;
            else if (!strcmp(new_white_str,
                             CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
                new_white = SecCamera::WHITE_BALANCE_CLOUDY;
            else if (!strcmp(new_white_str,
                             CameraParameters::WHITE_BALANCE_FLUORESCENT))
                new_white = SecCamera::WHITE_BALANCE_FLUORESCENT;
            else if (!strcmp(new_white_str,
                             CameraParameters::WHITE_BALANCE_INCANDESCENT))
                new_white = SecCamera::WHITE_BALANCE_INCANDESCENT;
            else {
                LOGE("ERR(%s):Invalid white balance(%s)", __func__, new_white_str); //twilight, shade, warm_flourescent
                ret = UNKNOWN_ERROR;
            }

            if (0 <= new_white) {
                // white_balance
                if (mSecCamera->setWhiteBalance(new_white) < 0) {
                    LOGE("ERR(%s):Fail on mSecCamera->setWhiteBalance(white(%d))", __func__, new_white);
                    ret = UNKNOWN_ERROR;
                }
            }
        }

        //5. sharpness
        if (0 <= new_sharpness) {
            if (mSecCamera->setSharpness(new_sharpness) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setSharpness(%d)", __func__,new_sharpness);
                ret = UNKNOWN_ERROR;
            }
        }

        //6. saturation
        if (0 <= new_saturation) {
            if (mSecCamera->setSaturation(new_saturation) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setSaturation(%d)", __func__, new_saturation);
                ret = UNKNOWN_ERROR;
            }
        }

        // 7. focus mode
        if (new_focus_mode_str != NULL) {
            int  new_focus_mode = -1;

            if (!strcmp(new_focus_mode_str,
                        CameraParameters::FOCUS_MODE_AUTO)) {
                new_focus_mode = SecCamera::FOCUS_MODE_AUTO; 
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
            }
            else if (!strcmp(new_focus_mode_str,
                             CameraParameters::FOCUS_MODE_MACRO)) {
                new_focus_mode = SecCamera::FOCUS_MODE_MACRO;
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR);
            }
            else if (!strcmp(new_focus_mode_str,
                             CameraParameters::FOCUS_MODE_INFINITY)) {
                new_focus_mode = SecCamera::FOCUS_MODE_INFINITY;
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR);
            }
            else {
                LOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str);
                ret = UNKNOWN_ERROR;
            }

            if (0 <= new_focus_mode) {
                if (mSecCamera->setFocusMode(new_focus_mode) < 0) {
                    LOGE("%s::mSecCamera->setFocusMode(%d) fail", __func__, new_focus_mode);
                    ret = UNKNOWN_ERROR;
                }
            }
        }

        //  8. flash..
        if (new_flash_mode_str != NULL) {
            int  new_flash_mode = -1;

            if (!strcmp(new_flash_mode_str, CameraParameters::FLASH_MODE_OFF))
                new_flash_mode = SecCamera::FLASH_MODE_OFF;
            else if (!strcmp(new_flash_mode_str, CameraParameters::FLASH_MODE_AUTO))
                new_flash_mode = SecCamera::FLASH_MODE_AUTO;
            else if (!strcmp(new_flash_mode_str, CameraParameters::FLASH_MODE_ON))
                new_flash_mode = SecCamera::FLASH_MODE_ON;
            else {
                LOGE("%s::unmatched flash_mode(%s)", __func__, new_flash_mode_str); //red-eye
                ret = UNKNOWN_ERROR;
            }
            if (0 <= new_flash_mode) {
                if (mSecCamera->setFlashMode(new_flash_mode) < 0) {
                    LOGE("%s::mSecCamera->setFlashMode(%d) fail", __func__, new_flash_mode);
                    ret = UNKNOWN_ERROR;
                }
            }
        }

        //  9. scene..
        if (0 <= new_scene_mode) {
            if (mSecCamera->setSceneMode(new_scene_mode) < 0) {
                LOGE("%s::mSecCamera->setSceneMode(%d) fail", __func__, new_scene_mode);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    // ---------------------------------------------------------------------------

    // image effect
    const char *new_image_effect_str = params.get(CameraParameters::KEY_EFFECT);
    if (new_image_effect_str != NULL) {

        int  new_image_effect = -1;

        if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NONE))
            new_image_effect = SecCamera::IMAGE_EFFECT_NONE;
        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_MONO))
            new_image_effect = SecCamera::IMAGE_EFFECT_BNW;
        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_SEPIA))
            new_image_effect = SecCamera::IMAGE_EFFECT_SEPIA;
        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_AQUA))
            new_image_effect = SecCamera::IMAGE_EFFECT_AQUA;
        else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NEGATIVE))
            new_image_effect = SecCamera::IMAGE_EFFECT_NEGATIVE;
        else {
            //posterize, whiteboard, blackboard, solarize
            LOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str);
            ret = UNKNOWN_ERROR;
        }

        if (new_image_effect >= 0) {
            if (mSecCamera->setImageEffect(new_image_effect) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setImageEffect(effect(%d))", __func__, new_image_effect);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    //antiBanding
    const char *new_antibanding_str = params.get(CameraParameters::KEY_ANTIBANDING);

    if (new_antibanding_str != NULL) {
        int new_antibanding = -1;

        if (!strcmp(new_antibanding_str, CameraParameters::ANTIBANDING_AUTO))
            new_antibanding = SecCamera::ANTI_BANDING_AUTO;
        else if (!strcmp(new_antibanding_str, CameraParameters::ANTIBANDING_50HZ))
            new_antibanding = SecCamera::ANTI_BANDING_50HZ;
        else if (!strcmp(new_antibanding_str, CameraParameters::ANTIBANDING_60HZ))
            new_antibanding = SecCamera::ANTI_BANDING_60HZ;
        else if (!strcmp(new_antibanding_str, CameraParameters::ANTIBANDING_OFF))
            new_antibanding = SecCamera::ANTI_BANDING_OFF;
        else {
            LOGE("%s::unmatched antibanding(%s)", __func__, new_antibanding_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_antibanding) {
            if (mSecCamera->setAntiBanding(new_antibanding) < 0) {
                LOGE("%s::mSecCamera->setAntiBanding(%d) fail", __func__, new_antibanding);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    //contrast
    int new_contrast = mInternalParameters.getInt("contrast");

    if (0 <= new_contrast) {
        if (mSecCamera->setContrast(new_contrast) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setContrast(%d)", __func__, new_contrast);
            ret = UNKNOWN_ERROR;
        }
    }

    //WDR
    int new_wdr = mInternalParameters.getInt("wdr");

    if (0 <= new_wdr) {
        if (mSecCamera->setWDR(new_wdr) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_wdr);
            ret = UNKNOWN_ERROR;
        }
    }

    //anti shake
    int new_anti_shake = mInternalParameters.getInt("anti-shake");

    if (0 <= new_anti_shake) {
        if (mSecCamera->setAntiShake(new_anti_shake) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_anti_shake);
            ret = UNKNOWN_ERROR;
        }
    }

    // gps latitude
    const char *new_gps_latitude_str = params.get(CameraParameters::KEY_GPS_LATITUDE);

    if (mSecCamera->setGPSLatitude(new_gps_latitude_str) < 0) {
        LOGE("%s::mSecCamera->setGPSLatitude(%s) fail", __func__, new_gps_latitude_str);
        ret = UNKNOWN_ERROR;
    }

    // gps longitude
    const char *new_gps_longitute_str = params.get(CameraParameters::KEY_GPS_LONGITUDE);

    if (mSecCamera->setGPSLongitude(new_gps_longitute_str) < 0) {
        LOGE("%s::mSecCamera->setGPSLongitude(%s) fail", __func__, new_gps_longitute_str);
        ret = UNKNOWN_ERROR;
    }

    // gps altitude
    const char *new_gps_altitude_str = params.get(CameraParameters::KEY_GPS_ALTITUDE);

    if (mSecCamera->setGPSAltitude(new_gps_altitude_str) < 0) {
        LOGE("%s::mSecCamera->setGPSAltitude(%s) fail", __func__, new_gps_altitude_str);
        ret = UNKNOWN_ERROR;
    }

    // gps timestamp
    const char *new_gps_timestamp_str = params.get(CameraParameters::KEY_GPS_TIMESTAMP);

    if (mSecCamera->setGPSTimeStamp(new_gps_timestamp_str) < 0) {
        LOGE("%s::mSecCamera->setGPSTimeStamp(%s) fail", __func__, new_gps_timestamp_str);
        ret = UNKNOWN_ERROR;
    }

    // Recording size
    int new_recording_width = params.getInt("recording-size-width");
    int new_recording_height= params.getInt("recording-size-height");

    if (0 < new_recording_width && 0 < new_recording_height) {
        if (mSecCamera->setRecordingSize(new_recording_width, new_recording_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_recording_width, new_recording_height);
            ret = UNKNOWN_ERROR;
        }
    } else {
        if (mSecCamera->setRecordingSize(new_preview_width, new_preview_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_preview_width, new_preview_height);
            ret = UNKNOWN_ERROR;
        }
    }

    //gamma
    const char *new_gamma_str = mInternalParameters.get("video_recording_gamma");

    if (new_gamma_str != NULL) {
        int new_gamma = -1;
        if (!strcmp(new_gamma_str, "off"))
            new_gamma = SecCamera::GAMMA_OFF;
        else if (!strcmp(new_gamma_str, "on"))
            new_gamma = SecCamera::GAMMA_ON;
        else {
            LOGE("%s::unmatched gamma(%s)", __func__, new_gamma_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_gamma) {
            if (mSecCamera->setGamma(new_gamma) < 0) {
                LOGE("%s::mSecCamera->setGamma(%d) fail", __func__, new_gamma);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    //slow ae
    const char *new_slow_ae_str = mInternalParameters.get("slow_ae");

    if (new_slow_ae_str != NULL) {
        int new_slow_ae = -1;

        if (!strcmp(new_slow_ae_str, "off"))
            new_slow_ae = SecCamera::SLOW_AE_OFF;
        else if (!strcmp(new_slow_ae_str, "on"))
            new_slow_ae = SecCamera::SLOW_AE_ON;
        else {
            LOGE("%s::unmatched slow_ae(%s)", __func__, new_slow_ae_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_slow_ae) {
            if (mSecCamera->setSlowAE(new_slow_ae) < 0) {
                LOGE("%s::mSecCamera->setSlowAE(%d) fail", __func__, new_slow_ae);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    /*Camcorder fix fps*/
    int new_sensor_mode = mInternalParameters.getInt("cam_mode");

    if (0 <= new_sensor_mode) {
        if (mSecCamera->setSensorMode(new_sensor_mode) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setSensorMode(%d)", __func__, new_sensor_mode);
            ret = UNKNOWN_ERROR;
        }
    } else {
        new_sensor_mode=0;
    }

    /*Shot mode*/
    int new_shot_mode = mInternalParameters.getInt("shot_mode");

    if (0 <= new_shot_mode) {
        if (mSecCamera->setShotMode(new_shot_mode) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setShotMode(%d)", __func__, new_shot_mode);
            ret = UNKNOWN_ERROR;
        }
    } else {
        new_shot_mode=0;
    }

    //blur for Video call
    int new_blur_level = mInternalParameters.getInt("blur");

    if (0 <= new_blur_level) {
        if (mSecCamera->setBlur(new_blur_level) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setBlur(%d)", __func__, new_blur_level);
            ret = UNKNOWN_ERROR;
        }
    }


    // chk_dataline
    int new_dataline = mInternalParameters.getInt("chk_dataline");

    if (0 <= new_dataline) {
        if (mSecCamera->setDataLineCheck(new_dataline) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setDataLineCheck(%d)", __func__, new_dataline);
            ret = UNKNOWN_ERROR;
        }
    }

    return ret;
}

CameraParameters CameraHardwareSec::getParameters() const
{
    LOGV("%s :", __func__);
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t CameraHardwareSec::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardwareSec::release()
{
    LOGV("%s :", __func__);
}

wp<CameraHardwareInterface> CameraHardwareSec::singleton;

sp<CameraHardwareInterface> CameraHardwareSec::createInstance(int cameraId)
{
    LOGV("%s :", __func__);
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardwareSec(cameraId));
    singleton = hardware;
    return hardware;
}

static CameraInfo sCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        90,  /* orientation */
    },
    {
        CAMERA_FACING_FRONT,
        90,  /* orientation */
    }
};

extern "C" int HAL_getNumberOfCameras()
{
    return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo *cameraInfo)
{
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    return CameraHardwareSec::createInstance(cameraId);
}

}; // namespace android
