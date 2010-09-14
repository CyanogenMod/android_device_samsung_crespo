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
#define CACHEABLE_BUFFERS 0x1
#define ALL_BUFFERS_FLUSHED -66
int buf_idx = 0;
#endif

#ifdef SEND_YUV_RECORD_DATA
#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)
#define RECORD_HEAP_SIZE (ALIGN_TO_8KB(ALIGN_TO_128B(1280) * ALIGN_TO_32B(720)) + ALIGN_TO_8KB(ALIGN_TO_128B(1280) * ALIGN_TO_32B(720/2)))
#endif

namespace android {

struct ADDRS
{
	unsigned int addr_y;
	unsigned int addr_cbcr;
	unsigned int buf_index;
	unsigned int reserved;
};

struct ADDRS_CAP
{
	unsigned int addr_y;
	unsigned int width;
	unsigned int height;
};

CameraHardwareSec::CameraHardwareSec()
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
		  mNotifyCb(0),
		  mDataCb(0),
		  mDataCbTimestamp(0),
		  mCallbackCookie(0),
		  mMsgEnabled(0),
		  mCurrentPreviewFrame(0),
#if defined(BOARD_USES_OVERLAY)
		  mUseOverlay(false),
#endif
		  mRecordRunning(false)
#ifdef JPEG_FROM_SENSOR
		  ,
		  mPostViewWidth(0),
		  mPostViewHeight(0),
		  mPostViewSize(0)
#endif                    
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
		  ,
		  mObjectTrackingStatus(0),
		  mSmartautosceneRunning(false),
		  mSmartautoscene_current_status(0),
		  mSmartautoscene_previous_status(0)
#endif
{
	LOGV("%s()", __func__);
	int ret = 0;
	mNoHwHandle = 0;

	mSecCamera = SecCamera::createInstance();
	if(mSecCamera == NULL)
	{
		LOGE("ERR(%s):Fail on mSecCamera object creation", __func__);
	}

	ret = mSecCamera->initCamera();
	if(ret < 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera init", __func__);
	}

	if(mSecCamera->flagCreate() == 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera->flagCreate()", __func__);
	}

#ifndef PREVIEW_USING_MMAP
	int previewHeapSize = sizeof(struct ADDRS) * kBufferCount;
	LOGV("mPreviewHeap : MemoryHeapBase(previewHeapSize(%d))", previewHeapSize);
	mPreviewHeap = new MemoryHeapBase(previewHeapSize);
	if (mPreviewHeap->getHeapID() < 0)
        {
        	LOGE("ERR(%s): Preview heap creation fail", __func__);
                mPreviewHeap.clear();
        }
#endif

#ifdef SEND_YUV_RECORD_DATA
	int recordHeapSize = RECORD_HEAP_SIZE;	
#else
	int recordHeapSize = sizeof(struct ADDRS) * kBufferCount;
#endif
	LOGV("mRecordHeap : MemoryHeapBase(recordHeapSize(%d))", recordHeapSize);
	mRecordHeap = new MemoryHeapBase(recordHeapSize);
	if (mRecordHeap->getHeapID() < 0)
        {
        	LOGE("ERR(%s): Record heap creation fail", __func__);
                mRecordHeap.clear();
        }

#ifdef JPEG_FROM_SENSOR 
	mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
	LOGV("mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",mPostViewWidth,mPostViewHeight,mPostViewSize);
#endif
	
#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
	int rawHeapSize = mPostViewSize;
#else
	int rawHeapSize = sizeof(struct ADDRS_CAP);
#endif
	LOGV("mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
	mRawHeap = new MemoryHeapBase(rawHeapSize);
	if (mRawHeap->getHeapID() < 0)
        {
        	LOGE("ERR(%s): Raw heap creation fail", __func__);
                mRawHeap.clear();
        }

	initDefaultParameters();
}

void CameraHardwareSec::initDefaultParameters()
{
        if(mSecCamera == NULL)
        {
                LOGE("ERR(%s):mSecCamera object is NULL", __func__);
		return;
        }

	CameraParameters p;

	int preview_max_width	= 0;
	int preview_max_height	= 0;
	int snapshot_max_width	= 0;
	int snapshot_max_height = 0;

	int camera_id = 1;

	p.set("camera-id", camera_id);

	if(camera_id == 1)
		mSecCamera->setCameraId(SecCamera::CAMERA_ID_BACK);
	else
		mSecCamera->setCameraId(SecCamera::CAMERA_ID_FRONT);

	if(mSecCamera->getPreviewMaxSize(&preview_max_width, &preview_max_height) < 0)
	{
		LOGE("getPreviewMaxSize fail (%d / %d) \n", preview_max_width, preview_max_height);
		preview_max_width  = LCD_WIDTH;
		preview_max_height = LCD_HEIGHT;
	}
	if(mSecCamera->getSnapshotMaxSize(&snapshot_max_width, &snapshot_max_height) < 0)
	{
		LOGE("getSnapshotMaxSize fail (%d / %d) \n", snapshot_max_width, snapshot_max_height);
		snapshot_max_width	= LCD_WIDTH;
		snapshot_max_height	= LCD_HEIGHT;
	}

#ifdef PREVIEW_USING_MMAP
	p.setPreviewFormat("yuv420sp");
#else
	p.setPreviewFormat("yuv420sp_custom");
#endif
	p.setPreviewSize(preview_max_width, preview_max_height);
	p.setPreviewFrameRate(30);
	
	p.setPictureFormat("jpeg");
	p.setPictureSize(snapshot_max_width, snapshot_max_height);
	p.set("jpeg-quality", "100"); // maximum quality
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	p.set("preview-size-values","640x480,800x480"); 	//s1_camera [ 3-party application 에서 get supported preview size 안되는 현상 수정 ]
	p.set("picture-size-values","2560x1920,2048x1536,1600x1200,640x480,2560x1536,2048x1232,1600x960,800x480");
    p.set("preview-format-values", "yuv420sp");
	p.set("preview-frame-rate-values", "15,30");
    p.set("picture-format-values", "jpeg");
	p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "160x120,0x0");
	p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set("focus-mode-values", "auto,macro");
	p.set("antibanding-values", "auto,50hz,60hz,off");
	p.set("effect-values", "none,mono,negative,sepia");
	p.set("flash-mode-values", "off");
//	p.set("focus-mode-values", "auto,infinity,macro");
	p.set("scene-mode-values", "auto,portrait,landscape,night,beach,snow,sunset,fireworks,sports,party,candlelight");
	p.set("whitebalance-values", "auto,incandescent,fluorescent,daylight,cloudy-daylight"); 
//add the max and min for adjust value[20100728 giung.jung]
	p.set("sharpness-min", 0);
	p.set("sharpness-max", 4);
	p.set("saturation-min", 0);
	p.set("saturation-max", 4); 
	p.set("contrast-min", 0);
	p.set("contrast-max", 4); 
	
#else
        // List supported picture size values //Kamat
        p.set("picture-size-values", "2560x1920,2048x1536,1600x1200,1280x960");
#endif
	// These values must be multiples of 16, so we can't do 427x320, which is the exact size on
	// screen we want to display at. 480x360 doesn't work either since it's a multiple of 8.
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

	p.set("rotation",	  0);
	p.set("whitebalance",  "auto");
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	p.set("effect", "none");
	p.set("scene-mode", "auto");
	p.set("vintagemode", "off");
	p.set("sharpness", 2);
	p.set("contrast", 2);
	p.set("saturation", 2);
	p.set("iso", "auto");
	p.set("metering", "center");
	//p.set("facedetect", 0);
	p.set("flash-mode", "off");
	p.set("focus-mode", "auto");	
	p.set("anti-shake", 0);	
	p.set("wdr", 0);
	p.set("smart-auto",0);
	p.set("beauty-shot", 0);
	p.set("antibanding", "auto");
	p.set("video_recording_gamma", "off");	
	p.set("slow_ae", "off");
	p.set("vtmode", 0);	
	p.set("chk_dataline", 0);		
	p.set("blur", 0);
#else
	p.set("image-effects", "original");
#endif

	p.set(CameraParameters::KEY_ZOOM, "0");
	p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
	p.set(CameraParameters::KEY_MAX_ZOOM, "12");
	p.set(CameraParameters::KEY_ZOOM_RATIOS, "100,125,150,175,200,225,250,275,300,324,350,375,400");

	p.set(CameraParameters::KEY_FOCAL_LENGTH, "3.79");

	p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
	p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

	p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
	p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
	p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
	p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");

	p.set("AppShutterSound", 0);
	if (setParameters(p) != NO_ERROR)
	{
		LOGE("ERR(%s):Fail on setParameters(p)", __func__);
	}
}

CameraHardwareSec::~CameraHardwareSec()
{
	LOGV("%s()", __func__);

	mSecCamera->DeinitCamera();

	if(mRawHeap != NULL)
		mRawHeap.clear();

	if(mJpegHeap != NULL)
		mJpegHeap.clear();

	if(mPreviewHeap != NULL)
		mPreviewHeap.clear();

	if(mRecordHeap != NULL)
		mRecordHeap.clear();

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
                                      void* user)
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
	if(index < 0)
	{
		LOGE("ERR(%s):Fail on SecCamera->getPreview()", __func__);
		return UNKNOWN_ERROR;
	}
	
	nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	
#ifdef PREVIEW_USING_MMAP
	int width, height, frame_size, offset;
	mSecCamera->getPreviewSize(&width, &height, &frame_size);
	
	offset = (frame_size+16)*index;
        sp<MemoryBase> buffer = new MemoryBase(mPreviewHeap, offset, frame_size);

	unsigned int phyYAddr = mSecCamera->getPhyAddrY(index);
	unsigned int phyCAddr = mSecCamera->getPhyAddrC(index);
	if(phyYAddr == 0xffffffff || phyCAddr == 0xffffffff)
	{
		LOGE("ERR(%s):Fail on SecCamera. Invalid PhyAddr, Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
		return UNKNOWN_ERROR;
	}
	memcpy(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset+frame_size	), &phyYAddr, 4);
	memcpy(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset+frame_size+4), &phyCAddr, 4);

#if defined(BOARD_USES_OVERLAY)
	if(mUseOverlay) {
		int ret;

		if ( buf_idx == 0 )
			buf_idx = 1;
		else
			buf_idx = 0;

		memcpy(static_cast<unsigned char*>(mPreviewHeap->base()) + (offset+frame_size) + sizeof(phyYAddr) + sizeof(phyCAddr), &buf_idx, sizeof(buf_idx));

		ret = mOverlay->queueBuffer((void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset+frame_size)));

		if (ret == ALL_BUFFERS_FLUSHED) {
			goto OverlayEnd;
		} else if (ret == -1) {
			LOGE("ERR(%s):overlay queueBuffer fail", __func__);
			goto OverlayEnd;
		}

		overlay_buffer_t overlay_buffer;
		ret = mOverlay->dequeueBuffer(&overlay_buffer);
		if (ret == -1) {
			LOGE("ERR(%s):overlay dequeueBuffer fail", __func__);
			goto OverlayEnd;
		}

	}

OverlayEnd:
#endif

#else
	unsigned int phyYAddr = mSecCamera->getPhyAddrY(index);
	unsigned int phyCAddr = mSecCamera->getPhyAddrC(index);
	if(phyYAddr == 0xffffffff || phyCAddr == 0xffffffff)
	{
		LOGE("ERR(%s):Fail on SecCamera getPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
		return UNKNOWN_ERROR;
	}
	struct ADDRS *addrs = (struct ADDRS *)mPreviewHeap->base();

	sp<MemoryBase> buffer = new MemoryBase(mPreviewHeap, index * sizeof(struct ADDRS), sizeof(struct ADDRS));
	addrs[index].addr_y = phyYAddr;
	addrs[index].addr_cbcr = phyCAddr;
#endif //PREVIEW_USING_MMAP

        // Notify the client of a new frame. //Kamat --eclair
        if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)
	{
        	  mDataCb(CAMERA_MSG_PREVIEW_FRAME, buffer, mCallbackCookie);
	}
//	LOG_CAMERA_PREVIEW("previewThread: addr_y(0x%08X) addr_cbcr(0x%08X)", addrs[index].addr_y, addrs[index].addr_cbcr);

	if(mRecordRunning == true)
	{
#ifdef SEND_YUV_RECORD_DATA
		int width, height, frame_size;
		unsigned char* virYAddr;
		unsigned char* virCAddr;
		mSecCamera->getPreviewSize(&width, &height, &frame_size);
		mSecCamera->getYUVBuffers(&virYAddr, &virCAddr, index);
		sp<MemoryBase> buffer = new MemoryBase(mRecordHeap, 0, frame_size);
		//memcpy(mRecordHeap->base(), (void*)virYAddr, width*height);
		//memcpy(mRecordHeap->base()+(width*height),(void*)virCAddr, width*height*0.5);
		memcpy(mRecordHeap->base(), (void*)virYAddr, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
		memcpy(mRecordHeap->base() + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)),(void*)virCAddr, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height/2)));
#else
#ifdef DUAL_PORT_RECORDING
		int index = mSecCamera->getRecord();
		if(index < 0)
		{
			LOGE("ERR(%s):Fail on SecCamera->getRecord()", __func__);
			return UNKNOWN_ERROR;
		}
		unsigned int phyYAddr = mSecCamera->getRecPhyAddrY(index);
		unsigned int phyCAddr = mSecCamera->getRecPhyAddrC(index);
		if(phyYAddr == 0xffffffff || phyCAddr == 0xffffffff)
		{	
			LOGE("ERR(%s):Fail on SecCamera getRectPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
			return UNKNOWN_ERROR;
		}
#endif//DUAL_PORT_RECORDING
		struct ADDRS *addrs = (struct ADDRS *)mRecordHeap->base();

		sp<MemoryBase> buffer = new MemoryBase(mRecordHeap, index * sizeof(struct ADDRS), sizeof(struct ADDRS));
		addrs[index].addr_y = phyYAddr;
		addrs[index].addr_cbcr = phyCAddr;
#endif
        	// Notify the client of a new frame. //Kamat --eclair
	        if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)
		{
			//nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
			mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, buffer, mCallbackCookie);
		}
	}
#ifdef DUAL_PORT_RECORDING
	else if(mRecordRunning == false)
	{
        	if(mSecCamera->stopRecord() < 0)
        	{
			LOGE("ERR(%s):Fail on mSecCamera->stopRecord()", __func__);
     		        return UNKNOWN_ERROR;
        	}
	}
#endif

//	buffer.clear();

	// Wait for it...
	if(mTimeStart.tv_sec == 0 && mTimeStart.tv_usec == 0) 
	{
		gettimeofday(&mTimeStart, NULL);
	} else 
	{
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
	int ret = 0;		//s1 [Apply factory standard]

	LOGE("%s()", __func__);
	
	Mutex::Autolock lock(mLock);
	if (mPreviewThread != 0) {
		// already running
		return INVALID_OPERATION;
	}

	memset(&mTimeStart, 0, sizeof(mTimeStart));
	memset(&mTimeStop, 0, sizeof(mTimeStop));

	mSecCamera->stopPreview();

#if 1	//s1 [Apply factory standard]
	ret  = mSecCamera->startPreview();
	LOGE("%s : return startPreview %d", __func__, ret);
	
	if(ret < 0)
#else
	if(mSecCamera->startPreview() < 0)
#endif
	{
		LOGE("ERR(%s):Fail on mSecCamera->startPreview()", __func__);
		if (mMsgEnabled & CAMERA_MSG_ERROR)
		{
	        	mNotifyCb(CAMERA_MSG_ERROR, -2, 0, mCallbackCookie);
		}
		return -1; //UNKNOWN_ERROR;
	}

#ifdef PREVIEW_USING_MMAP
	if(mPreviewHeap != NULL)
        	mPreviewHeap.clear();
	int width, height, frame_size;
	mSecCamera->getPreviewSize(&width, &height, &frame_size);
	int previewHeapSize = (frame_size+16) * kBufferCount;
        LOGD("MemoryHeapBase(fd(%d), size(%d), width(%d), height(%d))", (int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), width, height);
	mPreviewHeap = new MemoryHeapBase((int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), (uint32_t)0);
#endif

#ifdef JPEG_FROM_SENSOR 
		mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
		LOGE("CameraHardwareSec: mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",mPostViewWidth,mPostViewHeight,mPostViewSize);
#endif
			
#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
		int rawHeapSize = mPostViewSize;
#else
		int rawHeapSize = sizeof(struct ADDRS_CAP);
#endif
		LOGE("CameraHardwareSec: mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
		mRawHeap = new MemoryHeapBase(rawHeapSize);
		if (mRawHeap->getHeapID() < 0)
		{
			LOGE("ERR(%s): Raw heap creation fail", __func__);
				mRawHeap.clear();
		}

#if 0
		mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
		struct ADDRS_CAP *addrs = (struct ADDRS_CAP *)mRawHeap->base();

		LOGE("[5B 1] mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",mPostViewWidth,mPostViewHeight);

		addrs[0].width = mPostViewWidth;
		addrs[0].height = mPostViewHeight;
			
#endif 

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
	LOGV("%s() : ", __func__);

	int overlayWidth  = 0;
	int overlayHeight = 0;
	int overlayFrameSize = 0;

	if(overlay == NULL) {
		goto setOverlayFail;
	}

	if(overlay->getHandleRef()== NULL && mUseOverlay == true) {
        if(mOverlay != 0)
		mOverlay->destroy();
		mOverlay = NULL;
		mUseOverlay = false;

		return NO_ERROR;
	}

	if(overlay->getStatus() != NO_ERROR)
	{
		LOGE("ERR(%s):overlay->getStatus() fail", __func__);
		goto setOverlayFail;
	}

	mSecCamera->getPreviewSize(&overlayWidth, &overlayHeight, &overlayFrameSize);

	if(overlay->setCrop(0, 0, overlayWidth, overlayHeight) != NO_ERROR)
	{
		LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __func__, overlayWidth, overlayHeight);
		goto setOverlayFail;
	}

	mOverlay = overlay;
	mUseOverlay = true;

	return NO_ERROR;

setOverlayFail :

	if(mOverlay != 0)
		mOverlay->destroy();
	mOverlay = 0;

	mUseOverlay = false;

	return UNKNOWN_ERROR;

}
#endif

void CameraHardwareSec::stopPreview()
{
	LOGV("%s()", __func__);
	
	sp<PreviewThread> previewThread;

	{ // scope for the lock
		Mutex::Autolock lock(mLock);
		previewThread = mPreviewThread;
	}

	// don't hold the lock while waiting for the thread to quit
//	if (previewThread != 0) {
//		previewThread->requestExitAndWait();
//	}

	Mutex::Autolock lock(mLock);
	mPreviewThread.clear();

	if(!mNoHwHandle)
	if(mSecCamera->stopPreview() < 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera->stopPreview()", __func__);
	}

	mPreviewRunning = false;

#if defined(BOARD_USES_OVERLAY)
	if(mUseOverlay) {
		mOverlay->destroy();
		mUseOverlay = false;
		mOverlay = NULL;
	}
#endif

}

bool CameraHardwareSec::previewEnabled() {
	LOGV("%s() : %d", __func__, mPreviewThread != 0);
	return mPreviewThread != 0;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSec::startRecording()
{
	LOGV("%s()", __func__);

#ifdef DUAL_PORT_RECORDING
        if(mSecCamera->startRecord() < 0)
        {
                LOGE("ERR(%s):Fail on mSecCamera->startRecord()", __func__);
                return UNKNOWN_ERROR;
        }
#endif

	mRecordRunning = true;
	return NO_ERROR;
}

void CameraHardwareSec::stopRecording()
{
	LOGV("%s()", __func__);

	mRecordRunning = false;
}

bool CameraHardwareSec::recordingEnabled()
{
	LOGV("%s()", __func__);

	return mRecordRunning;
}

void CameraHardwareSec::releaseRecordingFrame(const sp<IMemory>& mem)
{
	LOG_CAMERA_PREVIEW("%s()", __func__);

//    ssize_t offset; size_t size;
//    sp<MemoryBase>	   mem1	= mem;
//    sp<MemoryHeapBase> heap = mem->getMemory(&offset, &size);
//    sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);

//    mem1.clear();
//    heap.clear();
}

// ---------------------------------------------------------------------------

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
int CameraHardwareSec::smartautosceneThread()
{

	mSmartautoscene_current_status = mSecCamera->getAutosceneStatus();
	
	if(mSmartautoscene_current_status < 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera->getAutosceneStatus()", __func__);
		return UNKNOWN_ERROR;
	}

	if(mSmartautoscene_current_status != mSmartautoscene_previous_status)
	{
		//if (mMsgEnabled & CAMERA_MSG_SMARTAUTO_SCENE_STATUS)
			//mNotifyCb(CAMERA_MSG_SMARTAUTO_SCENE_STATUS, mSmartautoscene_current_status, 0, mCallbackCookie);
		LOGE("%s   CAMERA_MSG_SMARTAUTO_SCENE_STATUS(%d) Callback!!!!!!!!    ", __func__,mSmartautoscene_current_status);
		mSmartautoscene_previous_status = mSmartautoscene_current_status;
	}
	else
	{
		LOGE("%s   current_status(%d) is same with previous_status(%d)", __func__,mSmartautoscene_current_status,mSmartautoscene_previous_status);
	}
	usleep(2000*1000);  //2000ms delay 
	LOGE("DELAY(2000ms)!!!!!!!");
	return NO_ERROR;
}

status_t CameraHardwareSec::startSmartautoscene()
{
	LOGV("%s()", __func__);
	
//	Mutex::Autolock lock(mLock);

	if (mSmartautosceneThread != 0) {
		// already running
		return INVALID_OPERATION;
	}

	mSmartautosceneRunning = true;
	mSmartautosceneThread = new SmartautosceneThread(this);
	return NO_ERROR;
}

void CameraHardwareSec::stopSmartautoscene()
{
	LOGV("%s()", __func__);
	
	sp<SmartautosceneThread> smartautosceneThread;

	{ // scope for the lock
//	Mutex::Autolock lock(mLock);
		smartautosceneThread = mSmartautosceneThread;
	}

	// don't hold the lock while waiting for the thread to quit
	if (smartautosceneThread != 0) {
		smartautosceneThread->requestExitAndWait();
	}

//	Mutex::Autolock lock(mLock);
	mSmartautosceneThread.clear();

	mSmartautosceneRunning = false;
}

bool CameraHardwareSec::smartautosceneEnabled() {
	LOGV("%s() : %d", __func__, mSmartautosceneThread != 0);
	return mSmartautosceneThread != 0;
}

#endif


int CameraHardwareSec::beginAutoFocusThread(void *cookie)
{
	LOGV("%s()", __func__);
	CameraHardwareSec *c = (CameraHardwareSec *)cookie;
	return c->autoFocusThread();
}

int CameraHardwareSec::autoFocusThread()
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	int count =0;
	int af_status =0 ;
#endif	

	LOGV("%s()", __func__);
//	usleep(50000); // 1frame delay 50ms
	if(mSecCamera->setAutofocus() < 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera->setAutofocus()", __func__);
		return UNKNOWN_ERROR;
	}
	
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
//	usleep(10000);
		af_status = mSecCamera->getAutoFocusResult();
		
	if (af_status == 0x01)
	{
		LOGV("%s() AF Success!!", __func__);
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
	}
	else if (af_status == 0x02)
		{
		LOGV("%s() AF Cancelled !!", __func__);
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotifyCb(CAMERA_MSG_FOCUS, 0x02, 0, mCallbackCookie);
	}	
	else
	{
		LOGV("%s() AF Fail !!", __func__);
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
	LOGV("%s()", __func__);
	Mutex::Autolock lock(mLock);
	if (createThread(beginAutoFocusThread, this) == false)
		return UNKNOWN_ERROR;
	return NO_ERROR;
}

/* 2009.10.14 by icarus for added interface */
status_t CameraHardwareSec::cancelAutoFocus()
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGV("%s()", __func__);
	
	if(mSecCamera->cancelAutofocus() < 0)
	{
		LOGE("ERR(%s):Fail on mSecCamera->cancelAutofocus()", __func__);
		return UNKNOWN_ERROR;
	}
#endif
    return NO_ERROR;
}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
status_t CameraHardwareSec::objectTracking(int onoff)
{
	LOGV("%s() onoff = %d", __func__, onoff);
	
	Mutex::Autolock lock(mLock);
	if(onoff)
	{
		if (mObjectTrackingThread == 0) {
			mObjectTrackingThread = new ObjectTrackingThread(this);
		}
		mObjectTrackingRunning=true;
	}	
	else
	{
		if (mObjectTrackingThread != 0) {
			mObjectTrackingThread->requestExitAndWait();
		}
		mObjectTrackingThread.clear();
		mObjectTrackingRunning=false;
	}
	return 0;
}

int CameraHardwareSec::save_jpeg( unsigned char * real_jpeg, int jpeg_size)
{
        FILE *yuv_fp = NULL;
        char filename[100], *buffer = NULL;

        /* file create/open, note to "wb" */
        yuv_fp = fopen("/data/camera_dump.jpeg", "wb");
        if (yuv_fp==NULL)
	{
		LOGE("Save jpeg file open error");
		return -1;
	}

		LOGE("[BestIQ]  real_jpeg size ========>  %d\n", jpeg_size);
		buffer = (char *) malloc(jpeg_size);
	if(buffer == NULL)
	{
		LOGE("Save YUV] buffer alloc failed");
		if(yuv_fp) fclose(yuv_fp);
		return -1;
	}

        memcpy(buffer, real_jpeg, jpeg_size);

        fflush(stdout);

		fwrite(buffer, 1, jpeg_size, yuv_fp);

        fflush(yuv_fp);

	if(yuv_fp)
	        fclose(yuv_fp);
	if(buffer)
	        free(buffer);

        return 0;
}


int CameraHardwareSec::objectTrackingThread()
{
	int new_obj_status;
	new_obj_status = mSecCamera->getObjectTrackingStatus();
#if 0 //temp till define callback msg
	if (mObjectTrackingStatus != new_obj_status)
	{
		mObjectTrackingStatus = new_obj_status;
		if (mMsgEnabled & CAMERA_MSG_OBJ_TRACKING)
        		mNotifyCb(CAMERA_MSG_OBJ_TRACKING, new_obj_status, 0, mCallbackCookie);
	}
#endif
	usleep(100000); //100ms
	return NO_ERROR;
}
#endif
/*static*/ int CameraHardwareSec::beginPictureThread(void *cookie)
{
	LOGV("%s()", __func__);
	CameraHardwareSec *c = (CameraHardwareSec *)cookie;
	return c->pictureThread();
}

void CameraHardwareSec::save_postview(const char *fname, uint8_t *buf, uint32_t size)
{
	int nw, cnt = 0;
	uint32_t written = 0;

	LOGD("opening file [%s]\n", fname);
	int fd = open(fname, O_RDWR | O_CREAT);
	if (fd < 0) {
		LOGE("failed to create file [%s]: %s", fname, strerror(errno));
	return;
	}

	LOGD("writing %d bytes to file [%s]\n", size, fname);
	while (written < size) {
		nw = ::write(fd,
		buf + written,
		size - written);
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

int CameraHardwareSec::pictureThread()
{
	LOGV("%s()", __func__);

	int jpeg_size = 0;
	int ret = NO_ERROR;
	unsigned char * jpeg_data = NULL;
	int postview_offset = 0;	
	unsigned char * postview_data = NULL;

	//unsigned int addr;
	unsigned char * addr = NULL;
	int mPostViewWidth, mPostViewHeight, mPostViewSize;
	int cap_width, cap_height, cap_frame_size;
	
	mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
	int postviewHeapSize = mPostViewWidth*mPostViewHeight*2; //*size = (BACK_CAMERA_POSTVIEW_WIDTH * BACK_CAMERA_POSTVIEW_HEIGHT * BACK_CAMERA_POSTVIEW_BPP)/8; 
	mSecCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
	LOGE("[kidggang]:func(%s):line(%d)&cap_width(%d), &cap_height(%d), &cap_frame_size(%d)\n",__func__,__LINE__,cap_width, cap_height, cap_frame_size);
	
//	sp<MemoryBase> buffer = new MemoryBase(mRawHeap, 0, postviewHeapSize);	

	LOG_TIME_DEFINE(0)
	LOG_TIME_START(0)
#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
	sp<MemoryBase> buffer = new MemoryBase(mRawHeap, 0, mPostViewSize+8);
#else
	sp<MemoryBase> buffer = new MemoryBase(mRawHeap, 0, sizeof(struct ADDRS_CAP));
#endif

	struct ADDRS_CAP *addrs = (struct ADDRS_CAP *)mRawHeap->base();

#ifdef JPEG_FROM_SENSOR
	addrs[0].width = mPostViewWidth;
	addrs[0].height = mPostViewHeight;
	LOGV("[5B] mPostViewWidth = %d mPostViewHeight = %d\n",mPostViewWidth,mPostViewHeight);	
#else
	mParameters.getPictureSize((int*)&addrs[0].width, (int*)&addrs[0].height);
#endif


	if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) { //[zzangdol] CAMERA_ID_BACK
#ifndef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
		if (mMsgEnabled & CAMERA_MSG_SHUTTER)
		{
			mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
		}
#endif
	}//[zzangdol]CAMERA_ID_BACK

	if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE)
	{
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
		if (mMsgEnabled & CAMERA_MSG_SHUTTER)
		{
			mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
		}
#endif

		if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK){ //[zzangdol] CAMERA_ID_BACK
			jpeg_data = mSecCamera->getJpeg(&jpeg_size, &phyAddr);
			if(jpeg_data == NULL)
			{
				LOGE("ERR(%s):Fail on SecCamera->getSnapshot()", __func__);
				ret = UNKNOWN_ERROR;
			}
		}//[zzangdol] CAMERA_ID_BACK
		else
		{
			addr = mSecCamera->getSnapshotAndJpeg(); 
			//LOGV("[zzangdol] getSnapshotAndJpeg\n");
		}
#else
		phyAddr = mSecCamera->getSnapshotAndJpeg();
		jpeg_data = mSecCamera->yuv2Jpeg((unsigned char*)phyAddr, 0, &jpeg_size, picture_width, picture_height, picture_format);
#endif
		
#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA		
		postview_offset = mSecCamera->getPostViewOffset();
		if(jpeg_data != NULL)
			memcpy(mRawHeap->base(), jpeg_data+postview_offset, mPostViewSize);
#else
		addrs[0].addr_y = phyAddr;
#endif
		
		LOG_TIME_END(1)
		LOG_CAMERA("getSnapshotAndJpeg interval: %lu us", LOG_TIME(1)); 	
	}

    int JpegImageSize, JpegExifSize;
    sp<MemoryHeapBase> PostviewHeap = new MemoryHeapBase(mPostViewSize);
    sp<MemoryHeapBase> JpegHeap = new MemoryHeapBase(4300000);
	decodeInterleaveData(jpeg_data, 4261248, mPostViewWidth, mPostViewHeight,
                         &JpegImageSize, JpegHeap->base(), PostviewHeap->base());

	sp<MemoryBase> postview = new MemoryBase(PostviewHeap, 0, postviewHeapSize);
	memcpy(mRawHeap->base(),PostviewHeap->base(), postviewHeapSize);
#if 0//def SWP1_CAMERA_ADD_ADVANCED_FUNCTION
 	if (mMsgEnabled & CAMERA_MSG_POSTVIEW_FRAME)
	{		
		int postviewHeapSize = mPostViewSize;
		sp<MemoryHeapBase> mPostviewHeap = new MemoryHeapBase(postviewHeapSize);

		postview_data = jpeg_data + postview_offset;
		sp<MemoryBase> postview = new MemoryBase(mPostviewHeap, 0, postviewHeapSize);

		if(postview_data != NULL)
			memcpy(mPostviewHeap->base(), postview_data, postviewHeapSize);

		mDataCb(CAMERA_MSG_POSTVIEW_FRAME, postview, mCallbackCookie);
	}
#endif //#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION

	if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)
	{
		if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK){
			const int EXIF_FILE_SIZE = 28800;
			const int JPG_STREAM_BUF_SIZE = 3145728;

            sp<MemoryHeapBase> ExifHeap = new MemoryHeapBase(EXIF_FILE_SIZE + JPG_STREAM_BUF_SIZE);
            JpegExifSize = mSecCamera->getExif((unsigned char *)ExifHeap->base(), 
                                               (unsigned char *)PostviewHeap->base());

            LOGE("JpegExifSize=%d", JpegExifSize);
            unsigned char *ExifStart = (unsigned char *)JpegHeap->base() + 2;
            unsigned char *ImageStart = ExifStart + JpegExifSize;

            memmove(ImageStart, ExifStart, JpegImageSize - 2);
            memcpy(ExifStart, ExifHeap->base(), JpegExifSize);
            sp<MemoryBase> mem = new MemoryBase(JpegHeap, 0, JpegImageSize + JpegExifSize);

            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
			
			mDataCb(CAMERA_MSG_RAW_IMAGE, buffer, mCallbackCookie);
		}//[zzangdol] CAMERA_ID_BACK
		else
		{
			LOGV("[zzangdol] COMPRESSED_IMAGE\n");
			//mSecCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
			//sp<MemoryHeapBase> mHeap = new MemoryHeapBase((int)mSecCamera->getCameraFd(), (size_t)(cap_frame_size * kBufferCount), (uint32_t)0);
			sp<MemoryHeapBase> mHeap = new MemoryHeapBase(2000000);
			memcpy(mHeap->base(), addr, cap_frame_size);
			sp<MemoryBase> mem = new MemoryBase(mHeap , 0, cap_frame_size);
			//save_postview("/data/front.yuv", (uint8_t *)mHeap->base(), yuv_frame_size2);
			mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);//[zzangdol]
		}
	}

	LOG_TIME_END(0)
	LOG_CAMERA("pictureThread interval: %lu us", LOG_TIME(0));

	return ret;
}

status_t CameraHardwareSec::takePicture()
{
	LOGV("%s()", __func__);

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

	LOGE("decodeInterleaveData Start~~~");	
	while(i < interleaveDataSize) {
		if ((*interleave_ptr == 0xFFFFFFFF) || 
            (*interleave_ptr == 0x02FFFFFF) || 
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

	} 
        else {
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
			for (i=0; i<3; i++) {
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
	LOGE("decodeInterleaveData End~~~");	
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
	LOGV("%s()", __func__);
	
	Mutex::Autolock lock(mLock);
	
	status_t ret = NO_ERROR;
	
	mParameters = params;

	// set camera id
	int new_camera_id = params.getInt("camera-id");
	if(0 <= new_camera_id)
	{	
		if(mSecCamera->setCameraId(new_camera_id) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setCameraId(camera_id(%d))", __func__, new_camera_id);
			ret = UNKNOWN_ERROR;
		}
	}

	// preview size
	int new_preview_width  = 0;
	int new_preview_height = 0; 
	params.getPreviewSize(&new_preview_width, &new_preview_height);
	const char * new_str_preview_format = params.getPreviewFormat();
	if(0 < new_preview_width && 0 < new_preview_height && new_str_preview_format != NULL)
	{
		int new_preview_format = 0;
		
		if (strcmp(new_str_preview_format, "rgb565") == 0)
			new_preview_format = V4L2_PIX_FMT_RGB565;
		else if (strcmp(new_str_preview_format, "yuv420sp") == 0)
			new_preview_format = V4L2_PIX_FMT_NV21; //Kamat
		else if (strcmp(new_str_preview_format, "yuv420sp_custom") == 0)
			new_preview_format = V4L2_PIX_FMT_NV12T; //Kamat
		else if (strcmp(new_str_preview_format, "yuv420p") == 0)
			new_preview_format = V4L2_PIX_FMT_YUV420;
		else if (strcmp(new_str_preview_format, "yuv422i") == 0)
			new_preview_format = V4L2_PIX_FMT_YUYV;
		else if (strcmp(new_str_preview_format, "yuv422p") == 0)
			new_preview_format = V4L2_PIX_FMT_YUV422P;
		else
			new_preview_format = V4L2_PIX_FMT_NV21; //for 3rd party

		if(mSecCamera->setPreviewSize(new_preview_width, new_preview_height, new_preview_format) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setPreviewSize(width(%d), height(%d), format(%d))", __func__, new_preview_width, new_preview_height, new_preview_format);
			ret = UNKNOWN_ERROR;
		}
#if defined(BOARD_USES_OVERLAY)
		if(mUseOverlay == true && mOverlay != 0)
		{
			if(mOverlay->setCrop(0, 0, new_preview_width, new_preview_height) != NO_ERROR) 	{
				LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __func__, new_preview_width, new_preview_height);
			}
		}
#endif
	}

	int new_picture_width  = 0;
	int new_picture_height = 0;
	params.getPictureSize(&new_picture_width, &new_picture_height);
	if(0 < new_picture_width && 0 < new_picture_height)
	{
		if(mSecCamera->setSnapshotSize(new_picture_width, new_picture_height) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setSnapshotSize(width(%d), height(%d))", __func__, new_picture_width, new_picture_height);
			ret = UNKNOWN_ERROR;
		}
	}
	
	// picture format
	const char * new_str_picture_format = params.getPictureFormat();
	if(new_str_picture_format != NULL)
	{
		int new_picture_format = 0;

		if (strcmp(new_str_picture_format, "rgb565") == 0)
			new_picture_format = V4L2_PIX_FMT_RGB565;
		else if (strcmp(new_str_picture_format, "yuv420sp") == 0)
			new_picture_format = V4L2_PIX_FMT_NV21; //Kamat: Default format
		else if (strcmp(new_str_picture_format, "yuv420sp_custom") == 0)
			new_picture_format = V4L2_PIX_FMT_NV12T;
		else if (strcmp(new_str_picture_format, "yuv420p") == 0)
			new_picture_format = V4L2_PIX_FMT_YUV420;
		else if (strcmp(new_str_picture_format, "yuv422i") == 0)
			new_picture_format = V4L2_PIX_FMT_YUYV;
		else if (strcmp(new_str_picture_format, "uyv422i_custom") == 0) //Zero copy UYVY format
			new_picture_format = V4L2_PIX_FMT_UYVY;		
		else if (strcmp(new_str_picture_format, "uyv422i") == 0) //Non-zero copy UYVY format
			new_picture_format = V4L2_PIX_FMT_UYVY;
		else if (strcmp(new_str_picture_format, "jpeg") == 0)
#ifdef JPEG_FROM_SENSOR
			new_picture_format = V4L2_PIX_FMT_UYVY;
#else
			new_picture_format = V4L2_PIX_FMT_YUYV;
#endif
		else if (strcmp(new_str_picture_format, "yuv422p") == 0)
			new_picture_format = V4L2_PIX_FMT_YUV422P;
		else
			new_picture_format = V4L2_PIX_FMT_NV21; //for 3rd party

		if(mSecCamera->setSnapshotPixelFormat(new_picture_format) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setSnapshotPixelFormat(format(%d))", __func__, new_picture_format);
			ret = UNKNOWN_ERROR;
		}
	}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	//JPEG image quality
	int new_jpeg_quality = params.getInt("jpeg-quality");

	if (new_jpeg_quality < 1 || new_jpeg_quality > 100) {
		LOGE("ERR(%s): Invalid quality(%d))", __func__, new_jpeg_quality);

		new_jpeg_quality = 100;

		mParameters.set("jpeg-quality", "100");
	}

	mSecCamera->setJpegQuality(new_jpeg_quality);
#else
	//JPEG image quality
	int new_jpeg_quality = params.getInt("jpeg-quality");
        if (new_jpeg_quality < 0)
	{
            LOGW("JPEG-image quality is not specified or is negative, defaulting to 100");
            new_jpeg_quality = 100;
            mParameters.set("jpeg-quality", "100");
        }
	mSecCamera->setJpegQuality(new_jpeg_quality);
#endif

	// JPEG thumbnail size
	int new_jpeg_thumbnail_width = params.getInt("jpeg-thumbnail-width");
	int new_jpeg_thumbnail_height= params.getInt("jpeg-thumbnail-height");

	if(0 < new_jpeg_thumbnail_width && 0 < new_jpeg_thumbnail_height)
	{
		if(mSecCamera->setJpegThumbnailSize(new_jpeg_thumbnail_width, new_jpeg_thumbnail_height) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setJpegThumbnailSize(width(%d), height(%d))", __func__, new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
			ret = UNKNOWN_ERROR;
		}
	}

	// frame rate		
	int new_frame_rate = params.getPreviewFrameRate();
	if(new_frame_rate < 5 || new_frame_rate > 30)
	{
		new_frame_rate = 30;
	}

	mParameters.setPreviewFrameRate(new_frame_rate);
	// Calculate how long to wait between frames.
	mPreviewFrameRateMicrosec = (int)(1000000.0f / float(new_frame_rate));
	
	LOGD("frame rate:%d, mPreviewFrameRateMicrosec:%d", new_frame_rate, mPreviewFrameRateMicrosec);

	mSecCamera->setFrameRate(new_frame_rate);

	//vt mode
	int new_vtmode = params.getInt("vtmode");
	if(0 <= new_vtmode)
	{
		if(mSecCamera->setVTmode(new_vtmode) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setVTMode(%d)", __func__, new_vtmode);
			ret = UNKNOWN_ERROR;
		}	
	}

	// rotation
	int new_rotation = params.getInt("rotation");
	int new_exif_rotation = 1;
	if(new_rotation != -1)
	{
		if(new_vtmode == SecCamera::VT_MODE_ON ) // vt preview rotation
		{
			LOGE("ERR(%s):VT mode is on. Rotate(%d))", __func__, new_rotation);

			if(mSecCamera->SetRotate(new_rotation) < 0)
			{
				LOGE("ERR(%s):Fail on mSecCamera->SetRotate(rotation(%d))", __func__, new_rotation);
				ret = UNKNOWN_ERROR;
			}
		}
		else //exif orientation information
		{
			if(mSecCamera->SetRotate(0) < 0)
			{
				LOGE("ERR(%s):Fail on mSecCamera->SetRotate(rotation(%d))", __func__, new_rotation);
				ret = UNKNOWN_ERROR;
			}

			if(new_rotation == 0)
				new_exif_rotation = 1;
			else if (new_rotation == 90)
				new_exif_rotation = 6;
			else if (new_rotation == 180)
				new_exif_rotation = 3;
			else if (new_rotation == 270)
				new_exif_rotation = 8;
			else
				new_exif_rotation = 1;
		}
	}
	
#ifndef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	// brightness
	int new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
	int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
	int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);

	if(	(min_exposure_compensation <= new_exposure_compensation) &&
		(max_exposure_compensation >= new_exposure_compensation)	)
	{
		if(mSecCamera->setBrightness(new_exposure_compensation) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setBrightness(brightness(%d))", __func__, new_exposure_compensation);
			ret = UNKNOWN_ERROR;
		}
	
	}

	// whitebalance
	const char * new_white_str = params.get("whitebalance");
	if(new_white_str != NULL)
	{
		int new_white = -1;
	
		if(strcmp(new_white_str, "auto") == 0)
			new_white = SecCamera::WHITE_BALANCE_AUTO;
		else if(strcmp(new_white_str, "indoor3100") == 0)
			new_white = SecCamera::WHITE_BALANCE_INDOOR3100;
		else if(strcmp(new_white_str, "outdoor5100") == 0)
			new_white = SecCamera::WHITE_BALANCE_OUTDOOR5100;
		else if(strcmp(new_white_str, "indoor2000") == 0)
			new_white = SecCamera::WHITE_BALANCE_INDOOR2000;
		else if(strcmp(new_white_str, "halt") == 0)
			new_white = SecCamera::WHITE_BALANCE_HALT;
		else if(strcmp(new_white_str, "cloudy") == 0)
			new_white = SecCamera::WHITE_BALANCE_CLOUDY;
		else if(strcmp(new_white_str, "sunny") == 0)
			new_white = SecCamera::WHITE_BALANCE_SUNNY;
		else
		{
			LOGE("ERR(%s):Invalid white balance(%s)", __func__, new_white_str);
			ret = UNKNOWN_ERROR;
		}
	
		if(0 <= new_white)
		{
			// white_balance
			if(mSecCamera->setWhiteBalance(new_white) < 0)
			{
				LOGE("ERR(%s):Fail on mSecCamera->setWhiteBalance(white(%d))", __func__, new_white);
				ret = UNKNOWN_ERROR;
			}
		}
	}

	// image effect
	const char * new_image_effect_str = params.get("image-effects");
	if(new_image_effect_str != NULL)
	{
		int  new_image_effect = -1;
	
		if(strcmp(new_image_effect_str, "original") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_ORIGINAL;
		else if(strcmp(new_image_effect_str, "arbitrary") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_ARBITRARY;
		else if(strcmp(new_image_effect_str, "negative") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_NEGATIVE;
		else if(strcmp(new_image_effect_str, "freeze") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_FREEZE;
		else if(strcmp(new_image_effect_str, "embossing") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_EMBOSSING;
		else if(strcmp(new_image_effect_str, "silhouette") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_SILHOUETTE;
		else
		{
			LOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str);
			ret = UNKNOWN_ERROR;
		}
		
		if(new_image_effect >= 0)
		{
			// white_balance
			if(mSecCamera->setImageEffect(new_image_effect) < 0)
			{
				LOGE("ERR(%s):Fail on mSecCamera->setImageEffect(effect(%d))", __func__, new_image_effect);
				ret = UNKNOWN_ERROR;
			}
		}
	}

#else
	// scene mode 
	const char * new_scene_mode_str = params.get("scene-mode");

	LOGV("%s()  new_scene_mode_str %s", __func__,new_scene_mode_str);

	if(new_scene_mode_str != NULL)
	{
		int  new_scene_mode = -1;

		const char * new_iso_str = NULL;
		const char * new_metering_str = NULL;
		int new_exposure_compensation = 0;
		const char * new_white_str = NULL;
		int new_sharpness = 0;
		int new_saturation = 0;
		const char * new_focus_mode_str = NULL;
		const char * new_flash_mode_str = NULL;		

		if(strcmp(new_scene_mode_str, "auto") == 0)
		{
			new_scene_mode = SecCamera::SCENE_MODE_NONE;	
			
			new_iso_str = params.get("iso");
			new_metering_str = params.get("metering");
			new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
			new_white_str = params.get("whitebalance");
			new_sharpness = params.getInt("sharpness");
			new_saturation = params.getInt("saturation");
			new_focus_mode_str = params.get("focus-mode");
			new_flash_mode_str = params.get("flash-mode");
		}
		else
		{
			if(strcmp(new_scene_mode_str, "portrait") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_PORTRAIT;
				
				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_MINUS_1);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "facedetect");
			}
			else if(strcmp(new_scene_mode_str, "landscape") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_LANDSCAPE;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "matrix");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_PLUS_1);
				mParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "sports") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_SPORTS;

				mParameters.set("iso", "sports");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "party") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_PARTY_INDOOR;

				mParameters.set("iso", "200");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
				mParameters.set("focus-mode", "auto");
			}
			else if((strcmp(new_scene_mode_str, "beach") == 0) || (strcmp(new_scene_mode_str, "snow") == 0))
			{
				new_scene_mode = SecCamera::SCENE_MODE_BEACH_SNOW;

				mParameters.set("iso", "50");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_PLUS_2);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_PLUS_1);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "sunset") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_SUNSET;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "daylight");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "dusk-dawn") == 0) //added
			{
				new_scene_mode = SecCamera::SCENE_MODE_DUSK_DAWN;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "fluorescent");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "fall-color") == 0) //added
			{
				new_scene_mode = SecCamera::SCENE_MODE_FALL_COLOR;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_PLUS_2);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "night") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_NIGHTSHOT;

				mParameters.set("iso", "night");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "back-light") == 0) //added
			{
				const char * flash_mode_str = params.get("flash-mode");

				new_scene_mode = SecCamera::SCENE_MODE_BACK_LIGHT;

				mParameters.set("iso", "auto");
				if(strcmp(flash_mode_str, "off") == 0)
					mParameters.set("metering", "spot");
				else
					mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
			}		
			else if(strcmp(new_scene_mode_str, "fireworks") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_FIREWORKS;

				mParameters.set("iso", "50");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else if(strcmp(new_scene_mode_str, "text") == 0) //added
			{
				new_scene_mode = SecCamera::SCENE_MODE_TEXT;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "auto");
				mParameters.set("sharpness", SecCamera::SHARPNESS_PLUS_2);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "macro");
			}
			else if(strcmp(new_scene_mode_str, "candlelight") == 0)
			{
				new_scene_mode = SecCamera::SCENE_MODE_CANDLE_LIGHT;

				mParameters.set("iso", "auto");
				mParameters.set("metering", "center");
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, SecCamera::BRIGHTNESS_NORMAL);
				mParameters.set("whitebalance", "daylight");
				mParameters.set("sharpness", SecCamera::SHARPNESS_NORMAL);
				mParameters.set("saturation", SecCamera::SATURATION_NORMAL);
				mParameters.set("focus-mode", "auto");
				mParameters.set("flash-mode", "off");
			}
			else
			{
				LOGE("%s::unmatched scene_mode(%s)", __func__, new_scene_mode_str); //action, night-portrait, theatre, steadyphoto
				ret = UNKNOWN_ERROR;
			}

			new_iso_str = mParameters.get("iso");
			new_metering_str = mParameters.get("metering");
			new_exposure_compensation = mParameters.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
			new_white_str = mParameters.get("whitebalance");
			new_sharpness = mParameters.getInt("sharpness");
			new_saturation = mParameters.getInt("saturation");
			new_focus_mode_str = mParameters.get("focus-mode");
			new_flash_mode_str = mParameters.get("flash-mode");
		}
			
		// 1. ISO
		if(new_iso_str != NULL )
		{
			int  new_iso = -1;
			if(strcmp(new_iso_str, "auto") == 0)
				new_iso = SecCamera::ISO_AUTO;
			else if(strcmp(new_iso_str, "50") == 0)
				new_iso = SecCamera::ISO_50;
			else if(strcmp(new_iso_str, "100") == 0)
				new_iso = SecCamera::ISO_100;
			else if(strcmp(new_iso_str, "200") == 0)
				new_iso = SecCamera::ISO_200;
			else if(strcmp(new_iso_str, "400") == 0)
				new_iso = SecCamera::ISO_400;
			else if(strcmp(new_iso_str, "800") == 0)
				new_iso = SecCamera::ISO_800;
			else if(strcmp(new_iso_str, "1600") == 0)
				new_iso = SecCamera::ISO_1600;
			else if(strcmp(new_iso_str, "sports") == 0)
				new_iso = SecCamera::ISO_SPORTS;
			else if(strcmp(new_iso_str, "night") == 0)
				new_iso = SecCamera::ISO_NIGHT;
				else if(strcmp(new_iso_str, "movie") == 0)
					new_iso = SecCamera::ISO_MOVIE;				
				else
				{
					LOGE("%s::unmatched iso(%d)", __func__, new_iso);
					ret = UNKNOWN_ERROR;
				}
				if(0 <= new_iso)
				{
					if(mSecCamera->setISO(new_iso) < 0)
					{
							LOGE("%s::mSecCamera->setISO(%d) fail", __func__, new_iso);
						ret = UNKNOWN_ERROR;
					}	
				}
			}

		// 2. metering
		if(new_metering_str != NULL )
		{
			int  new_metering = -1;
			if(strcmp(new_metering_str, "matrix") == 0)
				new_metering = SecCamera::METERING_MATRIX;
			else if(strcmp(new_metering_str, "center") == 0)
				new_metering = SecCamera::METERING_CENTER;
			else if(strcmp(new_metering_str, "spot") == 0)
				new_metering = SecCamera::METERING_SPOT;
			else
			{
				LOGE("%s::unmatched metering(%s)", __func__, new_metering_str);
				ret = UNKNOWN_ERROR;
			}
			if(0 <= new_metering)
			{
				if(mSecCamera->setMetering(new_metering) < 0)
				{
					LOGE("%s::mSecCamera->setMetering(%d) fail", __func__, new_metering);
					ret = UNKNOWN_ERROR;
				}
			}				
		}

			// 3. brightness
			int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
			int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);

			if(	(min_exposure_compensation <= new_exposure_compensation) &&
				(max_exposure_compensation >= new_exposure_compensation)	)
			{
				if(mSecCamera->setBrightness(new_exposure_compensation) < 0)
				{
					LOGE("ERR(%s):Fail on mSecCamera->setBrightness(brightness(%d))", __func__, new_exposure_compensation);
					ret = UNKNOWN_ERROR;
				}

			}

			// 4. whitebalance
			if(new_white_str != NULL)
			{
				int new_white = -1;

				if(strcmp(new_white_str, "auto") == 0)
					new_white = SecCamera::WHITE_BALANCE_AUTO;
				else if(strcmp(new_white_str, "daylight") == 0)
					new_white = SecCamera::WHITE_BALANCE_DAYLIGHT;
				else if((strcmp(new_white_str, "cloudy") == 0) || (strcmp(new_white_str, "cloudy-daylight") == 0))
					new_white = SecCamera::WHITE_BALANCE_CLOUDY;
				else if(strcmp(new_white_str, "fluorescent") == 0)
					new_white = SecCamera::WHITE_BALANCE_FLUORESCENT;
				else if(strcmp(new_white_str, "incandescent") == 0)
					new_white = SecCamera::WHITE_BALANCE_INCANDESCENT;
				else
				{
					LOGE("ERR(%s):Invalid white balance(%s)", __func__, new_white_str); //twilight, shade, warm_flourescent
					ret = UNKNOWN_ERROR;
				}
			
				if(0 <= new_white)
				{
					// white_balance
					if(mSecCamera->setWhiteBalance(new_white) < 0)
					{
						LOGE("ERR(%s):Fail on mSecCamera->setWhiteBalance(white(%d))", __func__, new_white);
						ret = UNKNOWN_ERROR;
					}
				}
			}

			//5. sharpness
			if(0 <= new_sharpness)
			{
				if(mSecCamera->setSharpness(new_sharpness) < 0)
				{
					LOGE("ERR(%s):Fail on mSecCamera->setSharpness(%d)", __func__,new_sharpness);
					ret = UNKNOWN_ERROR;
				}	
			}

			//6. saturation 
			if(0 <= new_saturation)
			{
				if(mSecCamera->setSaturation(new_saturation) < 0)
				{
					LOGE("ERR(%s):Fail on mSecCamera->setSaturation(%d)", __func__, new_saturation);
					ret = UNKNOWN_ERROR;
				}	
			}
				
			// 7. focus mode 
			if(new_focus_mode_str != NULL)
			{
				int  new_focus_mode = -1;

				if((strcmp(new_focus_mode_str, "auto") == 0) || (strcmp(new_focus_mode_str, "fixed") == 0) ||(strcmp(new_focus_mode_str, "infinity") == 0))
					new_focus_mode = SecCamera::FOCUS_MODE_AUTO;
				else if(strcmp(new_focus_mode_str, "macro") == 0)
					new_focus_mode = SecCamera::FOCUS_MODE_MACRO;
				else if(strcmp(new_focus_mode_str, "facedetect") == 0)
					new_focus_mode = SecCamera::FOCUS_MODE_FACEDETECT;
				else
				{
					LOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str); //infinity
					ret = UNKNOWN_ERROR;
				}
				
				if(0 <= new_focus_mode)
				{
					if(mSecCamera->setFocusMode(new_focus_mode) < 0)
					{
						LOGE("%s::mSecCamera->setFocusMode(%d) fail", __func__, new_focus_mode);
						ret = UNKNOWN_ERROR;
					}
				}
			}

		//  8. flash.. 
		if(new_flash_mode_str != NULL )
		{
			int  new_flash_mode = -1;
			if(strcmp(new_flash_mode_str, "off") == 0)
				new_flash_mode = SecCamera::FLASH_MODE_OFF;
			else if(strcmp(new_flash_mode_str, "auto") == 0)
				new_flash_mode = SecCamera::FLASH_MODE_AUTO;
			else if(strcmp(new_flash_mode_str, "on") == 0)
				new_flash_mode = SecCamera::FLASH_MODE_ON;
			else if(strcmp(new_flash_mode_str, "torch") == 0)
				new_flash_mode = SecCamera::FLASH_MODE_TORCH;
			else
			{
				LOGE("%s::unmatched flash_mode(%s)", __func__, new_flash_mode_str); //red-eye
				ret = UNKNOWN_ERROR;
			}
			if(0 <= new_flash_mode)
			{
				if(mSecCamera->setFlashMode(new_flash_mode) < 0)
				{
					LOGE("%s::mSecCamera->setFlashMode(%d) fail", __func__, new_flash_mode);
					ret = UNKNOWN_ERROR;
				}
			}				
		}

		//  9. scene.. 
		if(0 <= new_scene_mode)
		{
			if(mSecCamera->setSceneMode(new_scene_mode) < 0)
			{
				LOGE("%s::mSecCamera->setSceneMode(%d) fail", __func__, new_scene_mode);
				ret = UNKNOWN_ERROR;
			}
		}
	}

// ---------------------------------------------------------------------------	

	// image effect
	const char * new_image_effect_str = params.get("effect");
	if(new_image_effect_str != NULL)
	{
		int  new_image_effect = -1;
	
		if((strcmp(new_image_effect_str, "auto") == 0) || (strcmp(new_image_effect_str, "none") == 0))
			new_image_effect = SecCamera::IMAGE_EFFECT_NONE;
		else if((strcmp(new_image_effect_str, "bnw") == 0) || (strcmp(new_image_effect_str, "mono") == 0))
			new_image_effect = SecCamera::IMAGE_EFFECT_BNW;
		else if(strcmp(new_image_effect_str, "sepia") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_SEPIA;
		else if(strcmp(new_image_effect_str, "aqua") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_AQUA;
		else if(strcmp(new_image_effect_str, "antique") == 0) //added at samsung
			new_image_effect = SecCamera::IMAGE_EFFECT_ANTIQUE;
		else if(strcmp(new_image_effect_str, "negative") == 0)
			new_image_effect = SecCamera::IMAGE_EFFECT_NEGATIVE;
		else if(strcmp(new_image_effect_str, "sharpen") == 0) //added at samsung
			new_image_effect = SecCamera::IMAGE_EFFECT_SHARPEN;
		else
		{
			LOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str); //posterize, whiteboard, blackboard, solarize
			ret = UNKNOWN_ERROR;
		}
		
		if(new_image_effect >= 0)
		{
			if(mSecCamera->setImageEffect(new_image_effect) < 0)
			{
				LOGE("ERR(%s):Fail on mSecCamera->setImageEffect(effect(%d))", __func__, new_image_effect);
				ret = UNKNOWN_ERROR;
			}
		}
	}

	//antiBanding
	const char * new_antibanding_str = params.get("antibanding");
	if(new_antibanding_str != NULL)
	{
		int new_antibanding = -1;

		if(strcmp(new_antibanding_str,"auto") == 0)
			new_antibanding = SecCamera::ANTI_BANDING_AUTO;
		else if(strcmp(new_antibanding_str,"50hz") == 0)
			new_antibanding = SecCamera::ANTI_BANDING_50HZ;
		else if(strcmp(new_antibanding_str,"60hz") == 0)
			new_antibanding = SecCamera::ANTI_BANDING_60HZ;
		else if(strcmp(new_antibanding_str,"off") == 0)
			new_antibanding = SecCamera::ANTI_BANDING_OFF;
		else
		{
			LOGE("%s::unmatched antibanding(%s)", __func__, new_antibanding_str);
			ret = UNKNOWN_ERROR;
		}

		if(0 <= new_antibanding)
		{
			if(mSecCamera->setAntiBanding(new_antibanding) < 0)
			{
				LOGE("%s::mSecCamera->setAntiBanding(%d) fail", __func__, new_antibanding);
				ret = UNKNOWN_ERROR;
			}
		}	
			
	}
	
	//contrast 
	int new_contrast = params.getInt("contrast");
	if(0 <= new_contrast)
	{
		if(mSecCamera->setContrast(new_contrast) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setContrast(%d)", __func__, new_contrast);
			ret = UNKNOWN_ERROR;
		}	
	}	

	//WDR
	int new_wdr = params.getInt("wdr");
	if(0 <= new_wdr)
	{
		if(mSecCamera->setWDR(new_wdr) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_wdr);
			ret = UNKNOWN_ERROR;
		}	
	}

	//anti shake
	int new_anti_shake = params.getInt("anti-shake");
	if(0 <= new_anti_shake)
	{
		if(mSecCamera->setAntiShake(new_anti_shake) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_anti_shake);
			ret = UNKNOWN_ERROR;
		}	
	}

	//zoom
	int new_zoom_level = params.getInt(CameraParameters::KEY_ZOOM);
	if(0 <= new_zoom_level)
	{
		if(mSecCamera->setZoom(new_zoom_level) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setZoom(%d)", __func__, new_zoom_level);
			ret = UNKNOWN_ERROR;
		}	
	}

	//object tracking
	int new_obj_tracking = params.getInt("obj-tracking");
	if(0 <= new_obj_tracking)
	{
		if(mSecCamera->setObjectTracking(new_obj_tracking) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setObjectTracking(%d)", __func__, new_obj_tracking);
			ret = UNKNOWN_ERROR;
		}	
	}

	// smart auto
	int new_smart_auto = params.getInt("smart-auto");
	if(0 <= new_smart_auto)
	{
		if(mSecCamera->setSmartAuto(new_smart_auto) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setSmartAuto(%d)", __func__, new_smart_auto);
			ret = UNKNOWN_ERROR;
		}	

		//smart auto on	=> start Smartautoscene Thread
		if(mSecCamera->getSmartAuto() == 1)
		{
			if(startSmartautoscene() == INVALID_OPERATION)
			{
				LOGE("Smartautoscene thread is already running");
			}
			else
				LOGE("Smartautoscene thread start");
		}
		//smart auto off  => stop Smartautoscene Thread
		else	
		{
			if(mSmartautosceneRunning == true)
			{
				stopSmartautoscene();
				LOGV("Smartautoscene thread stop");
			}
			else
				LOGV("Smartautoscene thread was already stop");
		}
	}

	// beauty_shot
	int new_beauty_shot = params.getInt("face_beauty");
	if(0 <= new_beauty_shot)
	{
		if(mSecCamera->setBeautyShot(new_beauty_shot) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setBeautyShot(%d)", __func__, new_beauty_shot);
			ret = UNKNOWN_ERROR;
		}	
	}

	// vintage mode 
	const char * new_vintage_mode_str = params.get("vintagemode");
	if(new_vintage_mode_str != NULL)
	{
		int  new_vintage_mode = -1;

		if(strcmp(new_vintage_mode_str, "off") == 0)
			new_vintage_mode = SecCamera::VINTAGE_MODE_OFF;
		else if(strcmp(new_vintage_mode_str, "normal") == 0)
			new_vintage_mode = SecCamera::VINTAGE_MODE_NORMAL;
		else if(strcmp(new_vintage_mode_str, "warm") == 0)
			new_vintage_mode = SecCamera::VINTAGE_MODE_WARM;
		else if(strcmp(new_vintage_mode_str, "cool") == 0)
			new_vintage_mode = SecCamera::VINTAGE_MODE_COOL;
		else if(strcmp(new_vintage_mode_str, "bnw") == 0)
			new_vintage_mode = SecCamera::VINTAGE_MODE_BNW;
		else
		{
			LOGE("%s::unmatched vintage_mode(%d)", __func__, new_vintage_mode);
			ret = UNKNOWN_ERROR;
		}
		
		if(0 <= new_vintage_mode)
		{
			if(mSecCamera->setVintageMode(new_vintage_mode) < 0)
			{
				LOGE("%s::mSecCamera->setVintageMode(%d) fail", __func__, new_vintage_mode);
				ret = UNKNOWN_ERROR;
			}
		}
	}

	// gps latitude
	const char * new_gps_latitude_str = params.get("gps-latitude");
	if(mSecCamera->setGPSLatitude(new_gps_latitude_str) < 0)
	{
		LOGE("%s::mSecCamera->setGPSLatitude(%s) fail", __func__, new_gps_latitude_str);
		ret = UNKNOWN_ERROR;
	}

	// gps longitude
	const char * new_gps_longitute_str = params.get("gps-longitude");
	if(mSecCamera->setGPSLongitude(new_gps_longitute_str) < 0)
	{
		LOGE("%s::mSecCamera->setGPSLongitude(%s) fail", __func__, new_gps_longitute_str);
		ret = UNKNOWN_ERROR;
	}

	// gps altitude
	const char * new_gps_altitude_str = params.get("gps-altitude");
	if(mSecCamera->setGPSAltitude(new_gps_altitude_str) < 0)
	{
		LOGE("%s::mSecCamera->setGPSAltitude(%s) fail", __func__, new_gps_altitude_str);
		ret = UNKNOWN_ERROR;
	}

	// gps timestamp
	const char * new_gps_timestamp_str = params.get("gps-timestamp");
	if(mSecCamera->setGPSTimeStamp(new_gps_timestamp_str) < 0)
	{
		LOGE("%s::mSecCamera->setGPSTimeStamp(%s) fail", __func__, new_gps_timestamp_str);
		ret = UNKNOWN_ERROR;
	}

	// Recording size
	int new_recording_width = params.getInt("recording-size-width");
	int new_recording_height= params.getInt("recording-size-height");

	if(0 < new_recording_width && 0 < new_recording_height) {
		if(mSecCamera->setRecordingSize(new_recording_width, new_recording_height) < 0) {
			LOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_recording_width, new_recording_height);
			ret = UNKNOWN_ERROR;
		}
	}

	else {
		if(mSecCamera->setRecordingSize(new_preview_width, new_preview_height) < 0) {
			LOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_preview_width, new_preview_height);
			ret = UNKNOWN_ERROR;
		}
	}

	//gamma
	const char * new_gamma_str = params.get("video_recording_gamma");
	if(new_gamma_str != NULL)
	{
		int new_gamma = -1;
		
		if(strcmp(new_gamma_str, "off") == 0)
			new_gamma = SecCamera::GAMMA_OFF;
		else if(strcmp(new_gamma_str, "on") == 0)
			new_gamma = SecCamera::GAMMA_ON;
		else
		{
			LOGE("%s::unmatched gamma(%s)", __func__, new_gamma_str);
			ret = UNKNOWN_ERROR;
		}

		if(0 <= new_gamma)
		{
			if(mSecCamera->setGamma(new_gamma) < 0)
			{
				LOGE("%s::mSecCamera->setGamma(%d) fail", __func__, new_gamma);
				ret = UNKNOWN_ERROR;
			}
		}			
	}

	//slow ae
	const char * new_slow_ae_str = params.get("slow_ae");
	if(new_slow_ae_str != NULL)
	{
		int new_slow_ae = -1;
		
		if(strcmp(new_slow_ae_str, "off") == 0)
			new_slow_ae = SecCamera::SLOW_AE_OFF;
		else if(strcmp(new_slow_ae_str, "on") == 0)
			new_slow_ae = SecCamera::SLOW_AE_ON;
		else
		{
			LOGE("%s::unmatched slow_ae(%s)", __func__, new_slow_ae_str);
			ret = UNKNOWN_ERROR;
		}

		if(0 <= new_slow_ae)
		{
			if(mSecCamera->setSlowAE(new_slow_ae) < 0)
			{
				LOGE("%s::mSecCamera->setSlowAE(%d) fail", __func__, new_slow_ae);
				ret = UNKNOWN_ERROR;
			}
		}			
	}

	//exif orientation info
	int new_exif_orientation = params.getInt("exifOrientation"); //kidggang
	if(0 <= new_exif_orientation)
	{
		if(mSecCamera->setExifOrientationInfo(new_exif_orientation) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setExifOrientationInfo(%d)", __func__, new_exif_orientation);
			ret = UNKNOWN_ERROR;
		}	
	}

	/*Camcorder fix fps*/
	int new_sensor_mode = params.getInt("cam_mode");
	if(0 <= new_sensor_mode)
	{
		if(mSecCamera->setSensorMode(new_sensor_mode) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setSensorMode(%d)", __func__, new_sensor_mode);
			ret = UNKNOWN_ERROR;
		}
	}
	else
	{
		new_sensor_mode=0;
	}

	/*Shot mode*/
	int new_shot_mode = params.getInt("shot_mode");
	if(0 <= new_shot_mode)
	{
		if(mSecCamera->setShotMode(new_shot_mode) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setShotMode(%d)", __func__, new_shot_mode);
			ret = UNKNOWN_ERROR;
		}
	}
	else
	{
		new_shot_mode=0;
	}

	//blur for Video call
	int new_blur_level = params.getInt("blur");
	if(0 <= new_blur_level)
	{
		if(mSecCamera->setBlur(new_blur_level) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setBlur(%d)", __func__, new_blur_level);
			ret = UNKNOWN_ERROR;
		}	
	}	


    // chk_dataline
	int new_dataline = params.getInt("chk_dataline");
	if(0 <= new_dataline)
	{
		if(mSecCamera->setDataLineCheck(new_dataline) < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setDataLineCheck(%d)", __func__, new_dataline);
			ret = UNKNOWN_ERROR;
		}	
	}

	//Batch Command
	if(new_camera_id == SecCamera::CAMERA_ID_BACK)
	{
		if(mSecCamera->setBatchReflection() < 0)
		{
			LOGE("ERR(%s):Fail on mSecCamera->setBatchCmd", __func__);
			ret = UNKNOWN_ERROR;
		}	
	} 
#endif

	return ret;
}

CameraParameters CameraHardwareSec::getParameters() const
{
	LOGV("%s()", __func__);
	Mutex::Autolock lock(mLock);
	return mParameters;
}

status_t CameraHardwareSec::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGV("%s() : command = %d, arg1 =%d, arg2= %d", __func__,command, arg1, arg2);
	switch(command)
	{
		case COMMAND_AE_AWB_LOCK_UNLOCK:
			mSecCamera->setAEAWBLockUnlock(arg1, arg2);
			break;
		case COMMAND_FACE_DETECT_LOCK_UNLOCK:
			mSecCamera->setFaceDetectLockUnlock(arg1);
			break;
		case COMMAND_OBJECT_POSITION:
			mSecCamera->setObjectPosition(arg1, arg2);
			break;
		case COMMAND_OBJECT_TRACKING_STARTSTOP:
			mSecCamera->setObjectTrackingStartStop(arg1);
			objectTracking(arg1);
			break;
		case CONTINUOUS_SHOT_START_CAPTURE:
			break;
	  	case CONTINUOUS_SHOT_STOP_AND_ENCODING:
			mSecCamera->setAEAWBLockUnlock(arg1, arg2);
			LOGV("Continuous shot command received");
			break;
		case COMMAND_TOUCH_AF_STARTSTOP:
			mSecCamera->setTouchAFStartStop(arg1);
			break;
		case COMMAND_CHECK_DATALINE:
			mSecCamera->setDataLineCheckStop();
			break;						
		case COMMAND_DEFAULT_IMEI:
			mSecCamera->setDefultIMEI(arg1);
			break;				
		defualt:
			LOGV("%s()", __func__);
			break;
	}

	return NO_ERROR;
#else
    return BAD_VALUE;
#endif
}

void CameraHardwareSec::release()
{
	LOGV("%s()", __func__);
}

wp<CameraHardwareInterface> CameraHardwareSec::singleton;

sp<CameraHardwareInterface> CameraHardwareSec::createInstance()
{
	LOGV("%s()", __func__);
	if (singleton != 0) {
		sp<CameraHardwareInterface> hardware = singleton.promote();
		if (hardware != 0) {
			return hardware;
		}
	}
	sp<CameraHardwareInterface> hardware(new CameraHardwareSec());
	singleton = hardware;
	return hardware;
}
#if 0
extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
	LOGV("%s()", __func__);
	return CameraHardwareSec::createInstance();
}
#endif

static CameraInfo sCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        90,  /* orientation */
    }
};

extern "C" int HAL_getNumberOfCameras()
{
    return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    return CameraHardwareSec::createInstance();
}

}; // namespace android
