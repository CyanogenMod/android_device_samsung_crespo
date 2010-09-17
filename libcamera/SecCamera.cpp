/*
 * Copyright@ Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

/*
************************************
* Filename: SecCamera.cpp 
* Author:   Sachin P. Kamat
* Purpose:  This file interacts with the Camera and JPEG drivers.
*************************************
*/
 
//#define LOG_NDEBUG 0
#define LOG_TAG "SecCamera"
#define ADD_THUMB_IMG 1

#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include "SecCamera.h"
#include "cutils/properties.h"

#ifdef BOARD_USES_SDTV
#include "TvOut.h"
#endif

using namespace android;

#ifdef BOARD_USES_SDTV
#include "utils/Timers.h"
//#define  MEASURE_DURATION_TVOUT
sp<TvOut> mtvoutcamera; 
static bool  suspendTvInit = false ; 
#define TVOUT_RESUME_TIME 4
#endif

//#define PERFORMANCE //Uncomment to measure performance
//#define DUMP_YUV    //Uncomment to take a dump of YUV frame during capture

#define CHECK(return_value)         \
                if(return_value < 0)                                            \
                {                                                                     \
                        LOGE("%s::%d fail. errno: %s\n", __func__,__LINE__, strerror(errno));  \
                        return -1;                                              \
                }                                                                     \

#define CHECK_PTR(return_value)         \
                if(return_value < 0)                                            \
                {                                                                     \
                        LOGE("%s::%d fail\n", __func__,__LINE__);  \
                        return NULL;                                              \
                }                                                                     \

#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)

namespace android {

// ======================================================================
// Camera controls

static struct timeval time_start;
static struct timeval time_stop;

unsigned long measure_time(struct timeval *start, struct timeval *stop)
{

	unsigned long sec, usec, time;

	sec = stop->tv_sec - start->tv_sec;

	if (stop->tv_usec >= start->tv_usec) {
		usec = stop->tv_usec - start->tv_usec;
	} else {
		usec = stop->tv_usec + 1000000 - start->tv_usec;
		sec--;
		}

	time = (sec * 1000000) + usec;

	return time;
}

static inline unsigned long check_performance()
{
	unsigned long time = 0;
	static unsigned long max=0, min=0xffffffff;

	if(time_start.tv_sec == 0 && time_start.tv_usec == 0) {
		gettimeofday(&time_start, NULL);
	} else {
		gettimeofday(&time_stop, NULL);
		time = measure_time(&time_start, &time_stop);
		if(max < time) max = time;
		if(min > time) min = time;
		LOGV("Interval: %lu us (%2.2lf fps), min:%2.2lf fps, max:%2.2lf fps\n", time, 1000000.0/time, 1000000.0/max, 1000000.0/min);
		gettimeofday(&time_start, NULL);
	}

	return time;
}

static int close_buffers(struct fimc_buffer *buffers)
{
	int i;

	for (i = 0; i < MAX_BUFFERS; i++) {
		if (buffers[i].start)
		{
			munmap(buffers[i].start, buffers[i].length);
			//LOGV("munmap():virt. addr[%d]: 0x%x size = %d\n", i, (unsigned int) buffers[i].start, buffers[i].length);
			buffers[i].start = NULL;
		}
	}

	return 0;
}

static int get_pixel_depth(unsigned int fmt)
{
        int depth = 0;

        switch (fmt) {
        case V4L2_PIX_FMT_NV12:
                depth = 12;
                break;
        case V4L2_PIX_FMT_NV12T:
                depth = 12;
                break;
        case V4L2_PIX_FMT_NV21:
                depth = 12;
                break;
        case V4L2_PIX_FMT_YUV420:
                depth = 12;
                break;

        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YVYU:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_VYUY:
        case V4L2_PIX_FMT_NV16:
        case V4L2_PIX_FMT_NV61:
        case V4L2_PIX_FMT_YUV422P:
                depth = 16;
                break;

        case V4L2_PIX_FMT_RGB32:
                depth = 32;
                break;
        }

        return depth;
}

#define ALIGN_W(x)      ((x+0x7F)&(~0x7F)) // Set as multiple of 128
#define ALIGN_H(x)      ((x+0x1F)&(~0x1F)) // Set as multiple of 32
#define ALIGN_BUF(x)    ((x+0x1FFF)&(~0x1FFF)) // Set as multiple of 8K

static int init_yuv_buffers(struct fimc_buffer *buffers, int width, int height, unsigned int fmt)
{
	int i, len;
	
	len = (width * height * get_pixel_depth(fmt)) / 8;
	

	for (i = 0; i < MAX_BUFFERS; i++) 
	{
		if(fmt==V4L2_PIX_FMT_NV12T)
                {
                	buffers[i].start = NULL;
	                buffers[i].length = ALIGN_BUF(ALIGN_W(width) * ALIGN_H(height))+ ALIGN_BUF(ALIGN_W(width) * ALIGN_H(height/2));
                }
		else
		{
			buffers[i].start = NULL;
			buffers[i].length = len;
		}
	}

	return 0;
}

static int fimc_poll(struct pollfd *events)
{
	int ret;

	ret = poll(events, 1, 5000);
	if (ret < 0) {
		LOGE("ERR(%s):poll error\n", __FUNCTION__);
		return ret;
	}

	if (ret == 0) {
		LOGE("ERR(%s):No data in 5 secs..\n", __FUNCTION__);
		return ret;
	}

	return ret;
}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
static int fimc_esd_poll(struct pollfd *events)
{
	int ret;

	ret = poll(events, 1, 1000);

	if (ret < 0) {
		LOGE("ERR(%s):poll error\n", __FUNCTION__);
		return ret;
	}

	if (ret == 0) {
		LOGE("ERR(%s):No data in 1 secs.. Camera Device Reset \n", __FUNCTION__);
		return ret;
	}

	return ret;
}
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */

#ifdef DUMP_YUV
static int save_yuv(struct fimc_buffer *m_buffers_c, int width, int height, int depth, int index, int frame_count)
{
        FILE *yuv_fp = NULL;
        char filename[100], *buffer = NULL;

        /* file create/open, note to "wb" */
        yuv_fp = fopen("/data/main.yuv", "wb");
        if (yuv_fp==NULL)
	{
		LOGE("Save YUV] file open error");
		return -1;
	}

		buffer = (char *) malloc(m_buffers_c[index].length);
	if(buffer == NULL)
	{
		LOGE("Save YUV] buffer alloc failed");
		if(yuv_fp) fclose(yuv_fp);
		return -1;
	}

        memcpy(buffer, m_buffers_c[index].start, m_buffers_c[index].length);

        fflush(stdout);

		fwrite(buffer, 1, m_buffers_c[index].length, yuv_fp);

        fflush(yuv_fp);

	if(yuv_fp)
	        fclose(yuv_fp);
	if(buffer)
	        free(buffer);

        return 0;
}
#endif //DUMP_YUV

static int fimc_v4l2_querycap(int fp)
{
	struct v4l2_capability cap;
	int ret = 0;

	ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);

	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __FUNCTION__);
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOGE("ERR(%s):no capture devices\n", __FUNCTION__);
		return -1;
	}

	return ret;
}

static int fimc_v4l2_enuminput(int fp, int index)
{
        struct v4l2_input input;

        input.index = index;
        if(ioctl(fp, VIDIOC_ENUMINPUT, &input) != 0)
	{
		LOGE("ERR(%s):No matching index found\n", __FUNCTION__);
		return -1;
	}
	LOGI("Name of input channel[%d] is %s\n", input.index, input.name);

	return 0;
}


static int fimc_v4l2_s_input(int fp, int index)
{
	struct v4l2_input input;
	int ret;

	input.index = index;

	ret = ioctl(fp, VIDIOC_S_INPUT, &input);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __FUNCTION__);
		return ret;
	}

	return ret;
}

static int fimc_v4l2_s_fmt(int fp, int width, int height, unsigned int fmt, int flag_capture)
{
        struct v4l2_format v4l2_fmt;
        struct v4l2_pix_format pixfmt;
        int ret;

        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		memset(&pixfmt, 0, sizeof(pixfmt));

        pixfmt.width = width;
        pixfmt.height = height;
        pixfmt.pixelformat = fmt;
		
        pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

		pixfmt.field = V4L2_FIELD_NONE;

        v4l2_fmt.fmt.pix = pixfmt;

        /* Set up for capture */
        ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __FUNCTION__);
                return -1;
        }

        return 0;
}

static int fimc_v4l2_s_fmt_cap(int fp, int width, int height, unsigned int fmt)
{
        struct v4l2_format v4l2_fmt;
        struct v4l2_pix_format pixfmt;
        int ret;

		memset(&pixfmt, 0, sizeof(pixfmt));

        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        pixfmt.width = width;
        pixfmt.height = height;
        pixfmt.pixelformat = fmt;
	if (fmt == V4L2_PIX_FMT_JPEG){
		pixfmt.colorspace = V4L2_COLORSPACE_JPEG;
	}

        pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

        v4l2_fmt.fmt.pix = pixfmt;

	//LOGE("ori_w %d, ori_h %d, w %d, h %d\n", width, height, v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

        /* Set up for capture */
        ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
        if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __FUNCTION__);
                return ret;
        }

        return ret;
}

static int fimc_v4l2_enum_fmt(int fp, unsigned int fmt)
{
        struct v4l2_fmtdesc fmtdesc;
        int found = 0;

        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmtdesc.index = 0;

        while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
                if (fmtdesc.pixelformat == fmt) {
                        LOGD("passed fmt = %d found pixel format[%d]: %s\n", fmt, fmtdesc.index, fmtdesc.description);
                        found = 1;
                        break;
                }

                fmtdesc.index++;
        }

        if (!found) {
                LOGE("unsupported pixel format\n");
                return -1;
        }

        return 0;
}

static int fimc_v4l2_reqbufs(int fp, enum v4l2_buf_type type, int nr_bufs)
{
        struct v4l2_requestbuffers req;
        int ret;

        req.count = nr_bufs;
        req.type = type;
        req.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fp, VIDIOC_REQBUFS, &req);
        if(ret < 0) {
		LOGE("ERR(%s):VIDIOC_REQBUFS failed\n", __FUNCTION__);
                return -1;
        }

        return req.count;
}

static int fimc_v4l2_querybuf(int fp, struct fimc_buffer *buffers, enum v4l2_buf_type type, int nr_frames)
{
	struct v4l2_buffer v4l2_buf;
	int i, ret;

	for(i = 0; i < nr_frames; i++)
	{
		v4l2_buf.type = type;
		v4l2_buf.memory = V4L2_MEMORY_MMAP;
		v4l2_buf.index = i;

		ret = ioctl(fp , VIDIOC_QUERYBUF, &v4l2_buf);
		if(ret < 0) 
		{
			LOGE("ERR(%s):VIDIOC_QUERYBUF failed\n", __FUNCTION__);
			return -1;
		}

		if(nr_frames == 1)
		{
			buffers[i].length = v4l2_buf.length;
			if ((buffers[i].start = (char *) mmap(0, v4l2_buf.length, \
							PROT_READ | PROT_WRITE, MAP_SHARED, \
							fp, v4l2_buf.m.offset)) < 0)
			{
				LOGE("%s %d] mmap() failed\n",__FUNCTION__, __LINE__);
				return -1;
			}

			//LOGV("buffers[%d].start = %p v4l2_buf.length = %d", i, buffers[i].start, v4l2_buf.length);
		}
		else
		{

#if defined DUMP_YUV || defined (SEND_YUV_RECORD_DATA) 
			buffers[i].length = v4l2_buf.length;
			if ((buffers[i].start = (char *) mmap(0, v4l2_buf.length, \
							PROT_READ | PROT_WRITE, MAP_SHARED, \
							fp, v4l2_buf.m.offset)) < 0)
			{
				LOGE("%s %d] mmap() failed\n",__FUNCTION__, __LINE__);
				return -1;
			}

			//LOGV("buffers[%d].start = %p v4l2_buf.length = %d", i, buffers[i].start, v4l2_buf.length);
#endif
		}

	}

	return 0;
}

static int fimc_v4l2_streamon(int fp)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

	ret = ioctl(fp, VIDIOC_STREAMON, &type);
	if (ret < 0) {
			LOGE("ERR(%s):VIDIOC_STREAMON failed\n", __FUNCTION__);
			return ret;
	}

	return ret;
}

static int fimc_v4l2_streamoff(int fp)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGE("%s()", __FUNCTION__);
#endif
	ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
			LOGE("ERR(%s):VIDIOC_STREAMOFF failed\n", __FUNCTION__);
			return ret;
	}

	return ret;
}

static int fimc_v4l2_qbuf(int fp, int index)
{
        struct v4l2_buffer v4l2_buf;
        int ret;

        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;
        v4l2_buf.index = index;

        ret = ioctl(fp, VIDIOC_QBUF, &v4l2_buf);
        if (ret < 0) {
	        LOGE("ERR(%s):VIDIOC_QBUF failed\n", __FUNCTION__);
                return ret;
        }

        return 0;
}

static int fimc_v4l2_dqbuf(int fp)
{
        struct v4l2_buffer v4l2_buf;
        int ret;

        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fp, VIDIOC_DQBUF, &v4l2_buf);
        if (ret < 0) {
	        LOGE("ERR(%s):VIDIOC_DQBUF failed\n", __FUNCTION__);
                return ret;
        }

        return v4l2_buf.index;
}

static int fimc_v4l2_g_ctrl(int fp, unsigned int id)
{
        struct v4l2_control ctrl;
        int ret;

        ctrl.id = id;

        ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
        if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_G_CTRL failed\n", __FUNCTION__);
		return ret;
        }

        return ctrl.value;
}

static int fimc_v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
        struct v4l2_control ctrl;
        int ret;

        ctrl.id = id;
        ctrl.value = value;

        ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
        if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_CTRL failed, ret: %d\n", __FUNCTION__, ret);
		return ret;
        }

        return ctrl.value;
}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
static int fimc_v4l2_s_ext_ctrl(int fp, unsigned int id, void * value)
{
        struct v4l2_ext_controls ctrls;
        struct v4l2_ext_control ctrl;
        int ret;

	  ctrl.id = id;
	  ctrl.reserved = value;

	  ctrls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
	  ctrls.count = 1;
	  ctrls.controls = &ctrl;

        ret = ioctl(fp, VIDIOC_S_EXT_CTRLS, &ctrls);
        if (ret < 0)
		LOGE("ERR(%s):VIDIOC_S_EXT_CTRLS failed\n", __FUNCTION__);
        
        return ret;
}
#endif
static int fimc_v4l2_g_parm(int fp)
{
        struct v4l2_streamparm stream;
        int ret;

        stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(fp, VIDIOC_G_PARM, &stream);
        if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_G_PARM failed\n", __FUNCTION__);
		return -1;
        }

//        LOGV("timeperframe: numerator %d, denominator %d\n", \
                stream.parm.capture.timeperframe.numerator, \
                stream.parm.capture.timeperframe.denominator);

        return 0;
}

static int fimc_v4l2_s_parm(int fp, int fps_numerator, int fps_denominator)
{
        struct v4l2_streamparm stream;
        int ret;

        stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        stream.parm.capture.capturemode = 0;
        stream.parm.capture.timeperframe.numerator = fps_numerator;
        stream.parm.capture.timeperframe.denominator = fps_denominator;

        ret = ioctl(fp, VIDIOC_S_PARM, &stream);
        if (ret < 0) {
                LOGE("ERR(%s):VIDIOC_S_PARM failed\n", __FUNCTION__);
                return ret;
        }

        return 0;
}

#if 0
static int fimc_v4l2_s_parm_ex(int fp, int mode, int no_dma_op) //Kamat: not present in new code
{
	struct v4l2_streamparm stream;
	int ret;

	stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	stream.parm.capture.capturemode = mode;
	if(no_dma_op)
		stream.parm.capture.reserved[0] = 100;
		
	ret = ioctl(fp, VIDIOC_S_PARM, &stream);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_PARM_EX failed\n", __FUNCTION__);
		return ret;
	}

	return 0;
}
#endif

// ======================================================================
// Constructor & Destructor

SecCamera::SecCamera() :
			m_focus_mode(1),
			m_iso(0),
			m_camera_id(CAMERA_ID_BACK),
			m_preview_v4lformat(-1),
			m_preview_width 	 (0),
			m_preview_height	 (0),
			m_preview_max_width  (MAX_BACK_CAMERA_PREVIEW_WIDTH),
			m_preview_max_height (MAX_BACK_CAMERA_PREVIEW_HEIGHT),
			m_snapshot_v4lformat(-1),
			m_snapshot_width	  (0),
			m_snapshot_height	  (0),
			m_snapshot_max_width  (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
			m_snapshot_max_height (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
			m_angle(0),
			m_fps(30),
			m_autofocus(AUTO_FOCUS_ON),
			m_white_balance(WHITE_BALANCE_AUTO),
			m_brightness(BRIGHTNESS_NORMAL),
			m_image_effect(IMAGE_EFFECT_NONE),
			#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
			m_anti_banding(0),
			m_flash_mode(1),
			m_metering(2),
			m_contrast(2),
			m_saturation(2),
			m_sharpness(2),
			m_wdr(0),
			m_anti_shake(0),
			m_zoom_level(0),
			m_object_tracking(0),
			m_smart_auto(0),
			m_beauty_shot(0),
			m_vintage_mode(1),
			m_face_detect(0),
			m_gps_latitude(0),
			m_gps_longitude(0),
			m_gps_altitude(0),
			m_gps_timestamp(0),
			m_vtmode(0),
			m_sensor_mode(0),
			m_exif_orientation(1),
			m_blur_level(0),
			m_chk_dataline(0),	
			m_video_gamma(0),
			m_slow_ae(0),
			#else
			m_image_effect(IMAGE_EFFECT_ORIGINAL),
			#endif
			m_flag_camera_start(0),
			m_jpeg_thumbnail_width (0),
			m_jpeg_thumbnail_height(0),
			m_jpeg_quality(100),
			m_camera_af_flag(-1),
			m_shot_mode(0),
			m_flag_init(0)
{
	LOGV("%s()", __FUNCTION__);
#ifdef BOARD_USES_SDTV
	nsecs_t   before1, after1;

#ifdef	MEASURE_DURATION_TVOUT
	before1  = systemTime(SYSTEM_TIME_MONOTONIC);
#endif
		//suspend
		if(mtvoutcamera == 0) {
		 	mtvoutcamera = TvOut::connect();
		}
			if(mtvoutcamera != 0 )
		{  
			if (mtvoutcamera->isEnabled() )
			{
				mtvoutcamera->DisableTvOut() ; //                          //TvOutSuspend("Tvout is not available! so what can i do, close camera or any other app which uses fimc");
				suspendTvInit = true; 
			}
		} 

#ifdef	MEASURE_DURATION_TVOUT
	after1  = systemTime(SYSTEM_TIME_MONOTONIC);
	LOGD("%s: MEASURE_DURATION_TVOUT duration=%lld", __func__, ns2us(after1-before1));
#endif

#endif 
}

int SecCamera::flagCreate(void) const
{
	LOGV("%s() : %d", __FUNCTION__, m_flag_init);
	return m_flag_init;
}

SecCamera::~SecCamera()
{
	LOGV("%s()", __FUNCTION__);

#ifdef BOARD_USES_SDTV
	nsecs_t   before1, after1;

#ifdef	MEASURE_DURATION_TVOUT
	before1  = systemTime(SYSTEM_TIME_MONOTONIC);
#endif

		//resume
		if(mtvoutcamera != 0 )
		{
			if (!mtvoutcamera->isEnabled() && (mtvoutcamera->isSuspended() ))
			{
				mtvoutcamera->TvOutResume(TVOUT_RESUME_TIME); 
			}
		}
		suspendTvInit = false;
#ifdef	MEASURE_DURATION_TVOUT
	after1  = systemTime(SYSTEM_TIME_MONOTONIC);
	LOGD("%s: MEASURE_DURATION_TVOUT duration=%lld", __func__, ns2us(after1-before1));
#endif
#endif 
}

int SecCamera::initCamera(int index)
{
	LOGV("%s()", __FUNCTION__);
	int ret = 0;

	if(!m_flag_init)
	{
		/* Arun C
		 * Reset the lense position only during camera starts; don't do
		 * reset between shot to shot
		 */
		m_camera_af_flag = -1; 

#ifdef BOARD_USES_SDTV
	nsecs_t   before1, after1;

#ifdef	MEASURE_DURATION_TVOUT
	before1  = systemTime(SYSTEM_TIME_MONOTONIC);
#endif
	
	//suspend
		if(mtvoutcamera == 0) {
		 	mtvoutcamera = TvOut::connect();
		}
		
		
	
		if(mtvoutcamera != 0 )
		{  
		
			if ( mtvoutcamera->isEnabled()  ) 
			{

				mtvoutcamera->DisableTvOut() ; 
				suspendTvInit = true; 
			}
		} 
	
#ifdef	MEASURE_DURATION_TVOUT
	after1  = systemTime(SYSTEM_TIME_MONOTONIC);
	LOGD("%s: MEASURE_DURATION_TVOUT duration=%lld", __func__, ns2us(after1-before1));
#endif


#endif
#ifndef JPEG_FROM_SENSOR
		m_jpeg_fd = SsbSipJPEGEncodeInit();
		LOGD("(%s):JPEG device open ID = %d\n", __FUNCTION__,m_jpeg_fd);
		if(m_jpeg_fd < 0)
		{
			m_jpeg_fd = 0;
			LOGE("ERR(%s):Cannot open a jpeg device file\n", __FUNCTION__);
			return -1;
		}
#endif

		m_cam_fd_temp = -1;
		m_cam_fd2_temp = -1;

		m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
		if (m_cam_fd < 0) 
		{
			LOGE("ERR(%s):Cannot open %s (error : %s)\n", __FUNCTION__, CAMERA_DEV_NAME, strerror(errno));
#ifndef JPEG_FROM_SENSOR
			SsbSipJPEGEncodeDeInit(m_jpeg_fd);
#endif
			return -1;
		}

		if(m_cam_fd < 3) { // for 0, 1, 2
			LOGE("ERR(%s):m_cam_fd is %d\n", __FUNCTION__, m_cam_fd);

			close(m_cam_fd);
			
			m_cam_fd_temp = open(CAMERA_DEV_NAME_TEMP, O_CREAT);
			
			LOGE("ERR(%s):m_cam_fd_temp is %d\n", __FUNCTION__, m_cam_fd_temp);

			m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
			
			if(m_cam_fd < 3) { // for 0, 1, 2
				LOGE("ERR(%s):retring to open %s is failed, %d\n", __FUNCTION__, CAMERA_DEV_NAME, m_cam_fd);

				if (m_cam_fd < 0){
					return -1;
				}
				else{
					close(m_cam_fd);
					m_cam_fd = -1;
				}
				
				if(m_cam_fd_temp != -1){
					close(m_cam_fd_temp);
					m_cam_fd_temp = -1;
				}
				return -1;
			}
		}

		LOGE("initCamera: m_cam_fd(%d), m_jpeg_fd(%d)", m_cam_fd, m_jpeg_fd);

		ret = fimc_v4l2_querycap(m_cam_fd);
		CHECK(ret);
		ret = fimc_v4l2_enuminput(m_cam_fd, index);
		CHECK(ret);
		ret = fimc_v4l2_s_input(m_cam_fd, index);
		CHECK(ret);

#ifdef DUAL_PORT_RECORDING
		m_cam_fd2 = open(CAMERA_DEV_NAME2, O_RDWR);
		if (m_cam_fd2 < 0) 
		{
			LOGE("ERR(%s):Cannot open %s (error : %s)\n", __FUNCTION__, CAMERA_DEV_NAME2, strerror(errno));
			return -1;
		}
		if(m_cam_fd2 < 3) { // for 0, 1, 2
			LOGE("ERR(%s):m_cam_fd2 is %d\n", __FUNCTION__, m_cam_fd2);

			close(m_cam_fd2);
			
			m_cam_fd2_temp = open(CAMERA_DEV_NAME2_TEMP, O_CREAT);

			LOGE("ERR(%s):m_cam_fd2_temp is %d\n", __FUNCTION__, m_cam_fd2_temp);

			m_cam_fd2 = open(CAMERA_DEV_NAME2, O_RDWR);
			
			if(m_cam_fd2 < 3) { // for 0, 1, 2
				LOGE("ERR(%s):retring to open %s is failed, %d\n", __FUNCTION__, CAMERA_DEV_NAME2, m_cam_fd2);

				if (m_cam_fd2 < 0){
					return -1;
				}
				else{
					close(m_cam_fd2);
					m_cam_fd2 = -1;
				}
				
				if(m_cam_fd2_temp != -1){
					close(m_cam_fd2_temp);
					m_cam_fd2_temp = -1;
				}
				
				return -1;
			}
		}

		if(m_cam_fd_temp != -1){
			close(m_cam_fd_temp);
			m_cam_fd_temp = -1;
		}

		if(m_cam_fd2_temp != -1){
			close(m_cam_fd2_temp);
			m_cam_fd2_temp = -1;
		}

		LOGE("initCamera: m_cam_fd2(%d)", m_cam_fd2);

		ret = fimc_v4l2_querycap(m_cam_fd2);
		CHECK(ret);
		ret = fimc_v4l2_enuminput(m_cam_fd2, index);
		CHECK(ret);
		ret = fimc_v4l2_s_input(m_cam_fd2, index);
		CHECK(ret);
#endif
        setExifFixedAttribute();

		m_camera_id = index;
		m_flag_init = 1;
	}
	return 0;
}

void SecCamera::resetCamera()
{
	LOGV("%s()", __FUNCTION__);
	DeinitCamera();
	initCamera(m_camera_id);
}

void SecCamera::DeinitCamera()
{
	LOGV("%s()", __FUNCTION__);

	if(m_flag_init)
	{
#ifndef JPEG_FROM_SENSOR
		if(m_jpeg_fd > 0)
		{
			if(SsbSipJPEGEncodeDeInit(m_jpeg_fd) != JPEG_OK)
			{
				LOGE("ERR(%s):Fail on SsbSipJPEGEncodeDeInit\n", __FUNCTION__);
			}
			m_jpeg_fd = 0;
		}
#endif
		LOGE("DeinitCamera: m_cam_fd(%d)", m_cam_fd);
		if(m_cam_fd > -1)
		{
			close(m_cam_fd);
			m_cam_fd = -1;
		}
#ifdef DUAL_PORT_RECORDING
		LOGE("DeinitCamera: m_cam_fd2(%d)", m_cam_fd2);
		if(m_cam_fd2 > -1)
		{
			close(m_cam_fd2);
			m_cam_fd2 = -1;
		}
#endif
		if(m_cam_fd_temp != -1){
			close(m_cam_fd_temp);
			m_cam_fd_temp = -1;
		}

		if(m_cam_fd2_temp != -1){
			close(m_cam_fd2_temp);
			m_cam_fd2_temp = -1;
		}

#ifdef  BOARD_USES_SDTV
	nsecs_t   before1, after1;

#ifdef	MEASURE_DURATION_TVOUT
	before1  = systemTime(SYSTEM_TIME_MONOTONIC);
#endif

	if(mtvoutcamera == 0) {
		 	mtvoutcamera = TvOut::connect();
				
		}

		//resume
		if(mtvoutcamera != 0 )
		{

			if(mtvoutcamera->isSuspended()) 
			{ 
				mtvoutcamera->TvOutResume(TVOUT_RESUME_TIME); 
			}
		}
#ifdef	MEASURE_DURATION_TVOUT
	after1  = systemTime(SYSTEM_TIME_MONOTONIC);
	LOGD("%s: MEASURE_DURATION_TVOUT duration=%lld", __func__, ns2us(after1-before1));
#endif
#endif
		m_flag_init = 0;
		usleep(100000); //100 ms delay to allow proper closure of fimc device.
	}
}


int SecCamera::getCameraFd(void)
{
	return m_cam_fd;
}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
int SecCamera::setCameraSensorReset(void)
{
	int ret = 0;
	
	LOGV("%s", __FUNCTION__);

	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_RESET, 0);
	CHECK(ret);
	
	return ret;
}

int SecCamera::setDefultIMEI(int imei)	
{
	LOGV("%s(m_default_imei (%d))", __FUNCTION__, imei);
	
	if(m_default_imei != imei)
	{
		m_default_imei = imei;
	}
	return 0;
}

int	SecCamera::getDefultIMEI(void)
{
	return m_default_imei;
}


#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */


// ======================================================================
// Preview

int SecCamera::flagPreviewStart(void)
{
	LOGV("%s:started(%d)", __func__, m_flag_camera_start);
	
	return m_flag_camera_start > 0;
}

int SecCamera::startPreview(void)
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGV("%s()", __FUNCTION__);
#else	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	LOGE("%s()", __FUNCTION__);
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	
	// aleady started
	if(m_flag_camera_start > 0)
	{
		LOGE("ERR(%s):Preview was already started\n", __FUNCTION__);
		return 0;
	}

	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	memset(&m_events_c, 0, sizeof(m_events_c));
      	m_events_c.fd = m_cam_fd;
	m_events_c.events = POLLIN | POLLERR;

        /* enum_fmt, s_fmt sample */
        int ret = fimc_v4l2_enum_fmt(m_cam_fd,m_preview_v4lformat);
	CHECK(ret);
        ret = fimc_v4l2_s_fmt(m_cam_fd, m_preview_width,m_preview_height,m_preview_v4lformat, 0);
	CHECK(ret);

	init_yuv_buffers(m_buffers_c, m_preview_width, m_preview_height, m_preview_v4lformat);
	ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
	CHECK(ret);
	ret = fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
	CHECK(ret);

        /* g_parm, s_parm sample */
        ret = fimc_v4l2_g_parm(m_cam_fd);
	CHECK(ret);
        ret = fimc_v4l2_s_parm(m_cam_fd, 1, m_fps);
	CHECK(ret);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION	
	LOGE("%s()m_preview_width: %d m_preview_height: %d m_angle: %d\n", __FUNCTION__, m_preview_width,m_preview_height,m_angle);
	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_ROTATION, m_angle);
	CHECK(ret);

	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CHECK_DATALINE, m_chk_dataline);
	CHECK(ret);

	if(m_camera_id == CAMERA_ID_BACK) 
	{
		/*Should be set before starting preview*/
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ANTI_BANDING, m_anti_banding);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ISO, m_iso);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BRIGHTNESS, m_brightness + BRIGHTNESS_NORMAL);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FRAME_RATE, m_fps);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_METERING, m_metering);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_GAMMA, m_video_gamma);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_SLOW_AE, m_slow_ae);
		CHECK(ret);		
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_EFFECT, m_image_effect);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_WHITE_BALANCE, m_white_balance);
		CHECK(ret);
	}
	else
	{
		/* VT mode setting */
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_VT_MODE, m_vtmode);
		CHECK(ret);
	}

#endif
	 /* start with all buffers in queue */
        for (int i = 0; i < MAX_BUFFERS; i++)
	{
                ret = fimc_v4l2_qbuf(m_cam_fd, i);
		CHECK(ret);
	}
	
	ret = fimc_v4l2_streamon(m_cam_fd);
	CHECK(ret);
	
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	m_flag_camera_start = 1;  //Kamat check

	if(m_camera_id == CAMERA_ID_BACK) {
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_EFFECT, m_image_effect);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_WHITE_BALANCE, m_white_balance);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ISO, m_iso);
		CHECK(ret);
		if(m_focus_mode ==FOCUS_MODE_FACEDETECT)
			ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, FOCUS_MODE_AUTO);
		else{
			if(m_shot_mode == 2){	//Panorama shot
				ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, m_focus_mode);
				CHECK(ret);
				m_camera_af_flag = -1;
			}
			else if(m_shot_mode == 3){	//Smile shot
				ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, m_focus_mode);
				CHECK(ret);
				m_camera_af_flag = -1;
			}
			else if (m_camera_af_flag < 0) {	//After captur, Focus is fixed.
				ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, m_focus_mode);
				CHECK(ret);
				m_camera_af_flag = 0;
			}
		}
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FRAME_RATE, m_fps);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_METERING, m_metering);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SATURATION, m_saturation);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CONTRAST, m_contrast);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ZOOM, m_zoom_level);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SENSOR_MODE, m_sensor_mode);
		CHECK(ret);
		// Apply the scene mode only in camera not in camcorder
		if (!m_sensor_mode)
		{
			ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SCENE_MODE, m_scene_mode);
			CHECK(ret);
		}
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BRIGHTNESS, m_brightness + BRIGHTNESS_NORMAL);
		CHECK(ret);
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SHARPNESS, m_sharpness);
		CHECK(ret);		
	}
	else // In case VGA camera
	{
		/* Brightness setting */
		ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BRIGHTNESS, m_brightness + BRIGHTNESS_NORMAL);
		CHECK(ret);  
	}
#endif

	// It is a delay for a new frame, not to show the previous bigger ugly picture frame.
	ret = fimc_poll(&m_events_c);
	CHECK(ret);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGE("%s: get the first frame of the preview\n", __FUNCTION__);
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */

#ifndef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	m_flag_camera_start = 1;  //Kamat check
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */

#ifdef ENABLE_HDMI_DISPLAY
	hdmi_initialize(m_preview_width,m_preview_height);
	hdmi_gl_initialize(0);
	hdmi_gl_streamoff(0);
#endif	
#ifdef BOARD_USES_SDTV

	if(suspendTvInit  ==true )
	{
			if(!mtvoutcamera->isSuspended())
			{
				mtvoutcamera->TvOutSuspend("") ; 
			}
			}

#endif 

	return 0;
}

int SecCamera::stopPreview(void)
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGE("%s()\n", __FUNCTION__);
	
	close_buffers(m_buffers_c);

	if(m_flag_camera_start == 0) {		
		LOGE("%s: m_flag_camera_start is zero", __FUNCTION__);
		return 0;
	}
#else	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	LOGV("%s()", __FUNCTION__);
	
	close_buffers(m_buffers_c);

	if(m_flag_camera_start == 0)
		return 0;
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */	
#ifdef ENABLE_HDMI_DISPLAY
	hdmi_deinitialize();
	hdmi_gl_streamon(0);
#endif

	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	int ret = fimc_v4l2_streamoff(m_cam_fd);

	m_flag_camera_start = 0; //Kamat check
	CHECK(ret);

	return ret;
}

//Recording
#ifdef DUAL_PORT_RECORDING
int SecCamera::startRecord(void)
{
	LOGV("%s()", __FUNCTION__);
	
	// aleady started
	if(m_flag_record_start > 0)
	{
		LOGE("ERR(%s):Preview was already started\n", __FUNCTION__);
		return 0;
	}

	if(m_cam_fd2 <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	memset(&m_events_c2, 0, sizeof(m_events_c2));
      	m_events_c2.fd = m_cam_fd2;
	m_events_c2.events = POLLIN | POLLERR;

	int m_record_v4lformat = V4L2_PIX_FMT_NV12T; //Kamat: set suitably
        /* enum_fmt, s_fmt sample */
        int ret = fimc_v4l2_enum_fmt(m_cam_fd2,m_record_v4lformat);
	CHECK(ret);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGE("%s: m_recording_width = %d, m_recording_height = %d\n", __FUNCTION__, m_recording_width, m_recording_height);

	ret = fimc_v4l2_s_fmt(m_cam_fd2, m_recording_width, m_recording_height,m_record_v4lformat, 0);

	CHECK(ret);

	init_yuv_buffers(m_buffers_c2, m_recording_width, m_recording_height, m_record_v4lformat);
#else	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
        ret = fimc_v4l2_s_fmt(m_cam_fd2, m_preview_width,m_preview_height,m_record_v4lformat, 0);
	CHECK(ret);

	init_yuv_buffers(m_buffers_c2, m_preview_width, m_preview_height, m_record_v4lformat);
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */

	ret = fimc_v4l2_reqbufs(m_cam_fd2, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
	CHECK(ret);
	ret = fimc_v4l2_querybuf(m_cam_fd2, m_buffers_c2, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
	CHECK(ret);

        /* g_parm, s_parm sample */
        ret = fimc_v4l2_g_parm(m_cam_fd2);
	CHECK(ret);
        ret = fimc_v4l2_s_parm(m_cam_fd2, 1, m_fps);
	CHECK(ret);

	 /* start with all buffers in queue */
        for (int i = 0; i < MAX_BUFFERS; i++)
	{
                ret = fimc_v4l2_qbuf(m_cam_fd2, i);
		CHECK(ret);
	}

	ret = fimc_v4l2_streamon(m_cam_fd2);
	CHECK(ret);

	// It is a delay for a new frame, not to show the previous bigger ugly picture frame.
	ret = fimc_poll(&m_events_c2);
	CHECK(ret);

	m_flag_record_start = 1;  //Kamat check

	return 0;
}

int SecCamera::stopRecord(void)
{
	if(m_flag_record_start == 0)
		return 0;
	
	LOGV("%s()", __FUNCTION__);
	
	close_buffers(m_buffers_c2);
	
	if(m_cam_fd2 <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	int ret = fimc_v4l2_streamoff(m_cam_fd2);

	m_flag_record_start = 0; //Kamat check
	CHECK(ret);

	return 0;
}

unsigned int SecCamera::getRecPhyAddrY(int index)
{
	unsigned int addr_y;

	addr_y = fimc_v4l2_s_ctrl(m_cam_fd2, V4L2_CID_PADDR_Y, index);
	CHECK((int)addr_y);
	return addr_y;
}

unsigned int SecCamera::getRecPhyAddrC(int index)
{
	unsigned int addr_c;

	addr_c = fimc_v4l2_s_ctrl(m_cam_fd2, V4L2_CID_PADDR_CBCR, index);
	CHECK((int)addr_c);
	return addr_c;
}
#endif //DUAL_PORT_RECORDING

unsigned int SecCamera::getPhyAddrY(int index)
{
	unsigned int addr_y;

	addr_y = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_PADDR_Y, index);
	CHECK((int)addr_y);
	return addr_y;
}

unsigned int SecCamera::getPhyAddrC(int index)
{
	unsigned int addr_c;

	addr_c = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_PADDR_CBCR, index);
	CHECK((int)addr_c);
	return addr_c;
}

#ifdef SEND_YUV_RECORD_DATA 
#define PAGE_ALIGN(x)   ((x+0xFFF)&(~0xFFF)) // Set as multiple of 4K
void SecCamera::getYUVBuffers(unsigned char** virYAddr, unsigned char** virCAddr, int index)
{
    	*virYAddr = (unsigned char*)m_buffers_c[index].start;
	//*virCAddr = (unsigned char*)m_buffers_c[index].start + PAGE_ALIGN(m_preview_width * m_preview_height);
	*virCAddr = (unsigned char*)m_buffers_c[index].start + ALIGN_TO_8KB(ALIGN_TO_128B(m_preview_width) * ALIGN_TO_32B(m_preview_height));
}
#endif

void SecCamera::pausePreview()
{
	fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
}

int SecCamera::getPreview()
{
	int index;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	int ret;
#endif

#ifdef PERFORMANCE

	LOG_TIME_DEFINE(0)
	LOG_TIME_DEFINE(1)

	LOG_TIME_START(0)
	fimc_poll(&m_events_c);
	LOG_TIME_END(0)
	LOG_CAMERA("fimc_poll interval: %lu us", LOG_TIME(0));

	LOG_TIME_START(1)
	index = fimc_v4l2_dqbuf(m_cam_fd);
	LOG_TIME_END(1)
	LOG_CAMERA("fimc_dqbuf interval: %lu us", LOG_TIME(1));

#else
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	if(m_flag_camera_start == 0 || fimc_esd_poll(&m_events_c) == 0)
	{
		LOGE("ERR(%s):Start Camera Device Reset \n", __FUNCTION__);
		/* GAUDI Project([arun.c@samsung.com]) 2010.05.20. [Implemented ESD code] */
		/*
		 * When there is no data for more than 1 second from the camera we inform
		 * the FIMC driver by calling fimc_v4l2_s_input() with a special value = 1000
		 * FIMC driver identify that there is something wrong with the camera
		 * and it restarts the sensor.
		 */
		stopPreview();
		/* Reset Only Camera Device */
		ret = fimc_v4l2_querycap(m_cam_fd);
		CHECK(ret);
		ret = fimc_v4l2_enuminput(m_cam_fd, m_camera_id);
		CHECK(ret);
		ret = fimc_v4l2_s_input(m_cam_fd, 1000);
		CHECK(ret);
		//setCameraSensorReset();
		ret = startPreview();

		if(ret < 0) {
			LOGE("ERR(%s): startPreview() return %d\n", __FUNCTION__, ret);

			return 0;
		}
	}
#else	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	fimc_poll(&m_events_c);
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	index = fimc_v4l2_dqbuf(m_cam_fd);
#endif
	if(!(0 <= index && index < MAX_BUFFERS))
	{
		LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
		return -1;
	}

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	ret = fimc_v4l2_qbuf(m_cam_fd, index); //Kamat: is it overhead?
#else	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */
	int ret = fimc_v4l2_qbuf(m_cam_fd, index); //Kamat: is it overhead?
#endif	/* SWP1_CAMERA_ADD_ADVANCED_FUNCTION */

	CHECK(ret);

#ifdef ENABLE_HDMI_DISPLAY 
	hdmi_set_v_param(getPhyAddrY(index), getPhyAddrC (index),m_preview_width,m_preview_height); 
#endif
	return index;

}

#ifdef DUAL_PORT_RECORDING
int SecCamera::getRecord()
{
	int index;

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	if(m_flag_record_start == 0) {
		LOGE("%s: m_flag_record_start is 0", __FUNCTION__);
		startRecord();
	}
#endif

#ifdef PERFORMANCE

	LOG_TIME_DEFINE(0)
	LOG_TIME_DEFINE(1)

	LOG_TIME_START(0)
	fimc_poll(&m_events_c2);
	LOG_TIME_END(0)
	LOG_CAMERA("fimc_poll interval: %lu us", LOG_TIME(0));

	LOG_TIME_START(1)
	index = fimc_v4l2_dqbuf(m_cam_fd2);
	LOG_TIME_END(1)
	LOG_CAMERA("fimc_dqbuf interval: %lu us", LOG_TIME(1));

#else
	fimc_poll(&m_events_c2);
	index = fimc_v4l2_dqbuf(m_cam_fd2);
#endif
	if(!(0 <= index && index < MAX_BUFFERS))
	{
		LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
		return -1;
	}

	int ret = fimc_v4l2_qbuf(m_cam_fd2, index); //Kamat: is it overhead?
	CHECK(ret);

	return index;
}
#endif //DUAL_PORT_RECORDING

int SecCamera::setPreviewSize(int width, int height, int pixel_format)
{
	LOGV("%s(width(%d), height(%d), format(%d))", __FUNCTION__, width, height, pixel_format);

	int v4lpixelformat = pixel_format;

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if(v4lpixelformat == V4L2_PIX_FMT_YUV420) { LOGV("PreviewFormat:V4L2_PIX_FMT_YUV420"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_NV12) { LOGV("PreviewFormat:V4L2_PIX_FMT_NV12"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_NV12T) { LOGV("PreviewFormat:V4L2_PIX_FMT_NV12T"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_NV21) { LOGV("PreviewFormat:V4L2_PIX_FMT_NV21"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_YUV422P) { LOGV("PreviewFormat:V4L2_PIX_FMT_YUV422P"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_YUYV) { LOGV("PreviewFormat:V4L2_PIX_FMT_YUYV"); }
	else if(v4lpixelformat == V4L2_PIX_FMT_RGB565) { LOGV("PreviewFormat:V4L2_PIX_FMT_RGB565"); }
	else { LOGV("PreviewFormat:UnknownFormat"); }
#endif
	m_preview_width  = width;
	m_preview_height = height;
	m_preview_v4lformat = v4lpixelformat;

	return 0;
}

int SecCamera::getPreviewSize(int * width, int * height, int * frame_size)
{
	*width	= m_preview_width;
	*height = m_preview_height;
	*frame_size = m_frameSize(m_preview_v4lformat, m_preview_width, m_preview_height);

	return 0;
}

int SecCamera::getPreviewMaxSize(int * width, int * height)
{
	*width	= m_preview_max_width;
	*height = m_preview_max_height;

	return 0;
}

int SecCamera::getPreviewPixelFormat(void)
{
	return m_preview_v4lformat;
}


// ======================================================================
// Snapshot
#ifdef JPEG_FROM_SENSOR
/*
 * Devide getJpeg() as two funcs, setSnapshotCmd() & getJpeg() because of the shutter sound timing.
 * Here, just send the capture cmd to camera ISP to start JPEG capture.
 */
int SecCamera::setSnapshotCmd(void)
{
	LOGV("%s()", __FUNCTION__);
	
	int ret = 0;

	LOG_TIME_DEFINE(0)
	LOG_TIME_DEFINE(1)

	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return 0;		
	}

	if(m_flag_camera_start > 0)
	{
	LOG_TIME_START(0)
		stopPreview();
	LOG_TIME_END(0)
	}

	memset(&m_events_c, 0, sizeof(m_events_c));
	m_events_c.fd = m_cam_fd;
	m_events_c.events = POLLIN | POLLERR;

	LOG_TIME_START(1) // prepare
	int nframe = 1;

        ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
	CHECK_PTR(ret);
        ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, V4L2_PIX_FMT_JPEG);
	CHECK_PTR(ret);
        init_yuv_buffers(m_buffers_c, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);

        ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
	CHECK_PTR(ret);
        ret = fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
	CHECK_PTR(ret);

        ret = fimc_v4l2_g_parm(m_cam_fd);
	CHECK_PTR(ret);
        ret = fimc_v4l2_s_parm(m_cam_fd, 1, m_fps);
	CHECK_PTR(ret);

	ret = fimc_v4l2_qbuf(m_cam_fd, 0);
	CHECK_PTR(ret);

	ret = fimc_v4l2_streamon(m_cam_fd);
	CHECK_PTR(ret);
	LOG_TIME_END(1)

	return 0;
}

/*
 * Set Jpeg quality & exif info and get JPEG data from camera ISP
 */
unsigned char* SecCamera::getJpeg(int* jpeg_size, unsigned int* phyaddr)
{
	LOGV("%s()", __FUNCTION__);
	
	int index, ret = 0;
	unsigned char* addr;

	LOG_TIME_DEFINE(2)
/* kidggang (10.7.5) - Problem - After jpegquality is capture, operation setting is normal.

	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_QUALITY, m_jpeg_quality);
	CHECK_PTR(ret);
*/

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	//exif orient info
//	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_EXIF_ORIENTATION, m_exif_orientation); //kidggang
//	CHECK_PTR(ret);
#if  0//def SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	//set gps information
	ret = fimc_v4l2_s_ext_ctrl(m_cam_fd, V4L2_CID_CAMERA_GPS_LATITUDE, &m_gps_latitude);
	CHECK_PTR(ret);
	ret = fimc_v4l2_s_ext_ctrl(m_cam_fd, V4L2_CID_CAMERA_GPS_LONGITUDE, &m_gps_longitude);
	CHECK_PTR(ret);
	ret = fimc_v4l2_s_ext_ctrl(m_cam_fd, V4L2_CID_CAMERA_GPS_ALTITUDE, &m_gps_altitude);
	CHECK_PTR(ret);
	ret = fimc_v4l2_s_ext_ctrl(m_cam_fd, V4L2_CID_CAMERA_GPS_TIMESTAMP, &m_gps_timestamp);
	CHECK_PTR(ret);
#endif

/* kidggang
	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CAPTURE, 0);
	CHECK_PTR(ret);*/

	// capture
	ret = fimc_poll(&m_events_c);
	CHECK_PTR(ret);
	index = fimc_v4l2_dqbuf(m_cam_fd);
	if(!(0 <= index && index < MAX_BUFFERS))
	{
		LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
		return NULL;
	}
#endif	

	*jpeg_size = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_SIZE);
	CHECK_PTR(*jpeg_size);
      	int main_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_OFFSET);
	CHECK_PTR(main_offset);
        m_postview_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET);
	CHECK_PTR(m_postview_offset);
	
	ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
	CHECK_PTR(ret);
	LOGV("\nsnapshot dqueued buffer = %d snapshot_width = %d snapshot_height = %d\n\n",index, m_snapshot_width, m_snapshot_height);

	addr = (unsigned char*)(m_buffers_c[index].start) + main_offset;
	*phyaddr = getPhyAddrY(index) + m_postview_offset;

	LOG_TIME_START(2) // post
	ret = fimc_v4l2_streamoff(m_cam_fd);
	CHECK_PTR(ret);
	LOG_TIME_END(2)
		
#if 0 //temporary blocked for build
	LOG_CAMERA("getSnapshotAndJpeg intervals : stopPreview(%lu), prepare(%lu), capture(%lu), memcpy(%lu), yuv2Jpeg(%lu), post(%lu)  us"
	, LOG_TIME(0), LOG_TIME(1), LOG_TIME(2), LOG_TIME(3), LOG_TIME(4), LOG_TIME(5));
#endif
	return addr;
}

int SecCamera::getExif(unsigned char *pExifDst, unsigned char *pThumbSrc)
{
    return 0;
}

void SecCamera::getPostViewConfig(int* width, int* height, int* size)
{
	if(m_preview_width == 1024)
	{
		*width = BACK_CAMERA_POSTVIEW_WIDE_WIDTH;
		*height = BACK_CAMERA_POSTVIEW_HEIGHT;
		*size = BACK_CAMERA_POSTVIEW_WIDE_WIDTH * BACK_CAMERA_POSTVIEW_HEIGHT * BACK_CAMERA_POSTVIEW_BPP/8; 

	}
	else
	{
		*width = BACK_CAMERA_POSTVIEW_WIDTH;
		*height = BACK_CAMERA_POSTVIEW_HEIGHT;
		*size = BACK_CAMERA_POSTVIEW_WIDTH * BACK_CAMERA_POSTVIEW_HEIGHT * BACK_CAMERA_POSTVIEW_BPP/8; 		
	}
	LOGV("[5B] m_preview_width : %d, mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d", m_preview_width, *width, *height, *size);	
}

#ifdef DIRECT_DELIVERY_OF_POSTVIEW_DATA
int SecCamera::getPostViewOffset(void)
{
	return m_postview_offset;
}
#endif

#else //#ifdef JPEG_FROM_SENSOR
int SecCamera::getJpegFd(void)
{
	return m_jpeg_fd;
}

void SecCamera::SetJpgAddr(unsigned char *addr)
{
	SetMapAddr(addr);
}

#if 0
int SecCamera::getSnapshot(unsigned char * buffer, unsigned int buffer_size)
{
	LOGV("%s(buffer(%p), size(%d))", __FUNCTION__, buffer, buffer_size);
	
	if(getSnapshotAndJpeg(buffer, buffer_size, NULL, NULL) == 0)
		return -1;
		
	return 0;
}
#endif

#endif

unsigned char*  SecCamera::getSnapshotAndJpeg(unsigned int* output_size)
{ 
	LOGV("%s()", __FUNCTION__);
	
	int index;
	//unsigned int addr;
	unsigned char* addr;
	int ret = 0;

	LOG_TIME_DEFINE(0)
	LOG_TIME_DEFINE(1)
	LOG_TIME_DEFINE(2)
	LOG_TIME_DEFINE(3)
	LOG_TIME_DEFINE(4)
	LOG_TIME_DEFINE(5)
	
	//fimc_v4l2_streamoff(m_cam_fd); [zzangdol] remove - it is separate in HWInterface with camera_id

	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return 0;		
	}

	if(m_flag_camera_start > 0)
	{
	LOG_TIME_START(0)
		stopPreview();
	LOG_TIME_END(0)
	}

	memset(&m_events_c, 0, sizeof(m_events_c));
	m_events_c.fd = m_cam_fd;
	m_events_c.events = POLLIN | POLLERR;

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420) { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV420"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12) { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV12"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T) { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV12T"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV21) { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV21"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P) { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV422P"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV) { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUYV"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY) { LOGV("SnapshotFormat:V4L2_PIX_FMT_UYVY"); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565) { LOGV("SnapshotFormat:V4L2_PIX_FMT_RGB565"); }
	else { LOGV("SnapshotFormat:UnknownFormat"); }
#endif

	LOG_TIME_START(1) // prepare
	int nframe = 1;

	LOGE("[zzangdol] w %d, h %d\n", m_snapshot_width, m_snapshot_height);

        ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
	CHECK_PTR(ret);
        ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);
	CHECK_PTR(ret);
        init_yuv_buffers(m_buffers_c, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);

        ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
	CHECK_PTR(ret);
        ret = fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
	CHECK_PTR(ret);

        /* g_parm, s_parm sample */
        ret = fimc_v4l2_g_parm(m_cam_fd);
	CHECK_PTR(ret);
        ret = fimc_v4l2_s_parm(m_cam_fd, 1, m_fps);
	CHECK_PTR(ret);

	ret = fimc_v4l2_qbuf(m_cam_fd, 0);
	CHECK_PTR(ret);

	ret = fimc_v4l2_streamon(m_cam_fd);//zzangdolp
	CHECK_PTR(ret);
	LOG_TIME_END(1)

	LOG_TIME_START(2) // capture
	fimc_poll(&m_events_c);
	index = fimc_v4l2_dqbuf(m_cam_fd);
	fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
	LOGV("\nsnapshot dqueued buffer = %d snapshot_width = %d snapshot_height = %d\n\n",index, m_snapshot_width, m_snapshot_height);

#ifdef DUMP_YUV
	save_yuv(m_buffers_c, m_snapshot_width, m_snapshot_height, 16, index, 0);
#endif
	LOG_TIME_END(2)

	//addr = getPhyAddrY(index);
	addr = (unsigned char*)m_buffers_c[index].start;
	if(addr == 0)
	{
		LOGE("%s] Physical address 0");
	}
	LOG_TIME_START(5) // post
	fimc_v4l2_streamoff(m_cam_fd);
#ifdef DUMP_YUV
	close_buffers(m_buffers_c);
#endif
	LOG_TIME_END(5)

	LOG_CAMERA("getSnapshotAndJpeg intervals : stopPreview(%lu), prepare(%lu), capture(%lu), memcpy(%lu), yuv2Jpeg(%lu), post(%lu)  us"
	, LOG_TIME(0), LOG_TIME(1), LOG_TIME(2), LOG_TIME(3), LOG_TIME(4), LOG_TIME(5));

	return addr;
}


int SecCamera::setSnapshotSize(int width, int height)
{
	LOGV("%s(width(%d), height(%d))", __FUNCTION__, width, height);
	
	m_snapshot_width  = width;
	m_snapshot_height = height;

	return 0;
}

int SecCamera::getSnapshotSize(int * width, int * height, int * frame_size)
{
	*width	= m_snapshot_width;
	*height = m_snapshot_height;

	int frame = 0;

	frame = m_frameSize(m_snapshot_v4lformat, m_snapshot_width, m_snapshot_height);
	
	// set it big.
	if(frame == 0)
		frame = m_snapshot_width * m_snapshot_height * BPP;
	
	*frame_size = frame;

	return 0;
}

int SecCamera::getSnapshotMaxSize(int * width, int * height)
{
	switch(m_camera_id)
	{
		case CAMERA_ID_FRONT:
			m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
			m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;	
			break;

		default:
		case CAMERA_ID_BACK:
			m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
			m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;	
			break;
	}

	*width	= m_snapshot_max_width;
	*height = m_snapshot_max_height;

	return 0;
}

int SecCamera::setSnapshotPixelFormat(int pixel_format)
{
	int v4lpixelformat= pixel_format;

	if(m_snapshot_v4lformat != v4lpixelformat)
	{
		m_snapshot_v4lformat = v4lpixelformat;
	}
	
	
#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_YUV420", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_NV12", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_NV12T", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV21) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_NV21", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_YUV422P", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_YUYV", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_UYVY", __FUNCTION__); }
	else if(m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565) { LOGE("%s():SnapshotFormat:V4L2_PIX_FMT_RGB565", __FUNCTION__); }
	else { LOGE("SnapshotFormat:UnknownFormat"); }
#endif

	return 0;
}

int SecCamera::getSnapshotPixelFormat(void)
{
	return m_snapshot_v4lformat;
}


// ======================================================================
// Settings

int SecCamera::setCameraId(int camera_id)
{
        if(   camera_id != CAMERA_ID_FRONT
           && camera_id != CAMERA_ID_BACK)
        {       
                LOGE("ERR(%s)::Invalid camera id(%d)\n", __func__, camera_id);
                return -1;
        }
	if(m_camera_id == camera_id)
		return 0;

	LOGV("%s(camera_id(%d))", __FUNCTION__, camera_id);
	
	switch(camera_id)
	{
		case CAMERA_ID_FRONT:

			m_preview_max_width   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
			m_preview_max_height  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
			m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
			m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
			break;

		case CAMERA_ID_BACK:
			m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
			m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
			m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
			m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
			break;
	}

	m_camera_id = camera_id;
	
	resetCamera();

	return 0;
}

int SecCamera::getCameraId(void)
{
	return m_camera_id;
}

// -----------------------------------

int SecCamera::setAutofocus(void)
{
	LOGV("%s()", __FUNCTION__);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}
	
	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_ON) < 0) 
	{
	        LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __FUNCTION__);
		return -1;
	}
#else
	// kcoolsw : turn on setAutofocus initially..
	if(m_autofocus != AUTO_FOCUS_ON)
	{
		m_autofocus = AUTO_FOCUS_ON;
	}	
#endif

	return 0;
} 

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
int SecCamera::getAutoFocusResult(void)
{
	int af_result = 0;
	af_result = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAMERA_AUTO_FOCUS_RESULT);
	return af_result;
}
int SecCamera::cancelAutofocus(void)
{
	LOGV("%s()", __FUNCTION__);

	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}	
	
	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_OFF) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __FUNCTION__);
		return -1;
	}	
	
	usleep(1000);

	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_STATUS) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __FUNCTION__);
		return -1;
	}	
	return 0;
} 
#endif
// -----------------------------------

int SecCamera::zoomIn(void)
{
	LOGV("%s()", __FUNCTION__);
	return 0;
}

int SecCamera::zoomOut(void)
{
	LOGV("%s()", __FUNCTION__);
	return 0;
}

// -----------------------------------

int SecCamera::SetRotate(int angle)
{
	LOGE("%s(angle(%d))", __FUNCTION__, angle);	
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	if(m_angle != angle)
	{
	switch(angle)
	{
		case -360 :
		case	0 :
		case  360 :
			m_angle = 0;
			break;
					
		case -270 :
		case   90 :
			m_angle = 90;
			break;
				
		case -180 :
		case  180 :
			m_angle = 180;
			break;
				
		case  -90 :
		case  270 :
			m_angle = 270;
			break;
			
		default : 
			LOGE("ERR(%s):Invalid angle(%d)", __FUNCTION__, angle);
			return -1;
	}


		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_ROTATION, angle) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_ROTATION", __FUNCTION__);
				return -1;
			}
		}		
	}
#endif 
	return 0;
}

int SecCamera::getRotate(void)
{
	LOGV("%s():angle(%d)", __FUNCTION__, m_angle);
	return m_angle;
}

void SecCamera::setFrameRate(int frame_rate)
{
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	LOGV("%s(FrameRate(%d))", __FUNCTION__, frame_rate);
	
	if(frame_rate < FRAME_RATE_AUTO || FRAME_RATE_MAX < frame_rate )
		LOGE("ERR(%s):Invalid frame_rate(%d)", __FUNCTION__, frame_rate);

	if(m_fps != frame_rate)
	{
		m_fps = frame_rate;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FRAME_RATE, frame_rate) < 0) 
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FRAME_RATE", __FUNCTION__);
		}
	}
#else
	m_fps = frame_rate;	
#endif
	
}

/* kidggang (10.7.5) - Problem - After jpegquality is capture, operation setting is normal.
void SecCamera::setJpegQuality(int quality)
{
	m_jpeg_quality = quality;
}
*/
// -----------------------------------

int SecCamera::setVerticalMirror(void)
{
	LOGV("%s()", __FUNCTION__);
	
	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_VFLIP, 0) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_VFLIP", __FUNCTION__);
		return -1;
	}

	return 0;
}

int SecCamera::setHorizontalMirror(void)
{
	LOGV("%s()", __FUNCTION__);
	
	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return -1;
	}

	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_HFLIP, 0) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_HFLIP", __FUNCTION__);
		return -1;
	}

	return 0;
}

// -----------------------------------

int SecCamera::setWhiteBalance(int white_balance)
{
	LOGV("%s(white_balance(%d))", __FUNCTION__, white_balance);

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION	
	if(white_balance <= WHITE_BALANCE_BASE || WHITE_BALANCE_MAX <= white_balance)
#else
	if(white_balance < WHITE_BALANCE_AUTO || WHITE_BALANCE_SUNNY < white_balance)
#endif
	{
		LOGE("ERR(%s):Invalid white_balance(%d)", __FUNCTION__, white_balance);
		return -1;
	}

	if(m_white_balance != white_balance)
	{
		m_white_balance = white_balance;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_WHITE_BALANCE, white_balance) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_WHITE_BALANCE", __FUNCTION__);
				return -1;
			}
		}
#endif
	}

	return 0;
}

int SecCamera::getWhiteBalance(void)
{
	LOGV("%s():white_balance(%d)", __FUNCTION__, m_white_balance);
	return m_white_balance;
}

// -----------------------------------

int SecCamera::setBrightness(int brightness)
{	
	LOGV("%s(brightness(%d))", __FUNCTION__, brightness);
	
	if(brightness < BRIGHTNESS_MINUS_4|| BRIGHTNESS_PLUS_4< brightness )
	{
		LOGE("ERR(%s):Invalid brightness(%d)", __FUNCTION__, brightness);
		return -1;
	}

	if(m_brightness != brightness)
	{
		m_brightness = brightness;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BRIGHTNESS, brightness + BRIGHTNESS_NORMAL) < 0)
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_BRIGHTNESS", __FUNCTION__);
				return -1;
			}
		}
#endif
	}

	return 0;
}

int SecCamera::getBrightness(void)
{
	LOGV("%s():brightness(%d)", __FUNCTION__, m_brightness);
	return m_brightness;
}

// -----------------------------------

int SecCamera::setImageEffect(int image_effect)
{
	LOGV("%s(image_effect(%d))", __FUNCTION__, image_effect);
	
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
	if(image_effect <= IMAGE_EFFECT_BASE || IMAGE_EFFECT_MAX <= image_effect)
#else
	if(image_effect < IMAGE_EFFECT_ORIGINAL || IMAGE_EFFECT_SILHOUETTE < image_effect)
#endif
	{
		LOGE("ERR(%s):Invalid image_effect(%d)", __FUNCTION__, image_effect);
		return -1;
	}
	
	if(m_image_effect != image_effect)
	{
		m_image_effect = image_effect;
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_EFFECT, image_effect) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_EFFECT", __FUNCTION__);
				return -1;
			}
		}
#endif
	}

	return 0;
}

int SecCamera::getImageEffect(void)
{
	LOGV("%s():image_effect(%d)", __FUNCTION__, m_image_effect);
	return m_image_effect;
}

// ======================================================================
#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
int SecCamera::setAntiBanding(int anti_banding)
{
	LOGV("%s(anti_banding(%d))", __FUNCTION__, anti_banding);
	
	if(anti_banding < ANTI_BANDING_AUTO|| ANTI_BANDING_OFF < anti_banding)
	{
		LOGE("ERR(%s):Invalid anti_banding (%d)", __FUNCTION__, anti_banding);
		return -1;
	}
	
	if(m_anti_banding != anti_banding)
	{
		m_anti_banding = anti_banding;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ANTI_BANDING, anti_banding) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ANTI_BANDING", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

//======================================================================
int SecCamera::setSceneMode(int scene_mode)
{
	LOGV("%s(scene_mode(%d))", __FUNCTION__, scene_mode);
	
	if(scene_mode <= SCENE_MODE_BASE || SCENE_MODE_MAX <= scene_mode)
	{
		LOGE("ERR(%s):Invalid scene_mode (%d)", __FUNCTION__, scene_mode);
		return -1;
	}
	
	if(m_scene_mode != scene_mode)
	{
		m_scene_mode = scene_mode;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SCENE_MODE, m_scene_mode) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SCENE_MODE", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getSceneMode(void)
{
	return m_scene_mode;
}

//======================================================================

int SecCamera::setFlashMode(int flash_mode)
{
	LOGV("%s(flash_mode(%d))", __FUNCTION__, flash_mode);
	
	if(flash_mode <= FLASH_MODE_BASE || FLASH_MODE_MAX <= flash_mode)
	{
		LOGE("ERR(%s):Invalid flash_mode (%d)", __FUNCTION__, flash_mode);
		return -1;
	}
	
	if(m_flash_mode != flash_mode)
	{
		m_flash_mode = flash_mode;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FLASH_MODE, flash_mode) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FLASH_MODE", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getFlashMode(void)
{
	return m_flash_mode;
}

//======================================================================

int SecCamera::setISO(int iso_value)
{
	LOGV("%s(iso_value(%d))", __FUNCTION__, iso_value);
	if(iso_value <ISO_AUTO || ISO_MAX <= iso_value)
	{
		LOGE("ERR(%s):Invalid iso_value (%d)", __FUNCTION__, iso_value);
		return -1;
	}
	
	if(m_iso != iso_value)
	{
		m_iso = iso_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ISO, iso_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ISO", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getISO(void)
{
	return m_iso;
}

//======================================================================

int SecCamera::setContrast(int contrast_value)
{
	LOGV("%s(contrast_value(%d))", __FUNCTION__, contrast_value);
	
	if(contrast_value <CONTRAST_MINUS_2|| CONTRAST_MAX<= contrast_value)
	{
		LOGE("ERR(%s):Invalid contrast_value (%d)", __FUNCTION__, contrast_value);
		return -1;
	}
	
	if(m_contrast != contrast_value)
	{
		m_contrast = contrast_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CONTRAST, contrast_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_CONTRAST", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getContrast(void)
{
	return m_contrast;
}

//======================================================================

int SecCamera::setSaturation(int saturation_value)
{
	LOGV("%s(saturation_value(%d))", __FUNCTION__, saturation_value);
	
	if(saturation_value <SATURATION_MINUS_2|| SATURATION_MAX<= saturation_value)
	{
		LOGE("ERR(%s):Invalid saturation_value (%d)", __FUNCTION__, saturation_value);
		return -1;
	}
	
	if(m_saturation!= saturation_value)
	{
		m_saturation = saturation_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SATURATION, saturation_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SATURATION", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getSaturation(void)
{
	return m_saturation;
}

//======================================================================

int SecCamera::setSharpness(int sharpness_value)
{
	LOGV("%s(sharpness_value(%d))", __FUNCTION__, sharpness_value);
	
	if(sharpness_value <SHARPNESS_MINUS_2|| SHARPNESS_MAX<= sharpness_value)
	{
		LOGE("ERR(%s):Invalid sharpness_value (%d)", __FUNCTION__, sharpness_value);
		return -1;
	}
	
	if(m_sharpness!= sharpness_value)
	{
		m_sharpness = sharpness_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SHARPNESS, sharpness_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SHARPNESS", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getSharpness(void)
{
	return m_sharpness;
}

//======================================================================

int SecCamera::setWDR(int wdr_value)
{
	LOGV("%s(wdr_value(%d))", __FUNCTION__, wdr_value);
	
	if(wdr_value<WDR_OFF || WDR_MAX<= wdr_value)
	{
		LOGE("ERR(%s):Invalid wdr_value (%d)", __FUNCTION__, wdr_value);
		return -1;
	}
	
	if(m_wdr!= wdr_value)
	{
		m_wdr = wdr_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_WDR, wdr_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_WDR", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getWDR(void)
{
	return m_wdr;
}

//======================================================================

int SecCamera::setAntiShake(int anti_shake)
{
	LOGV("%s(anti_shake(%d))", __FUNCTION__, anti_shake);
	
	if(anti_shake<ANTI_SHAKE_OFF || ANTI_SHAKE_MAX<= anti_shake)
	{
		LOGE("ERR(%s):Invalid anti_shake (%d)", __FUNCTION__, anti_shake);
		return -1;
	}
	
	if(m_anti_shake!= anti_shake)
	{
		m_anti_shake = anti_shake;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ANTI_SHAKE, anti_shake) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ANTI_SHAKE", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getAntiShake(void)
{
	return m_anti_shake;
}

//======================================================================


int SecCamera::setMetering(int metering_value)
{
	LOGV("%s(metering (%d))", __FUNCTION__, metering_value);
	
	if(metering_value <= METERING_BASE || METERING_MAX <= metering_value)
	{
		LOGE("ERR(%s):Invalid metering_value (%d)", __FUNCTION__, metering_value);
		return -1;
	}
	
	if(m_metering != metering_value)
	{
		m_metering = metering_value;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_METERING, metering_value) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_METERING", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getMetering(void)
{
	return m_metering;
}

//======================================================================

int SecCamera::setJpegQuality(int jpeg_quality)
{
	LOGV("%s(jpeg_quality (%d))", __FUNCTION__, jpeg_quality);
	
	if(jpeg_quality < JPEG_QUALITY_ECONOMY|| JPEG_QUALITY_MAX<= jpeg_quality)
	{
		LOGE("ERR(%s):Invalid jpeg_quality (%d)", __FUNCTION__, jpeg_quality);
		return -1;
	}
	
	if(m_jpeg_quality != jpeg_quality)
	{
		m_jpeg_quality = jpeg_quality;
		if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_QUALITY, jpeg_quality) < 0) 
		{
			LOGE("ERR(%s):Fail on V4L2_CID_CAM_JPEG_QUALITY", __FUNCTION__);
			return -1;
		}
	}

	return 0;
}

int	SecCamera::getJpegQuality(void)
{
	return m_jpeg_quality;
}

//======================================================================

int SecCamera::setZoom(int zoom_level)
{
	LOGV("%s(zoom_level (%d))", __FUNCTION__, zoom_level);
	
	if(zoom_level < ZOOM_LEVEL_0|| ZOOM_LEVEL_MAX<= zoom_level)
	{
		LOGE("ERR(%s):Invalid zoom_level (%d)", __FUNCTION__, zoom_level);
		return -1;
	}
	
	if(m_zoom_level != zoom_level)
	{
		m_zoom_level = zoom_level;

		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_ZOOM, zoom_level) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ZOOM", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getZoom(void)
{
	return m_zoom_level;
}

//======================================================================

int SecCamera::setObjectTracking(int object_tracking)
{
	LOGV("%s(object_tracking (%d))", __FUNCTION__, object_tracking);
	
	if(object_tracking < OBJECT_TRACKING_OFF|| OBJECT_TRACKING_MAX<= object_tracking)
	{
		LOGE("ERR(%s):Invalid object_tracking (%d)", __FUNCTION__, object_tracking);
		return -1;
	}
	
	if(m_object_tracking != object_tracking)
	{
		m_object_tracking = object_tracking;
	}

	return 0;
}

int	SecCamera::getObjectTracking(void)
{
	return m_object_tracking;
}

int SecCamera::getObjectTrackingStatus(void)
{
	int obj_status = 0;
	obj_status = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAMERA_OBJ_TRACKING_STATUS);
	return obj_status;
}

int SecCamera::setObjectTrackingStartStop(int start_stop)
{
	LOGV("%s(object_tracking_start_stop (%d))", __FUNCTION__, start_stop);
	
	if(m_object_tracking_start_stop != start_stop)
	{
		m_object_tracking_start_stop = start_stop;
		if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_OBJ_TRACKING_START_STOP, start_stop) < 0) 
		{
			LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_OBJ_TRACKING_START_STOP", __FUNCTION__);
			return -1;
		}
	}

	return 0;
}

int SecCamera::setTouchAFStartStop(int start_stop)
{
	LOGV("%s(touch_af_start_stop (%d))", __FUNCTION__, start_stop);
	
	if(m_touch_af_start_stop != start_stop)
	{
		m_touch_af_start_stop = start_stop;
		if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_TOUCH_AF_START_STOP, start_stop) < 0) 
{
			LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_TOUCH_AF_START_STOP", __FUNCTION__);
			return -1;
		}
	}

	return 0;
}

//======================================================================

int SecCamera::setSmartAuto(int smart_auto)
{
	LOGV("%s(smart_auto (%d))", __FUNCTION__, smart_auto);
	
	if(smart_auto < SMART_AUTO_OFF|| SMART_AUTO_MAX <= smart_auto)
	{
		LOGE("ERR(%s):Invalid smart_auto (%d)", __FUNCTION__, smart_auto);
		return -1;
	}
	
	if(m_smart_auto != smart_auto)
	{
		m_smart_auto = smart_auto;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SMART_AUTO, smart_auto) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SMART_AUTO", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getSmartAuto(void)
{
	return m_smart_auto;
}

int	SecCamera::getAutosceneStatus(void)
{
	int autoscene_status = -1;
	
	if(getSmartAuto() == SMART_AUTO_ON)
	{
		autoscene_status = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAMERA_SMART_AUTO_STATUS);
		
		if((autoscene_status < SMART_AUTO_STATUS_AUTO) || (autoscene_status > SMART_AUTO_STATUS_MAX))
		{
			LOGE("ERR(%s):Invalid getAutosceneStatus (%d)", __FUNCTION__, autoscene_status);
			return -1;
		}
	}
	//LOGV("%s()    autoscene_status (%d)", __FUNCTION__, autoscene_status);
	return autoscene_status;
}
//======================================================================

int SecCamera::setBeautyShot(int beauty_shot)
{
	LOGV("%s(beauty_shot (%d))", __FUNCTION__, beauty_shot);
	
	if(beauty_shot < BEAUTY_SHOT_OFF|| BEAUTY_SHOT_MAX <= beauty_shot)
	{
		LOGE("ERR(%s):Invalid beauty_shot (%d)", __FUNCTION__, beauty_shot);
		return -1;
	}
	
	if(m_beauty_shot != beauty_shot)
	{
		m_beauty_shot = beauty_shot;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BEAUTY_SHOT, beauty_shot) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_BEAUTY_SHOT", __FUNCTION__);
				return -1;
			}
			
			setFaceDetect(FACE_DETECT_BEAUTY_ON);
		}
	}

	return 0;
}

int	SecCamera::getBeautyShot(void)
{
	return m_beauty_shot;
}

//======================================================================

int SecCamera::setVintageMode(int vintage_mode)
{
	LOGV("%s(vintage_mode(%d))", __FUNCTION__, vintage_mode);
	
	if(vintage_mode <= VINTAGE_MODE_BASE || VINTAGE_MODE_MAX <= vintage_mode)
	{
		LOGE("ERR(%s):Invalid vintage_mode (%d)", __FUNCTION__, vintage_mode);
		return -1;
	}
	
	if(m_vintage_mode != vintage_mode)
	{
		m_vintage_mode = vintage_mode;
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_VINTAGE_MODE, vintage_mode) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_VINTAGE_MODE", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

int	SecCamera::getVintageMode(void)
{
	return m_vintage_mode;
}

//======================================================================

int SecCamera::setFocusMode(int focus_mode)
{
	LOGV("%s(focus_mode(%d))", __FUNCTION__, focus_mode);
	
	if(FOCUS_MODE_MAX <= focus_mode)
	{
		LOGE("ERR(%s):Invalid focus_mode (%d)", __FUNCTION__, focus_mode);
		return -1;
	}
	
	if(m_focus_mode != focus_mode)
	{
		m_focus_mode = focus_mode;
		
		if (m_focus_mode != FOCUS_MODE_FACEDETECT)
		{
			m_face_detect = FACE_DETECT_OFF;
			if(m_flag_camera_start)
			{
//				setFaceDetect(m_face_detect);
				
				if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, focus_mode) < 0) 
				{
					LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FOCUS_MODE", __FUNCTION__);
					return -1;
				}
			}
		}
		else
		{
			m_face_detect = FACE_DETECT_NORMAL_ON;
			if(m_flag_camera_start)
			{
				if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, FOCUS_MODE_AUTO) < 0) 
				{
					LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FOCUS_MODE", __FUNCTION__);
					return -1;
				}
							
//				setFaceDetect(m_face_detect);
			}
		}
	}

	return 0;
}

int	SecCamera::getFocusMode(void)
{
	return m_focus_mode;
}

//======================================================================

 int	SecCamera::setFaceDetect(int face_detect)
 {
	LOGV("%s(face_detect(%d))", __FUNCTION__, face_detect);
	
//	if(m_face_detect != face_detect)
	{
		m_face_detect = face_detect;
		if(m_flag_camera_start)
		{
		 	if(m_face_detect != FACE_DETECT_OFF)
		 	{
				if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FOCUS_MODE, FOCUS_MODE_AUTO) < 0) 
				{
					LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FOCUS_MODin face detecion", __FUNCTION__);
					return -1;
				}
			}
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FACE_DETECTION, face_detect) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FACE_DETECTION", __FUNCTION__);
				return -1;
			}
		}
	}

	return 0;
}

 int	SecCamera::getFaceDetect(void)
 {
 	return m_face_detect;
 }

 //====================================================================== 
 
 int	SecCamera::setGPSLatitude(const char * gps_latitude)
 {
 	double conveted_latitude = 0;
	LOGV("%s(gps_latitude(%s))", __FUNCTION__, gps_latitude);
	if(gps_latitude == NULL)
		m_gps_latitude = 0;
	else
	{
		conveted_latitude = atof(gps_latitude);
		m_gps_latitude = (long)(conveted_latitude *10000 /1);
	}
	
	LOGV("%s(m_gps_latitude(%ld))", __FUNCTION__, m_gps_latitude);
	return 0;
 }
 int	SecCamera::setGPSLongitude(const char * gps_longitude)
 {
 	double conveted_longitude = 0;
 	LOGV("%s(gps_longitude(%s))", __FUNCTION__, gps_longitude);
	if(gps_longitude == NULL)
		m_gps_longitude = 0;
	else
	{
		conveted_longitude = atof(gps_longitude);
		m_gps_longitude = (long)(conveted_longitude *10000 /1);
	}

	LOGV("%s(m_gps_longitude(%ld))", __FUNCTION__, m_gps_longitude);
	return 0;
 }
 int	SecCamera::setGPSAltitude(const char * gps_altitude)
 {
 	double conveted_altitude = 0;
 	LOGV("%s(gps_altitude(%s))", __FUNCTION__, gps_altitude);
	if(gps_altitude == NULL)
		m_gps_altitude = 0;
	else
	{
		conveted_altitude = atof(gps_altitude);
		m_gps_altitude = (long)(conveted_altitude *100 /1);
	}

	LOGV("%s(m_gps_altitude(%ld))", __FUNCTION__, m_gps_altitude);
	return 0;
 }
 int	SecCamera::setGPSTimeStamp(const char * gps_timestamp)
 {
 	LOGV("%s(gps_timestamp(%s))", __FUNCTION__, gps_timestamp);
	if(gps_timestamp == NULL)
		m_gps_timestamp = 0;
	else
		m_gps_timestamp = atol(gps_timestamp);

	LOGV("%s(m_gps_timestamp(%ld))", __FUNCTION__, m_gps_timestamp);
	return 0;
 }

 //======================================================================
 int	SecCamera::setAEAWBLockUnlock(int ae_lockunlock, int awb_lockunlock)
 {
 	LOGV("%s(ae_lockunlock(%d) , (awb_lockunlock(%d))", __FUNCTION__, ae_lockunlock, awb_lockunlock);
	int ae_awb_status = 1;
#if 0
	if(ae_lockunlock == 0 && awb_lockunlock ==0)
		ae_awb_status = AE_UNLOCK_AWB_UNLOCK;
	else if (ae_lockunlock == 1 && awb_lockunlock ==0)
		ae_awb_status = AE_LOCK_AWB_UNLOCK;
	else if (ae_lockunlock == 1 && awb_lockunlock ==0)
		ae_awb_status = AE_UNLOCK_AWB_LOCK;
	else
		ae_awb_status = AE_LOCK_AWB_LOCK;	
#endif	
	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK, ae_awb_status) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_AE_AWB_LOCKUNLOCK", __FUNCTION__);
		return -1;
	}

	return 0;
 }

 int	SecCamera::setFaceDetectLockUnlock(int facedetect_lockunlock)
 {
 	LOGV("%s(facedetect_lockunlock(%d))", __FUNCTION__, facedetect_lockunlock);
	
	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_FACEDETECT_LOCKUNLOCK, facedetect_lockunlock) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FACEDETECT_LOCKUNLOCK", __FUNCTION__);
		return -1;
	}

	return 0;
 }

 int SecCamera::setObjectPosition(int x, int y)
 {
 	LOGV("%s(setObjectPosition(x=%d, y=%d))", __FUNCTION__, x, y);

	if(m_preview_width ==640)
		x = x - 80;
	
	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_OBJECT_POSITION_X, x) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_OBJECT_POSITION_X", __FUNCTION__);
		return -1;
	}

	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_OBJECT_POSITION_Y, y) < 0) 
	{
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_OBJECT_POSITION_Y", __FUNCTION__);
		return -1;
	}

	return 0;
 }
 
 //======================================================================

 int SecCamera::setGamma(int gamma)
 {
	 LOGV("%s(gamma(%d))", __FUNCTION__, gamma);
	 
	 if(gamma<GAMMA_OFF|| GAMMA_MAX<= gamma)
	 {
		 LOGE("ERR(%s):Invalid gamma (%d)", __FUNCTION__, gamma);
		 return -1;
	 }
	 
	 if(m_video_gamma!= gamma)
	 { 
	 	 m_video_gamma = gamma;
	 if(m_flag_camera_start)
	 {
		 if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_GAMMA, gamma) < 0) 
		 {
			 LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_GAMMA", __FUNCTION__);
			 return -1;
		 }
	 }
	 }
 
	 return 0;
 }
 
 //======================================================================
 
 int SecCamera::setSlowAE(int slow_ae)
 {
	 LOGV("%s(slow_ae(%d))", __FUNCTION__, slow_ae);
	 
	 if(slow_ae<GAMMA_OFF|| GAMMA_MAX<= slow_ae)
	 {
		 LOGE("ERR(%s):Invalid slow_ae (%d)", __FUNCTION__, slow_ae);
		 return -1;
	 }
	 
	 if(m_slow_ae!= slow_ae)
	 {	 
	 	 m_slow_ae = slow_ae;
	 if(m_flag_camera_start)
	 {
		 if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_SET_SLOW_AE, slow_ae) < 0) 
		 {
			 LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_SLOW_AE", __FUNCTION__);
			 return -1;
		 }
	 }
	 }
 
	 return 0;
 }
 
 //======================================================================
 
 int SecCamera::setRecordingSize(int width, int height)
 {
	 LOGE("%s(width(%d), height(%d))", __FUNCTION__, width, height);
	 
	 m_recording_width  = width;
	 m_recording_height = height;
 
	 return 0;
 }

  //======================================================================

int SecCamera::setExifOrientationInfo(int orientationInfo)
{
	 LOGV("%s(orientationInfo(%d))", __FUNCTION__, orientationInfo);
	 
	 if(orientationInfo < 0)
	 {
		 LOGE("ERR(%s):Invalid orientationInfo (%d)", __FUNCTION__, orientationInfo);
		 return -1;
	 }
	 m_exif_orientation = orientationInfo;

	 return 0;
}

 //======================================================================
int SecCamera::setBatchReflection()
{
	if(m_flag_camera_start)
	{
	 	if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_BATCH_REFLECTION, 1) < 0) 
		 {
			 LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_BATCH_REFLECTION", __FUNCTION__);
			 return -1;
		 }
	}
	
	return 0;
}

/*Video call*/
int SecCamera::setVTmode(int vtmode)
{
	LOGV("%s(vtmode (%d))", __FUNCTION__, vtmode);
	
	if(vtmode < VT_MODE_OFF || VT_MODE_MAX <= vtmode)
	{
		LOGE("ERR(%s):Invalid vtmode (%d)", __FUNCTION__, vtmode);
		return -1;
	}
	
	if(m_vtmode != vtmode)
	{
		m_vtmode = vtmode;
	}

	return 0;
}

/* Camcorder fix fps */
int SecCamera::setSensorMode(int sensor_mode)
{
	LOGV("%s(sensor_mode (%d))", __FUNCTION__, sensor_mode);
	
	if(sensor_mode < SENSOR_MODE_CAMERA || SENSOR_MODE_MOVIE < sensor_mode)
	{
		LOGE("ERR(%s):Invalid sensor mode (%d)", __FUNCTION__, sensor_mode);
		return -1;
	}
	
	if(m_sensor_mode != sensor_mode)
	{
		m_sensor_mode = sensor_mode;
	}

	return 0;
}

/*	Shot mode	*/
/*	SINGLE = 0
*	CONTINUOUS = 1
*	PANORAMA = 2
*	SMILE = 3
*	SELF = 6
*/
int SecCamera::setShotMode(int shot_mode)
{
	LOGV("%s(shot_mode (%d))", __FUNCTION__, shot_mode);
	if(shot_mode < SHOT_MODE_SINGLE || SHOT_MODE_SELF < shot_mode)
	 {
		 LOGE("ERR(%s):Invalid shot_mode (%d)", __FUNCTION__, shot_mode);
		 return -1;
	 }
	 m_shot_mode = shot_mode;

	return 0;
}

int	SecCamera::getVTmode(void)
{
	return m_vtmode;
}

int SecCamera::setBlur(int blur_level)
{
	LOGV("%s(level (%d))", __FUNCTION__, blur_level);
	
	if(blur_level < BLUR_LEVEL_0|| BLUR_LEVEL_MAX <= blur_level)
	{
		LOGE("ERR(%s):Invalid level (%d)", __FUNCTION__, blur_level);
		return -1;
	}

	if(m_blur_level != blur_level)
	{
		m_blur_level = blur_level;	
		
		if(m_flag_camera_start)
		{
			if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_VGA_BLUR, blur_level) < 0) 
			{
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_VGA_BLUR", __FUNCTION__);
				return -1;
			}
		}
	}
	return 0;
}

int	SecCamera::getBlur(void)
{
	return m_blur_level;
}
	 

int SecCamera::setDataLineCheck(int chk_dataline)
{
	LOGV("%s(chk_dataline (%d))", __FUNCTION__, chk_dataline);
	
	if(chk_dataline < CHK_DATALINE_OFF || CHK_DATALINE_MAX<= chk_dataline)
	{
		LOGE("ERR(%s):Invalid chk_dataline (%d)", __FUNCTION__, chk_dataline);
		return -1;
	}
	
	if(m_chk_dataline != chk_dataline)
	{
		m_chk_dataline = chk_dataline;
	}

	return 0;
}

int	SecCamera::getDataLineCheck(void)
{
	return m_chk_dataline;
}

int SecCamera::setDataLineCheckStop(void)	
{
	LOGV("%s", __FUNCTION__);
	
	if(m_flag_camera_start)
	{
		if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_CHECK_DATALINE_STOP, 1) < 0) 
		 {
			 LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_CHECK_DATALINE_STOP", __FUNCTION__);
			 return -1;
		 }
	}
	return 0;
}

#endif

// ======================================================================
// Jpeg

#ifndef JPEG_FROM_SENSOR
unsigned char * SecCamera::getJpeg(unsigned char *snapshot_data, int snapshot_size, int * size)
{
	LOGV("%s()", __FUNCTION__);
	
	if(m_cam_fd <= 0)
	{
		LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
		return NULL;
	}

	unsigned char * jpeg_data = NULL;
	int 			jpeg_size = 0;

	jpeg_data = yuv2Jpeg(snapshot_data, snapshot_size, &jpeg_size, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);	
	
	*size = jpeg_size;
	return jpeg_data;
}
#endif

#ifndef JPEG_FROM_SENSOR
unsigned char * SecCamera::yuv2Jpeg(unsigned char * raw_data, int raw_size,int * jpeg_size,int width, int height, int pixel_format)
{
	LOGV("%s:raw_data(%p), raw_size(%d), jpeg_size(%d), width(%d), height(%d), format(%d)",
		__FUNCTION__, raw_data, raw_size, *jpeg_size, width, height, pixel_format);
	
	if(m_jpeg_fd <= 0)
	{
		LOGE("ERR(%s):JPEG device was closed\n", __FUNCTION__);
		return NULL;
	}
	if(pixel_format == V4L2_PIX_FMT_RGB565)
	{
		LOGE("ERR(%s):It doesn't support V4L2_PIX_FMT_RGB565\n", __FUNCTION__);
		return NULL;
	}

	unsigned char * InBuf = NULL;
	unsigned char * OutBuf = NULL;
	unsigned char * jpeg_data = NULL;
	long			frameSize;
	exif_file_info_t	ExifInfo;

	int input_file_format = JPG_MODESEL_YCBCR;

	int out_file_format = JPG_422;
	switch(pixel_format)
	{
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12T:
	case V4L2_PIX_FMT_YUV420:
		out_file_format = JPG_420;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUV422P:
		out_file_format = JPG_422;
		break;
	}

	//////////////////////////////////////////////////////////////
	// 2. set encode config.                                    //
	//////////////////////////////////////////////////////////////
	LOGV("Step 1 : JPEG_SET_ENCODE_IN_FORMAT(JPG_MODESEL_YCBCR)");
	if (SsbSipJPEGSetEncConfig(JPEG_SET_ENCODE_IN_FORMAT, input_file_format) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_IN_FORMAT\n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

	LOGV("Step 2 : JPEG_SET_SAMPING_MODE(JPG_422)");
	if (SsbSipJPEGSetEncConfig(JPEG_SET_SAMPING_MODE, out_file_format) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_SAMPING_MODE\n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

	LOGV("Step 3 : JPEG_SET_ENCODE_WIDTH(%d)", width);
	if (SsbSipJPEGSetEncConfig(JPEG_SET_ENCODE_WIDTH, width) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_WIDTH \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

	LOGV("Step 4 : JPEG_SET_ENCODE_HEIGHT(%d)", height);
	if (SsbSipJPEGSetEncConfig(JPEG_SET_ENCODE_HEIGHT, height) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_HEIGHT \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

	LOGV("Step 5 : JPEG_SET_ENCODE_QUALITY(JPG_QUALITY_LEVEL_2)");
	if (SsbSipJPEGSetEncConfig(JPEG_SET_ENCODE_QUALITY, JPG_QUALITY_LEVEL_2) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_QUALITY \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

#if (INCLUDE_JPEG_THUMBNAIL == 1)

	LOGV("Step 6a : JPEG_SET_ENCODE_THUMBNAIL(TRUE)");
	if (SsbSipJPEGSetEncConfig(JPEG_SET_ENCODE_THUMBNAIL, TRUE) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_THUMBNAIL \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}

	LOGV("Step 6b : JPEG_SET_THUMBNAIL_WIDTH(%d)", m_jpeg_thumbnail_width);
	if (SsbSipJPEGSetEncConfig(JPEG_SET_THUMBNAIL_WIDTH, m_jpeg_thumbnail_width) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_THUMBNAIL_WIDTH(%d) \n", __FUNCTION__, m_jpeg_thumbnail_height);
		goto YUV2JPEG_END;
	}

	LOGV("Step 6c : JPEG_SET_THUMBNAIL_HEIGHT(%d)", m_jpeg_thumbnail_height);
	if (SsbSipJPEGSetEncConfig(JPEG_SET_THUMBNAIL_HEIGHT, m_jpeg_thumbnail_height) != JPEG_OK) {
		LOGE("ERR(%s):Fail on JPEG_SET_THUMBNAIL_HEIGHT(%d) \n", __FUNCTION__, m_jpeg_thumbnail_height);
		goto YUV2JPEG_END;
	}

#endif

	if(raw_size == 0) //Kamat: This is our code path
	{
		unsigned int addr_y;
		int width, height,frame_size;
		getSnapshotSize(&width, &height, &frame_size);
		if(raw_data == NULL)
		{
			LOGE("%s %d] Raw data is NULL \n",__func__,__LINE__);
			goto YUV2JPEG_END;
		}
		else //Kamat: our path
		{
			addr_y = (unsigned int)raw_data;
		}

		SsbSipJPEGSetEncodeInBuf(m_jpeg_fd, addr_y, frame_size);
	}
	else
	{
		//////////////////////////////////////////////////////////////
		// 4. get Input buffer address                              //
		//////////////////////////////////////////////////////////////
		LOGV("Step 7 : Input buffer size(0x%X", raw_size);
		InBuf = (unsigned char *)SsbSipJPEGGetEncodeInBuf(m_jpeg_fd, raw_size);
		if(InBuf == NULL)
		{
			LOGE("ERR(%s):Fail on SsbSipJPEGGetEncodeInBuf \n", __FUNCTION__);
			goto YUV2JPEG_END;
		}
		//////////////////////////////////////////////////////////////
		// 5. put YUV stream to Input buffer						
		//////////////////////////////////////////////////////////////
		LOGV("Step 8: memcpy(InBuf(%p), raw_data(%p), raw_size(%d)", InBuf, raw_data, raw_size);
		memcpy(InBuf, raw_data, raw_size);
	}

	//////////////////////////////////////////////////////////////
	// 6. Make Exif info parameters 							
	//////////////////////////////////////////////////////////////
	LOGV("Step 9: m_makeExifParam()");
	memset(&ExifInfo, 0x00, sizeof(exif_file_info_t));
	m_makeExifParam(&ExifInfo);

	//////////////////////////////////////////////////////////////
	// 7. Encode YUV stream 									
	//////////////////////////////////////////////////////////////
	LOGV("Step a: SsbSipJPEGEncodeExe()");
	if(SsbSipJPEGEncodeExe(m_jpeg_fd, &ExifInfo, JPEG_USE_SW_SCALER) != JPEG_OK)	  //with Exif
	{
		LOGE("ERR(%s):Fail on SsbSipJPEGEncodeExe \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}
	//////////////////////////////////////////////////////////////
	// 8. get output buffer address 						
	//////////////////////////////////////////////////////////////
	LOGV("Step b: SsbSipJPEGGetEncodeOutBuf()");
	OutBuf = (unsigned char *)SsbSipJPEGGetEncodeOutBuf(m_jpeg_fd, &frameSize);
	if(OutBuf == NULL)
	{
		LOGE("ERR(%s):Fail on SsbSipJPEGGetEncodeOutBuf \n", __FUNCTION__);
		goto YUV2JPEG_END;
	}
	//////////////////////////////////////////////////////////////
	// 9. write JPEG result file								
	//////////////////////////////////////////////////////////////
	LOGV("Done");
	jpeg_data  = OutBuf;
	*jpeg_size = (int)frameSize;

YUV2JPEG_END :

	return jpeg_data;
}
#endif

int SecCamera::setJpegThumbnailSize(int width, int height)
{
	LOGV("%s(width(%d), height(%d))", __FUNCTION__, width, height);
	
	m_jpeg_thumbnail_width	= width;
	m_jpeg_thumbnail_height = height;

	return 0;
}

int SecCamera::getJpegThumbnailSize(int * width, int  * height)
{
	if(width)
		*width	 = m_jpeg_thumbnail_width;
	if(height)
		*height  = m_jpeg_thumbnail_height;
	
	return 0;
}

void SecCamera::setExifFixedAttribute()
{

}

void SecCamera::setExifChangedAttribute()
{    

}

// ======================================================================
// Conversions

inline int SecCamera::m_frameSize(int format, int width, int height)
{
	int size = 0;

	switch(format)
	{
		case V4L2_PIX_FMT_YUV420 :
		case V4L2_PIX_FMT_NV12 :
		case V4L2_PIX_FMT_NV21 :
			size = (width * height * 3 / 2);
			break;

		case V4L2_PIX_FMT_NV12T:
			size = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)) + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height/2));
			break;
			
		case V4L2_PIX_FMT_YUV422P :
		case V4L2_PIX_FMT_YUYV :
		case V4L2_PIX_FMT_UYVY :
			size = (width * height * 2);
			break;

		default : 
			LOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __FUNCTION__, format);
		case V4L2_PIX_FMT_RGB565 :
			size = (width * height * BPP);
			break;
	}
	
	return size;
}

status_t SecCamera::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, 255, "dump(%d)\n", fd);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

}; // namespace android
