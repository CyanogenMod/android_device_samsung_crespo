/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
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

    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void *user);

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

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory> &mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16> &args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void        release();

    static    sp<CameraHardwareInterface> createInstance(int cameraId);

private:
                        CameraHardwareSec(int cameraId);
    virtual             ~CameraHardwareSec();

    static wp<CameraHardwareInterface> singleton;

    static  const int   kBufferCount = MAX_BUFFERS;
    static  const int   kBufferCountForRecord = MAX_BUFFERS;

    class PreviewThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        PreviewThread(CameraHardwareSec *hw):
#ifdef SINGLE_PROCESS
        // In single process mode this thread needs to be a java thread,
        // since we won't be calling through the binder.
        Thread(true),
#else
        Thread(false),
#endif
        mHardware(hw) { }
        virtual bool threadLoop() {
            int ret = mHardware->previewThread();
            // loop until we need to quit
            if(ret == NO_ERROR)
                return true;
            else
                return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        PictureThread(CameraHardwareSec *hw):
        Thread(false),
        mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        AutoFocusThread(CameraHardwareSec *hw): Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAutoFocusThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return true;
        }
    };

            void        initDefaultParameters(int cameraId);
            void        initHeapLocked();

    sp<PreviewThread>   mPreviewThread;
            int         previewThread();
            bool        mPreviewRunning;

    sp<AutoFocusThread> mAutoFocusThread;
            int         autoFocusThread();

    sp<PictureThread>   mPictureThread;
            int         pictureThread();
            bool        mCaptureInProgress;

            int         save_jpeg(unsigned char *real_jpeg, int jpeg_size);
            void        save_postview(const char *fname, uint8_t *buf,
                                        uint32_t size);
            int         decodeInterleaveData(unsigned char *pInterleaveData,
                                                int interleaveDataSize,
                                                int yuvWidth,
                                                int yuvHeight,
                                                int *pJpegSize,
                                                void *pJpegData,
                                                void *pYuvData);
            bool        YUY2toNV21(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight);
            bool        scaleDownYuv422(char *srcBuf, uint32_t srcWidth,
                                        uint32_t srcHight, char *dstBuf,
                                        uint32_t dstWidth, uint32_t dstHight);

            bool        CheckVideoStartMarker(unsigned char *pBuf);
            bool        CheckEOIMarker(unsigned char *pBuf);
            bool        FindEOIMarkerInJPEG(unsigned char *pBuf,
                                            int dwBufSize, int *pnJPEGsize);
            bool        SplitFrame(unsigned char *pFrame, int dwSize,
                                   int dwJPEGLineLength, int dwVideoLineLength,
                                   int dwVideoHeight, void *pJPEG,
                                   int *pdwJPEGSize, void *pVideo,
                                   int *pdwVideoSize);
            void        setSkipFrame(int frame);
    /* used by auto focus thread to block until it's told to run */
    mutable Mutex       mFocusLock;
    mutable Condition   mCondition;
            bool        mExitAutoFocusThread;

    /* used to guard threading state */
    mutable Mutex       mStateLock;

    CameraParameters    mParameters;
    CameraParameters    mInternalParameters;

    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryHeapBase>  mRawHeap;
    sp<MemoryHeapBase>  mRecordHeap;
    sp<MemoryHeapBase>  mJpegHeap;
    sp<MemoryBase>      mBuffers[kBufferCount];
    sp<MemoryBase>      mRecordBuffers[kBufferCountForRecord];

            SecCamera   *mSecCamera;
            int         mPreviewFrameSize;
            int         mRawFrameSize;
            int         mPreviewFrameRateMicrosec;
            const __u8  *mCameraSensorName;

    mutable Mutex       mSkipFrameLock;
            int         mSkipFrame;

#if defined(BOARD_USES_OVERLAY)
            sp<Overlay> mOverlay;
            bool        mUseOverlay;
            int         mOverlayBufferIdx;
#endif

    notify_callback     mNotifyCb;
    data_callback       mDataCb;
    data_callback_timestamp mDataCbTimestamp;
            void        *mCallbackCookie;

            int32_t     mMsgEnabled;

            // only used from PreviewThread
            int         mCurrentPreviewFrame;
            int         mCurrentRecordFrame;

            bool        mRecordRunning;
#ifdef JPEG_FROM_SENSOR
            int         mPostViewWidth;
            int         mPostViewHeight;
            int         mPostViewSize;
#endif

    struct timeval      mTimeStart;
    struct timeval      mTimeStop;

};

}; // namespace android

#endif
