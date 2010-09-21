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

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H

#include "SecCamera.h"
#include <utils/threads.h>
#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>

namespace android {
class CameraHardwareSec : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

//Kamat --eclair
    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
#if defined(BOARD_USES_OVERLAY)
    virtual bool        useOverlay();
    virtual status_t    setOverlay(const sp<Overlay> &overlay);
#endif
    virtual void        stopPreview();
    virtual bool        previewEnabled();

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	virtual status_t	startSmartautoscene();
	virtual void		stopSmartautoscene();
	virtual bool		smartautosceneEnabled();
#endif

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void release();

    static sp<CameraHardwareInterface> createInstance(int cameraId);

private:
                        CameraHardwareSec(int cameraId);
    virtual             ~CameraHardwareSec();

    static wp<CameraHardwareInterface> singleton;

    static const int kBufferCount = MAX_BUFFERS;
    static const int kBufferCountForRecord = MAX_BUFFERS;

    class PreviewThread : public Thread {
        CameraHardwareSec* mHardware;
    public:
        PreviewThread(CameraHardwareSec* hw):
#ifdef SINGLE_PROCESS
            // In single process mode this thread needs to be a java thread,
            // since we won't be calling through the binder.
            Thread(true),
#else
            Thread(false),
#endif
	    mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            int ret = mHardware->previewThread();
            // loop until we need to quit
            if(ret == NO_ERROR)
            	return true;
	    else
		return false;
        }
    };

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION	
	class SmartautosceneThread : public Thread {
		CameraHardwareSec* mHardware;
	public:
		SmartautosceneThread(CameraHardwareSec* hw):
#ifdef SINGLE_PROCESS
			// In single process mode this thread needs to be a java thread,
			// since we won't be calling through the binder.
			Thread(true),
#else
			Thread(false),
#endif
		mHardware(hw) { }
		virtual void onFirstRef() {
			run("CameraSmartautosceneThread", PRIORITY_URGENT_DISPLAY);
		}
		virtual bool threadLoop() {
			int ret = mHardware->smartautosceneThread();
			// loop until we need to quit
			if(ret == NO_ERROR)
				return true;
		else
		return false;
		}
	};
#endif
    void initDefaultParameters(int cameraId);
    void initHeapLocked();

    int previewThread();
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION	
	int smartautosceneThread();
#endif

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();
	int save_jpeg( unsigned char * real_jpeg, int jpeg_size);
	void save_postview(const char *fname, uint8_t *buf, uint32_t size);
	int decodeInterleaveData(unsigned char *pInterleaveData,
											 int interleaveDataSize,
											 int yuvWidth,
											 int yuvHeight,
										int *pJpegSize,
										void *pJpegData,
										void *pYuvData);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    static int beginObjectTrackingThread(void *cookie);
    int objectTrackingThread();
    status_t objectTracking(int onoff);
#endif

    mutable Mutex       mLock;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryHeapBase>  mRawHeap;
    sp<MemoryHeapBase>  mRecordHeap;
    sp<MemoryHeapBase>  mJpegHeap;
    sp<MemoryBase>      mBuffers[kBufferCount];
    sp<MemoryBase>      mRecordBuffers[kBufferCountForRecord];

    SecCamera          *mSecCamera;
    bool                mPreviewRunning;
    int                 mPreviewFrameSize;
    int                 mRawFrameSize;
    int			mPreviewFrameRateMicrosec;

#if defined(BOARD_USES_OVERLAY)
    sp<Overlay>         mOverlay;
    bool                mUseOverlay;
#endif

    // protected by mLock
    sp<PreviewThread>   mPreviewThread;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION	
	sp<SmartautosceneThread>   mSmartautosceneThread;
#endif
    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    // only used from PreviewThread
    int                 mCurrentPreviewFrame;
    int                 mCurrentRecordFrame;

    bool                mRecordRunning;
#ifdef JPEG_FROM_SENSOR 
    int					mPostViewWidth;
    int					mPostViewHeight;
    int					mPostViewSize;
#endif	

	int mNoHwHandle;
	struct timeval mTimeStart;
	struct timeval mTimeStop;

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	enum COMMAND_DEFINE
	{
		COMMAND_AE_AWB_LOCK_UNLOCK = 1101,
		COMMAND_FACE_DETECT_LOCK_UNLOCK = 1102,
		COMMAND_OBJECT_POSITION = 1103,
		COMMAND_OBJECT_TRACKING_STARTSTOP = 1104,
		COMMAND_TOUCH_AF_STARTSTOP = 1105,
		COMMAND_CHECK_DATALINE = 1106,
		CONTINUOUS_SHOT_START_CAPTURE = 1023,
		CONTINUOUS_SHOT_STOP_AND_ENCODING = 1024,
		COMMAND_DEFAULT_IMEI = 1107,
	};

class ObjectTrackingThread : public Thread {
        CameraHardwareSec* mHardware;
    public:
        ObjectTrackingThread(CameraHardwareSec* hw):
#ifdef SINGLE_PROCESS
            // In single process mode this thread needs to be a java thread,
            // since we won't be calling through the binder.
            Thread(true),
#else
            Thread(false),
#endif
	    mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraObjectTrackingThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            int ret = mHardware->objectTrackingThread();
            // loop until we need to quit
            if(ret == NO_ERROR)
            	return true;
	    else
		return false;
        }
    };
	bool mObjectTrackingRunning;
	sp<ObjectTrackingThread>   mObjectTrackingThread;
	int			mObjectTrackingStatus;

	bool                mSmartautosceneRunning;
	int 		mSmartautoscene_current_status;
	int 		mSmartautoscene_previous_status;
	int 		af_thread_status;
#endif
};

}; // namespace android

#endif
