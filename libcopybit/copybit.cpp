/*
 * Copyright (C) 2008 The Android Open Source Project
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
** 
** @author siva krishna neeli(siva.neeli@samsung.com)
** @date   2009-02-27
*/

#define LOG_TAG "copybit"

//#define LOG_DEBUG 0

#define USE_HW_PMEM
#define USE_SGX_GRALLOC

#include <cutils/log.h>

#include <linux/fb.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <pthread.h>

#include <hardware/copybit.h>
#include <linux/android_pmem.h>
# include <asm/page.h>

#include "utils/Timers.h"

#include <linux/videodev2.h>
#include "s3c_mem.h"
#include "s5p_fimc.h"
#include "gralloc_priv.h"

//------------ DEFINE ---------------------------------------------------------//
#ifndef DEFAULT_FB_NUM
#define DEFAULT_FB_NUM 0
#endif

#define NUM_OF_MEMORY_OBJECT 2
#define MAX_RESIZING_RATIO_LIMIT  63

#define S3C_ALPHA_NOP 0xff

// some pixel formats are not defined in gingerbread compared with froyo
// so those are defined temporarily for compile
#define    COPYBIT_FORMAT_YCbCr_422_P  0x12 //HAL_PIXEL_FORMAT_YCbCr_422_P,
#define    COPYBIT_FORMAT_YCbCr_420_P  0x13 //HAL_PIXEL_FORMAT_YCbCr_420_P,
#define    COPYBIT_FORMAT_YCbCr_422_I  0x14 //HAL_PIXEL_FORMAT_YCbCr_422_I,
#define    COPYBIT_FORMAT_YCbCr_420_I  0x15 //HAL_PIXEL_FORMAT_YCbCr_420_I,
#define    COPYBIT_FORMAT_CbYCrY_422_I 0x16 //HAL_PIXEL_FORMAT_CbYCrY_422_I,
#define    COPYBIT_FORMAT_CbYCrY_420_I 0x17 //HAL_PIXEL_FORMAT_CbYCrY_420_I,
#define    COPYBIT_FORMAT_YCbCr_420_SP 0x21 //HAL_PIXEL_FORMAT_YCbCr_420_SP,
#define    COPYBIT_FORMAT_YCrCb_422_SP 0x23 //HAL_PIXEL_FORMAT_YCrCb_422_SP,

//------------ STRUCT ---------------------------------------------------------//

typedef struct _s3c_rect {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
}s3c_rect;

typedef struct _s3c_img {
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t offset;
	uint32_t base;
	int memory_id;
}s3c_img;

typedef struct _s3c_fb_t {
	unsigned int    width;
	unsigned int    height;
	unsigned int	bpp;
}s3c_fb_t;


struct yuv_fmt_list yuv_list[] = {
	{ "V4L2_PIX_FMT_NV12", 		"YUV420/2P/LSB_CBCR", 	V4L2_PIX_FMT_NV12, 		12, 2 },
	{ "V4L2_PIX_FMT_NV12T", 	"YUV420/2P/LSB_CBCR", 	V4L2_PIX_FMT_NV12T,		12, 2 },
	{ "V4L2_PIX_FMT_NV21", 		"YUV420/2P/LSB_CRCB", 	V4L2_PIX_FMT_NV21, 		12, 2 },
	{ "V4L2_PIX_FMT_NV21X", 	"YUV420/2P/MSB_CBCR", 	V4L2_PIX_FMT_NV21X, 	12, 2 },
	{ "V4L2_PIX_FMT_NV12X", 	"YUV420/2P/MSB_CRCB", 	V4L2_PIX_FMT_NV12X, 	12, 2 },
	{ "V4L2_PIX_FMT_YUV420", 	"YUV420/3P", 			V4L2_PIX_FMT_YUV420, 	12, 3 },
	{ "V4L2_PIX_FMT_YUYV", 		"YUV422/1P/YCBYCR", 	V4L2_PIX_FMT_YUYV, 		16, 1 },
	{ "V4L2_PIX_FMT_YVYU", 		"YUV422/1P/YCRYCB", 	V4L2_PIX_FMT_YVYU, 		16, 1 },
	{ "V4L2_PIX_FMT_UYVY", 		"YUV422/1P/CBYCRY", 	V4L2_PIX_FMT_UYVY, 		16, 1 },
	{ "V4L2_PIX_FMT_VYUY", 		"YUV422/1P/CRYCBY", 	V4L2_PIX_FMT_VYUY, 		16, 1 },
	{ "V4L2_PIX_FMT_UV12", 		"YUV422/2P/LSB_CBCR", 	V4L2_PIX_FMT_NV16, 		16, 2 },
	{ "V4L2_PIX_FMT_UV21", 		"YUV422/2P/LSB_CRCB", 	V4L2_PIX_FMT_NV61, 		16, 2 },
	{ "V4L2_PIX_FMT_UV12X", 	"YUV422/2P/MSB_CBCR", 	V4L2_PIX_FMT_NV16X, 	16, 2 },
	{ "V4L2_PIX_FMT_UV21X", 	"YUV422/2P/MSB_CRCB", 	V4L2_PIX_FMT_NV61X, 	16, 2 },
	{ "V4L2_PIX_FMT_YUV422P", 	"YUV422/3P", 			V4L2_PIX_FMT_YUV422P, 	16, 3 },
};

typedef struct _s3c_mem_t{
	int             dev_fd;   
	struct s3c_mem_alloc mem_alloc[NUM_OF_MEMORY_OBJECT];
	//unsigned int  	virt_addr;
	//unsigned int    phys_addr;
	//uint32_t		len;
	//struct s3c_mem_alloc 		mem_alloc_info;
}s3c_mem_t;

#ifdef USE_HW_PMEM
typedef struct _sec_pmem_alloc{
	int		fd;
	int		total_size;
	int		offset;
	int		size;
	unsigned int	virt_addr;
	unsigned int	phys_addr;
} sec_pmem_alloc_t;

typedef struct _sec_pmem_t{
	int 	pmem_master_fd;
	void	*pmem_master_base;	
	int		pmem_total_size;

	sec_pmem_alloc_t	sec_pmem_alloc[NUM_OF_MEMORY_OBJECT];
} sec_pmem_t;
#endif

struct copybit_context_t {
	struct copybit_device_t device;
	s3c_fb_t                s3c_fb;
	s5p_fimc_t              s5p_fimc;
	s3c_mem_t           	s3c_mem;
#ifdef USE_HW_PMEM
	sec_pmem_t				sec_pmem;
#endif
	
	uint8_t                 mAlpha;
	uint8_t                 mFlags;

	// the number of instances to open copybit module
	unsigned int			count;
};

//----------------------- Common hardware methods ------------------------------//

static int open_copybit (const struct hw_module_t* module, const char* name, struct hw_device_t** device);
static int close_copybit(struct hw_device_t *dev);

static int set_parameter_copybit(struct copybit_device_t *dev, int name, int value) ;
static int get_parameter_copybit(struct copybit_device_t *dev, int name);
static int stretch_copybit(struct copybit_device_t *dev,
                           struct copybit_image_t  const *dst,
                           struct copybit_image_t  const *src,
                           struct copybit_rect_t   const *dst_rect,
                           struct copybit_rect_t   const *src_rect,
                           struct copybit_region_t const *region);
static int blit_copybit(struct copybit_device_t *dev,
                        struct copybit_image_t  const *dst,
                        struct copybit_image_t  const *src,
                        struct copybit_region_t const *region);

//---------------------- Function Declarations ---------------------------------//
static int sec_stretch(struct copybit_context_t *ctx,
                           s3c_img *src_img, s3c_rect *src_rect,
                           s3c_img *dst_img, s3c_rect *dst_rect);

static int createPP  (struct copybit_context_t *ctx);
static int destroyPP (struct copybit_context_t *ctx);
static int doPP      (struct copybit_context_t *ctx,
               	      unsigned int src_phys_addr, s3c_img *src_img, s3c_rect *src_rect, uint32_t src_color,
                      unsigned int dst_phys_addr, s3c_img *dst_img, s3c_rect *dst_rect, uint32_t dst_color, int rotate_flag);

static unsigned int get_yuv_bpp(unsigned int fmt);
static unsigned int get_yuv_planes(unsigned int fmt);
static inline int colorFormatCopybit2PP(int format);
static int fimc_v4l2_streamon(int fp);
static int fimc_v4l2_streamoff(int fp);
static int fimc_v4l2_start_overlay(int fd);
static int fimc_v4l2_stop_overlay(int fd);
static int fimc_v4l2_s_crop(int fp, int target_width, int target_height, int h_offset, int v_offset);

static int createMem (struct copybit_context_t *ctx, unsigned int index, unsigned int memory_size);
static int destroyMem(struct copybit_context_t *ctx);
static int checkMem(struct copybit_context_t *ctx, unsigned int index, unsigned int requested_size);

#ifdef USE_HW_PMEM
static int initPmem (struct copybit_context_t *ctx);
static int destroyPmem(struct copybit_context_t *ctx);
static int checkPmem(struct copybit_context_t *ctx, unsigned int index, unsigned int requested_size);
#endif

static inline unsigned int getFrameSize(int colorformat, int width, int height);

static inline int rotateValueCopybit2PP(unsigned char flags);
static inline int heightOfPP(int pp_color_format, int number);
static inline int widthOfPP(unsigned int ver, int pp_color_format, int number);
static inline int multipleOf2 (int number);
static inline int multipleOf4 (int number);
static inline int multipleOf8 (int number);
static inline int multipleOf16 (int number);

static int can_support_rgb(struct copybit_context_t *ctx, int32_t format);

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

//---------------------- The COPYBIT Module ---------------------------------//

static struct hw_module_methods_t copybit_module_methods = {
	open:  open_copybit
};

struct copybit_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: COPYBIT_HARDWARE_MODULE_ID,
        name: "Samsung S5P C110 COPYBIT Module",
        author: "Samsung Electronics, Inc.",
        methods: &copybit_module_methods,
    }
};

//------------- GLOBAL VARIABLE-------------------------------------------------//

int ctx_created;
struct copybit_context_t *g_ctx;
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;

//-----------------------------------------------------------------------------//

// Open a new instance of a copybit device using name
static int open_copybit(const struct hw_module_t* module, const char* name, struct hw_device_t** device)
{
	int status = 0;
#ifdef USE_HW_PMEM
	int ret;
#endif

    pthread_mutex_lock(&lock);
	if (ctx_created == 1)
	{
		*device = &g_ctx->device.common;
		status = 0;
		g_ctx->count++;
    	pthread_mutex_unlock(&lock);
		return status;
	}

    struct copybit_context_t *ctx = (struct copybit_context_t *)malloc(sizeof(struct copybit_context_t));

	g_ctx = ctx;

	memset(ctx, 0, sizeof(*ctx));

	ctx->device.common.tag     = HARDWARE_DEVICE_TAG;
	ctx->device.common.version = 0;
	ctx->device.common.module  = (struct hw_module_t *)module;
	ctx->device.common.close   = close_copybit;
	ctx->device.set_parameter  = set_parameter_copybit;
	ctx->device.get            = get_parameter_copybit;
	ctx->device.blit           = blit_copybit;
	ctx->device.stretch        = stretch_copybit;
	ctx->mAlpha                = S3C_ALPHA_NOP;
	ctx->mFlags                = 0;

	// initialize fd
	ctx->s5p_fimc.dev_fd  = -1;
	ctx->s3c_mem.dev_fd   = -1;
#ifdef USE_HW_PMEM
	ctx->sec_pmem.pmem_master_fd = -1;
#endif

	// get width * height for decide virtual frame size..
	char const * const device_template[] = {
			"/dev/graphics/fb%u",
			"/dev/fb%u",
			0 };
	int fb_fd = -1;
	int i=0;
	char fb_name[64];
	struct fb_var_screeninfo info;

	while ((fb_fd==-1) && device_template[i]) {
		snprintf(fb_name, 64, device_template[i], DEFAULT_FB_NUM);
		fb_fd = open(fb_name, O_RDONLY, 0);
		if (0 > fb_fd)
			LOGE("%s:: %s errno=%d (%s)\n", __func__, fb_name, errno, strerror(errno));
		i++;
	}

	if (0 < fb_fd && ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) >= 0)
	{
		ctx->s3c_fb.width  = info.xres;
		ctx->s3c_fb.height = info.yres;
		ctx->s3c_fb.bpp	   = info.bits_per_pixel;
	}
	else
	{
		LOGE("%s::%d = open(%s) fail or FBIOGET_VSCREENINFO fail\n", __func__, fb_fd, fb_name);
		status = -EINVAL;
	}

	if(0 < fb_fd)
		close(fb_fd);

#ifdef USE_HW_PMEM
	if (0 > (ret = initPmem(ctx))) {
		LOGE("%s::initPmem fail %d (%s)\n", __func__, ret, strerror(errno));
	}
#endif

	if(createMem(ctx, 0, 0) < 0) {
		LOGE("%s::createMem fail (size=0)\n", __func__);
		return -1;
	} 

	//set PP
	if(createPP(ctx) < 0) {
		LOGE("%s::createPP fail\n", __func__);
		status = -EINVAL;
	}
		
	if (status == 0) {
		*device = &ctx->device.common;

		ctx->count = 1;
		ctx_created = 1;
	} else {
		close_copybit(&ctx->device.common);
	}

    pthread_mutex_unlock(&lock);

	return status;
}

// Close the copybit device
static int close_copybit(struct hw_device_t *dev)
{
	struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
	int ret = 0;

    pthread_mutex_lock(&lock);
	ctx->count--;

	if (0 >= ctx->count) {
		ctx->s3c_fb.width = 0;
		ctx->s3c_fb.height = 0;
		ctx->s3c_fb.bpp = 0;
        
		if(destroyPP(ctx) < 0) {
			LOGE("%s::destroyPP fail\n", __func__);
			ret = -1;
		}
		
		if(destroyMem(ctx) < 0) {
			LOGE("%s::destroyMem fail\n", __func__);
			ret = -1;
		}
		
#ifdef USE_HW_PMEM
		if(destroyPmem(ctx) < 0) {
			LOGE("%s::destroyPmem fail\n", __func__);
			ret = -1;
		}
#endif

        free(ctx);

		ctx_created = 0;
		g_ctx = NULL;
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

// min of int a, b
static inline int min(int a, int b) {
    return (a<b) ? a : b;
}

// max of int a, b
static inline int max(int a, int b) {
    return (a>b) ? a : b;
}

// scale each parameter by mul/div. Assume div isn't 0
static inline void MULDIV(uint32_t *a, uint32_t *b, int mul, int div) {
    if (mul != div) {
        *a = (mul * *a) / div;
        *b = (mul * *b) / div;
    }
}

// Determine the intersection of lhs & rhs store in out
static void intersect(struct copybit_rect_t *out,
                      const struct copybit_rect_t *lhs,
                      const struct copybit_rect_t *rhs) {
    out->l = max(lhs->l, rhs->l);
    out->t = max(lhs->t, rhs->t);
    out->r = min(lhs->r, rhs->r);
    out->b = min(lhs->b, rhs->b);
}

// convert from copybit image to mdp image structure
#ifdef USE_SGX_GRALLOC
static void set_image(s3c_img *img, const struct copybit_image_t *rhs) 
{
	// this code is specific to MSM7K
	// Need to modify
    struct private_handle_t* hnd = (struct private_handle_t*)rhs->handle;

    img->width      = rhs->w;
    img->height     = rhs->h;
    img->format     = rhs->format;

	if (0 != (uint32_t)rhs->base)
		img->base		= (uint32_t)rhs->base;
	else
		img->base		= hnd->base;

	if (hnd)
	{
		img->offset		= hnd->offset;
		img->memory_id 	= hnd->fd;
	}
	else
	{
		img->offset		= 0;
		img->memory_id 	= 0;
	}
}
#else
static void set_image(s3c_img *img, const struct copybit_image_t *rhs) 
{
	// this code is specific to MSM7K
	// Need to modify
    struct private_handle_t* hnd = (struct private_handle_t*)rhs->handle;

    img->width      = rhs->w;
    img->height     = rhs->h;
    img->format     = rhs->format;
	img->base		= hnd->base;

    img->offset		= hnd->offset;
    img->memory_id 	= hnd->fd;
}
#endif

// setup rectangles
static void set_rects(struct copybit_context_t *dev,
                      s3c_img  *src_img,
                      s3c_rect *src_rect,
                      s3c_rect *dst_rect,
                      const struct copybit_rect_t *src,
                      const struct copybit_rect_t *dst,
                      const struct copybit_rect_t *scissor)
{
    struct copybit_rect_t clip;
    intersect(&clip, scissor, dst);

    dst_rect->x  = clip.l;
    dst_rect->y  = clip.t;
    dst_rect->w  = clip.r - clip.l;
    dst_rect->h  = clip.b - clip.t;

    uint32_t W, H;
    if (dev->mFlags & COPYBIT_TRANSFORM_ROT_90) {
        src_rect->x = (clip.t - dst->t) + src->t;
        src_rect->y = (dst->r - clip.r) + src->l;
        src_rect->w = (clip.b - clip.t);
        src_rect->h = (clip.r - clip.l);
        W = dst->b - dst->t;
        H = dst->r - dst->l;
    } else {
        src_rect->x  = (clip.l - dst->l) + src->l;
        src_rect->y  = (clip.t - dst->t) + src->t;
        src_rect->w  = (clip.r - clip.l);
        src_rect->h  = (clip.b - clip.t);
        W = dst->r - dst->l;
        H = dst->b - dst->t;
    }
    MULDIV(&src_rect->x, &src_rect->w, src->r - src->l, W);
    MULDIV(&src_rect->y, &src_rect->h, src->b - src->t, H);
    if (dev->mFlags & COPYBIT_TRANSFORM_FLIP_V) {
        src_rect->y = src_img->height - (src_rect->y + src_rect->h);
    }
    if (dev->mFlags & COPYBIT_TRANSFORM_FLIP_H) {
        src_rect->x = src_img->width  - (src_rect->x + src_rect->w);
    }
}

// Set a parameter to value
static int set_parameter_copybit(struct copybit_device_t *dev, int name, int value) 
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int status = 0;
    if (ctx) {
        switch(name) {
        case COPYBIT_ROTATION_DEG:
            switch (value) {
            case 0:
                ctx->mFlags &= ~0x7;
                break;
            case 90:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= COPYBIT_TRANSFORM_ROT_90;
                break;
            case 180:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= COPYBIT_TRANSFORM_ROT_180;
                break;
            case 270:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= COPYBIT_TRANSFORM_ROT_270;
                break;
            default:
                LOGE("%s::Invalid value for COPYBIT_ROTATION_DEG", __func__);
                status = -EINVAL;
                break;
            }
            break;
        case COPYBIT_PLANE_ALPHA:
            if (value < 0)      value = 0;
            if (value >= 256)   value = 255;
            ctx->mAlpha = value;
            break;
        case COPYBIT_DITHER:
            if (value == COPYBIT_ENABLE) {
                ctx->mFlags |= 0x8;
            } else if (value == COPYBIT_DISABLE) {
                ctx->mFlags &= ~0x8;
            }
            break;
        case COPYBIT_TRANSFORM:
            ctx->mFlags &= ~0x7;
            ctx->mFlags |= value & 0x7;
            break;

        default:
            status = -EINVAL;
            break;
        }
    } else {
        status = -EINVAL;
    }
    return status;
}

// Get a static info value
static int get_parameter_copybit(struct copybit_device_t *dev, int name) 
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int value;
    if (ctx) {
        switch(name) {
        case COPYBIT_MINIFICATION_LIMIT:
	    	value = MAX_RESIZING_RATIO_LIMIT;
            break;
        case COPYBIT_MAGNIFICATION_LIMIT:
            value = MAX_RESIZING_RATIO_LIMIT;
            break;
        case COPYBIT_SCALING_FRAC_BITS:
            value = 32;
            break;
        case COPYBIT_ROTATION_STEP_DEG:
            value = 90;
            break;
        default:
            value = -EINVAL;
        }
    } else {
        value = -EINVAL;
    }
    return value;
}

// do a stretch blit type operation
static int stretch_copybit(struct copybit_device_t *dev,
                           struct copybit_image_t  const *dst,
                           struct copybit_image_t  const *src,
                           struct copybit_rect_t   const *dst_rect,
                           struct copybit_rect_t   const *src_rect,
                           struct copybit_region_t const *region) 
{
	struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
	int status = 0, ret = 0;

	if ((ret = can_support_rgb(ctx, src->format)) < 0)
	{
		LOGE("%s::sec_stretch: not support src->format = 0x%x", __func__, src->format);
		return -1;
	}

	if (ctx)
	{
		const struct copybit_rect_t bounds = { 0, 0, dst->w, dst->h };
		s3c_img src_img; 
		s3c_img dst_img;
		s3c_rect src_work_rect;
		s3c_rect dst_work_rect;

		struct copybit_rect_t clip;
		int count = 0;
		status = 0;

		while ((status == 0) && region->next(region, &clip))
		{
			count++;
			intersect(&clip, &bounds, &clip);
			set_image(&src_img, src);
			set_image(&dst_img, dst);
			set_rects(ctx, &src_img,&src_work_rect, &dst_work_rect, src_rect, dst_rect, &clip);

			if((ret = sec_stretch(ctx, &src_img, &src_work_rect, &dst_img, &dst_work_rect)) < 0)
			{
				LOGE("%s::sec_stretch fail : ret=%d\n", __func__, ret);
				status = -EINVAL;
			}
		}
	}
	else 
	{
		status = -EINVAL;
	}

	// re-initialize
	ctx->mAlpha                = S3C_ALPHA_NOP;
	ctx->mFlags                = 0;

	return status;
}

// Perform a blit type operation
static int blit_copybit(struct copybit_device_t *dev,
                        struct copybit_image_t  const *dst,
                        struct copybit_image_t  const *src,
                        struct copybit_region_t const *region) 
{
	int ret = 0;

	struct copybit_rect_t dr = { 0, 0, dst->w, dst->h };
	struct copybit_rect_t sr = { 0, 0, src->w, src->h };

	struct copybit_context_t* ctx = (struct copybit_context_t*)dev;

	ret = stretch_copybit(dev, dst, src, &dr, &sr, region);

	return ret;
}

//-----------------------------------------------------------------------------//

static int get_src_phys_addr(struct copybit_context_t *ctx, s3c_img *src_img, s3c_rect *src_rect)
{
	s5p_fimc_t	*s5p_fimc  	= &ctx->s5p_fimc;
	struct s3c_mem_alloc *s3c_mem  	= &ctx->s3c_mem.mem_alloc[0];
#ifdef	USE_HW_PMEM
	sec_pmem_alloc_t	*pm_alloc	= &ctx->sec_pmem.sec_pmem_alloc[0]; 
#endif

	unsigned int src_virt_addr 	= 0;
	unsigned int src_phys_addr 	= 0;
	unsigned int src_frame_size = 0;

	struct pmem_region 	region;

	switch(src_img->format)
	{
		case COPYBIT_FORMAT_CUSTOM_YCbCr_420_SP:
		case COPYBIT_FORMAT_CUSTOM_YCrCb_420_SP: //Kamat
			// sw workaround for video content zero copy
			memcpy(&s5p_fimc->params.src.buf_addr_phy_rgb_y, (void*)((unsigned int)src_img->base), 4);
			memcpy(&s5p_fimc->params.src.buf_addr_phy_cb, (void*)((unsigned int)src_img->base+4), 4);
			src_phys_addr = s5p_fimc->params.src.buf_addr_phy_rgb_y;
			if (0 == src_phys_addr)
			{
				LOGE("%s address error (format=CUSTOM_YCbCr/YCrCb_420_SP Y-addr=0x%x CbCr-Addr=0x%x)\n", 
						__func__, s5p_fimc->params.src.buf_addr_phy_rgb_y, s5p_fimc->params.src.buf_addr_phy_cb);
				return 0;
			}
			break;

		case COPYBIT_FORMAT_CUSTOM_YCbCr_422_I:
		case COPYBIT_FORMAT_CUSTOM_CbYCrY_422_I:
			// sw workaround for camera capture zero copy
			memcpy(&s5p_fimc->params.src.buf_addr_phy_rgb_y, (void*)((unsigned int)src_img->base + src_img->offset), 4);
			src_phys_addr = s5p_fimc->params.src.buf_addr_phy_rgb_y;
			if (0 == src_phys_addr)
			{
				LOGE("%s address error (format=CUSTOM_YCbCr/CbYCrY_422_I Y-addr=0x%x)\n", 
						__func__, s5p_fimc->params.src.buf_addr_phy_rgb_y);
				return 0;
			}
			break;

		default:

			// check the pmem case
			if(ioctl(src_img->memory_id, PMEM_GET_PHYS, &region) >= 0)
			{
				src_phys_addr = (unsigned int)region.offset + src_img->offset;
			}
			else
			{
				// copy
				src_frame_size = getFrameSize(src_img->format, src_img->width, src_img->height);
		
				if(src_frame_size == 0)
				{
					LOGE("%s::getFrameSize fail \n", __func__);
					return 0;
				}
		
#ifdef	USE_HW_PMEM
				if (0 <= checkPmem(ctx, 0, src_frame_size))
				{
					src_virt_addr	= pm_alloc->virt_addr;
					src_phys_addr	= pm_alloc->phys_addr;
					pm_alloc->size	= src_frame_size;
				}
				else
#endif
				if (0 <= checkMem(ctx, 0, src_frame_size))
				{
					src_virt_addr	= s3c_mem->vir_addr;
					src_phys_addr	= s3c_mem->phy_addr;
					s3c_mem->size	= src_frame_size;
				}
				else
				{
					LOGE("%s::check_mem fail \n", __func__);
					return 0;
				}
		
				memcpy((void *)src_virt_addr, (void*)((unsigned int)src_img->base), src_frame_size);
			}
	}
	
	return src_phys_addr;
}

static int get_dst_phys_addr(struct copybit_context_t *ctx, s3c_img *dst_img, s3c_rect *dst_rect, int *dst_memcpy_flag)
{
	struct s3c_mem_alloc *s3c_mem  	= &ctx->s3c_mem.mem_alloc[1];
#ifdef	USE_HW_PMEM
	sec_pmem_alloc_t	*pm_alloc		= &ctx->sec_pmem.sec_pmem_alloc[1]; 
#endif
	unsigned int dst_phys_addr	= 0;
	unsigned int dst_frame_size	= 0;

	struct pmem_region 	region;

    if (0 == dst_img->memory_id && 0 != dst_img->base)
    {
    	dst_phys_addr = dst_img->base;
    }
    else	
	{
		dst_frame_size = getFrameSize(dst_img->format, dst_img->width, dst_img->height);
		if(dst_frame_size == 0)
		{
			LOGE("%s::getFrameSize fail \n", __func__);
			return 0;
		}

#ifdef	USE_HW_PMEM
		if (0 <= checkPmem(ctx, 1, dst_frame_size))
		{
			//src_virt_addr	= sec_pm->sec_pmem_alloc.virt_addr;
			dst_phys_addr	= pm_alloc->phys_addr;
			pm_alloc->size	= dst_frame_size;
		}
		else
#endif
		if (0 <= checkMem(ctx, 1, dst_frame_size))
		{
			s3c_mem->size	= dst_frame_size;
			dst_phys_addr 	= s3c_mem->phy_addr;
		}
		else
		{
			LOGE("%s::check_mem fail \n", __func__);
			return 0;
		}


		// memcpy trigger...
		*dst_memcpy_flag = 1;
	}

	return dst_phys_addr;
}

static int sec_stretch(struct copybit_context_t *ctx,
                           s3c_img *src_img, s3c_rect *src_rect,
                           s3c_img *dst_img, s3c_rect *dst_rect)
{
	s5p_fimc_t *  s5p_fimc   = &ctx->s5p_fimc;

	unsigned int 	src_phys_addr  	= 0;
	unsigned int 	dst_phys_addr  	= 0;
	int          	rotate_value   	= 0;
	int          	flag_force_memcpy = 0;
	int32_t			src_color_space;
	int32_t			dst_color_space;


	// 1 : source address and size
	// becase of tierring issue...very critical..
	
	if (ctx->mAlpha < 255) return -8;

	if(0 == (src_phys_addr = get_src_phys_addr(ctx, src_img, src_rect)))
		return -1;

	// 2 : destination address and size
	if(0 == (dst_phys_addr = get_dst_phys_addr(ctx, dst_img, dst_rect, &flag_force_memcpy)))
		return -2;

	// check whether fimc supports the src format
	if (0 > (src_color_space = colorFormatCopybit2PP(src_img->format)))
		return -3;

	if (0 > (dst_color_space = colorFormatCopybit2PP(dst_img->format)))
		return -4;

	if(doPP(ctx, src_phys_addr, src_img, src_rect, (uint32_t)src_color_space,
			         dst_phys_addr, dst_img, dst_rect, (uint32_t)dst_color_space, ctx->mFlags) < 0) 
		return -5;

	if(flag_force_memcpy == 1)
	{
#ifdef USE_HW_PMEM
		if (0 != ctx->sec_pmem.sec_pmem_alloc[1].size) {
			struct s3c_mem_dma_param s3c_mem_dma;

			s3c_mem_dma.src_addr = (unsigned long)(ctx->sec_pmem.sec_pmem_alloc[1].virt_addr);
			s3c_mem_dma.size     = ctx->sec_pmem.sec_pmem_alloc[1].size;
			ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_CACHE_INV, &s3c_mem_dma);

			memcpy((void*)((unsigned int)dst_img->base), (void *)(ctx->sec_pmem.sec_pmem_alloc[1].virt_addr), ctx->sec_pmem.sec_pmem_alloc[1].size);
		} else
#endif
		{
			struct s3c_mem_alloc *s3c_mem  	= &ctx->s3c_mem.mem_alloc[1];
			struct s3c_mem_dma_param s3c_mem_dma;

			s3c_mem_dma.src_addr 	= (unsigned long)s3c_mem->vir_addr;
			s3c_mem_dma.size        = s3c_mem->size;
			ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_CACHE_INV, &s3c_mem_dma);

			memcpy((void*)((unsigned int)dst_img->base), (void *)s3c_mem->vir_addr, s3c_mem->size);
		}
	}

	return 0;
}

int fimc_v4l2_set_src(int fd, unsigned int hw_ver, s5p_fimc_img_info *src)
{
	struct v4l2_format			fmt;
	struct v4l2_cropcap			cropcap;
	struct v4l2_crop			crop;
	struct v4l2_requestbuffers	req;    
	int		ret_val;

	/* 
	 * To set size & format for source image (DMA-INPUT) 
	 */

	fmt.type 				= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width 		= src->full_width;
	fmt.fmt.pix.height 		= src->full_height;
	fmt.fmt.pix.pixelformat	= src->color_space;
	fmt.fmt.pix.field 		= V4L2_FIELD_NONE; 

	ret_val = ioctl (fd, VIDIOC_S_FMT, &fmt);
	if (ret_val < 0) {
    	LOGE("VIDIOC_S_FMT failed : ret=%d errno=%d (%s) : fd=%d\n", ret_val, errno, strerror(errno), fd);
		return -1;
	}

	/* 
	 * crop input size
	 */

	crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (0x50 == hw_ver)
	{
		crop.c.left   = src->start_x;
		crop.c.top    = src->start_y;
	}
	else
	{
		crop.c.left   = 0;
		crop.c.top    = 0;
	}
	crop.c.width  = src->width;
	crop.c.height = src->height;

	ret_val = ioctl(fd, VIDIOC_S_CROP, &crop);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_CROP (%d)\n",ret_val);
		return -1;
	}

	/* 
	 * input buffer type
	 */

	req.count       = 1;
	req.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory      = V4L2_MEMORY_USERPTR;

 	ret_val = ioctl (fd, VIDIOC_REQBUFS, &req);
 	if (ret_val < 0) 
	{
		LOGE("Error in VIDIOC_REQBUFS (%d)\n", ret_val);
		return -1;
	}

	return ret_val;
}

int fimc_v4l2_set_dst(int fd, s5p_fimc_img_info *dst, int rotation, unsigned int addr)
{
	struct v4l2_format		sFormat;
	struct v4l2_control		vc;
	struct v4l2_framebuffer	fbuf;
	int				ret_val;

	/* 
	 * set rotation configuration
	 */

	vc.id = V4L2_CID_ROTATION;
	vc.value = rotation;

	ret_val = ioctl(fd, VIDIOC_S_CTRL, &vc);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_CTRL - rotation (%d)\n",ret_val);
		return -1;
	}

	/*
	 * set size, format & address for destination image (DMA-OUTPUT)
	 */
	ret_val = ioctl (fd, VIDIOC_G_FBUF, &fbuf);
	if (ret_val < 0){
		LOGE("Error in video VIDIOC_G_FBUF (%d)\n", ret_val);
		return -1;
	}

	fbuf.base 				= (void *)addr;
	fbuf.fmt.width 			= dst->full_width;
	fbuf.fmt.height 		= dst->full_height;
	fbuf.fmt.pixelformat 	= dst->color_space;

	ret_val = ioctl (fd, VIDIOC_S_FBUF, &fbuf);
	if (ret_val < 0)  {
		LOGE("Error in video VIDIOC_S_FBUF (%d)\n",ret_val);        
		return -1;
	}

	/* 
	 * set destination window
	 */

	sFormat.type 				= V4L2_BUF_TYPE_VIDEO_OVERLAY;
	sFormat.fmt.win.w.left 		= dst->start_x;
	sFormat.fmt.win.w.top 		= dst->start_y;
	sFormat.fmt.win.w.width 	= dst->width;
	sFormat.fmt.win.w.height 	= dst->height;

	ret_val = ioctl(fd, VIDIOC_S_FMT, &sFormat);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_FMT (%d)\n",ret_val);
		return -1;
	}

	return 0;
}

int fimc_v4l2_stream_on(int fd, enum v4l2_buf_type type)
{
	if (-1 == ioctl (fd, VIDIOC_STREAMON, &type)) {
		LOGE("Error in VIDIOC_STREAMON\n");
		return -1;
	}

	return 0;
}

int fimc_v4l2_queue(int fd, struct fimc_buf *fimc_buf)
{
	struct v4l2_buffer	buf;

	buf.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory      = V4L2_MEMORY_USERPTR;
	buf.m.userptr	= (unsigned long)fimc_buf;
	buf.length		= 0;
	buf.index       = 0;

	int	ret_val;

	ret_val = ioctl (fd, VIDIOC_QBUF, &buf);
	if (0 > ret_val) {
		LOGE("Error in VIDIOC_QBUF : (%d) \n", ret_val);
		return -1;
	}

	return 0;
}

int fimc_v4l2_dequeue(int fd)
{
	struct v4l2_buffer          buf;

	buf.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory      = V4L2_MEMORY_USERPTR;

	if (-1 == ioctl (fd, VIDIOC_DQBUF, &buf)) {
		LOGE("Error in VIDIOC_DQBUF\n");
		return -1;
	}

	return buf.index;
}

int fimc_v4l2_stream_off(int fd)
{
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    
	if (-1 == ioctl (fd, VIDIOC_STREAMOFF, &type)) {
		LOGE("Error in VIDIOC_STREAMOFF\n");
		return -1;
	}

	return 0;
}

int fimc_v4l2_clr_buf(int fd)
{
	struct v4l2_requestbuffers req;

	req.count   = 0;
	req.type    = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory  = V4L2_MEMORY_USERPTR;

	if (ioctl (fd, VIDIOC_REQBUFS, &req) == -1) {
		LOGE("Error in VIDIOC_REQBUFS");
	}

	return 0;
}

int fimc_handle_oneshot(int fd, struct fimc_buf *fimc_buf)
{
	int		ret;

	ret = fimc_v4l2_stream_on(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if(ret < 0) {
		LOGE("Fail : v4l2_stream_on()");
		return -1;
	}

	ret = fimc_v4l2_queue(fd, fimc_buf);  
	if(ret < 0) {
		LOGE("Fail : v4l2_queue()\n");
		return -1;
	}

#if 0
	ret = fimc_v4l2_dequeue(fd);
	if(ret < 0) {
		LOGE("Fail : v4l2_dequeue()\n");
		return -1;
	}
#endif

	ret = fimc_v4l2_stream_off(fd);    
	if(ret < 0) {
		LOGE("Fail : v4l2_stream_off()");
		return -1;
	}

	ret = fimc_v4l2_clr_buf(fd);
	if(ret < 0) {
		LOGE("Fail : v4l2_clr_buf()");
		return -1;
	}

	return 0;
}

static int createPP(struct copybit_context_t* ctx)
{	
	struct v4l2_capability 		cap;
	struct v4l2_format			fmt;
	s5p_fimc_t *s5p_fimc 			= &ctx->s5p_fimc;
	s5p_fimc_params_t * params 	= &(s5p_fimc->params);
	s3c_fb_t *s3c_fb 			= &ctx->s3c_fb;
	int		ret, index;
	struct v4l2_control		vc;

	#define  PP_DEVICE_DEV_NAME  "/dev/video1"

	// open device file
	if(s5p_fimc->dev_fd < 0)
		s5p_fimc->dev_fd = open(PP_DEVICE_DEV_NAME, O_RDWR);
	if (0 > s5p_fimc->dev_fd)
	{
		LOGE("%s::Post processor open error (%d)\n", __func__,  errno);
		return -1;
	}

	// check capability
	ret = ioctl(s5p_fimc->dev_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		LOGE("VIDIOC_QUERYCAP failed\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOGE("%d has no streaming support\n", s5p_fimc->dev_fd);
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
		LOGE("%d is no video output\n", s5p_fimc->dev_fd);
		return -1;
	}

	/*
	 * malloc fimc_outinfo structure
	 */
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        LOGE("[%s] Error in video VIDIOC_G_FMT\n", __FUNCTION__);
        return -1;
    }

    vc.id = V4L2_CID_FIMC_VERSION;
    vc.value = 0;

    ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_CTRL, &vc);
    if (ret < 0) {
        LOGE("Error in video VIDIOC_G_CTRL - V4L2_CID_FIMC_VERSION (%d)\n",ret);
        return -1;
    }
	s5p_fimc->hw_ver = vc.value;
	if (0x50 == s5p_fimc->hw_ver)
		ctx->device.common.version = 1;
    LOGI("[%s] fimc version : %x", __func__, s5p_fimc->hw_ver);

	s5p_fimc->use_ext_out_mem = 0;

	return 0;
}

static int destroyPP(struct copybit_context_t *ctx)
{
	s5p_fimc_t *s5p_fimc = &ctx->s5p_fimc;
	
	if(s5p_fimc->out_buf.virt_addr != NULL)
	{
		s5p_fimc->out_buf.virt_addr = NULL;
		s5p_fimc->out_buf.length = 0;
	}

	// close
	if(s5p_fimc->dev_fd >= 0)
	{
		close(s5p_fimc->dev_fd);
		s5p_fimc->dev_fd = -1;
	}
	
	return 0;
}


static int doPP(struct copybit_context_t *ctx,
                unsigned int src_phys_addr, s3c_img *src_img, s3c_rect *src_rect, uint32_t src_color_space,
                unsigned int dst_phys_addr, s3c_img *dst_img, s3c_rect *dst_rect, uint32_t dst_color_space, int rotate_flag)
{
	s5p_fimc_t        * s5p_fimc = &ctx->s5p_fimc;
	s5p_fimc_params_t * params = &(s5p_fimc->params);

	unsigned int	frame_size = 0;
	struct fimc_buf	fimc_src_buf;

	int		src_bpp, src_planes;
	int		rotate_value = rotateValueCopybit2PP(rotate_flag);

	// set post processor configuration
	params->src.full_width  = src_img->width;
	params->src.full_height = src_img->height;
	params->src.start_x     = src_rect->x;
	params->src.start_y     = src_rect->y;
	params->src.width       = widthOfPP(s5p_fimc->hw_ver, src_color_space, src_rect->w);
	params->src.height      = heightOfPP(src_color_space, src_rect->h);
	params->src.color_space = src_color_space;
	params->src.buf_addr_phy_rgb_y 	= src_phys_addr;

	// check minimum
	if (src_rect->w < 16 || src_rect->h < 8) {
		LOGE("%s src size is not supported by fimc : f_w=%d f_h=%d x=%d y=%d w=%d h=%d (ow=%d oh=%d) format=0x%x", __func__,
			params->src.full_width, params->src.full_height, params->src.start_x, params->src.start_y, 
			params->src.width, params->src.height, src_rect->w, src_rect->h, params->src.color_space);
		return -1;
	}

	if (90 == rotate_value || 270 == rotate_value) {
		params->dst.full_width  = dst_img->height;
		params->dst.full_height = dst_img->width;

		params->dst.start_x     = dst_rect->y;
		params->dst.start_y     = dst_rect->x;

		params->dst.width       = widthOfPP(s5p_fimc->hw_ver, dst_color_space, dst_rect->h);
		params->dst.height      = widthOfPP(s5p_fimc->hw_ver, dst_color_space, dst_rect->w);

		if (0x50 != s5p_fimc->hw_ver)
			params->dst.start_y     += (dst_rect->w - params->dst.height);
	} else {
		params->dst.full_width  = dst_img->width;
		params->dst.full_height = dst_img->height;

		params->dst.start_x     = dst_rect->x;
		params->dst.start_y     = dst_rect->y;

		params->dst.width       = widthOfPP(s5p_fimc->hw_ver, dst_color_space, dst_rect->w);
		params->dst.height      = heightOfPP(dst_color_space, dst_rect->h);

	}
	params->dst.color_space = dst_color_space;

	// check minimum
	if (dst_rect->w < 8 || dst_rect->h < 4) {
		LOGE("%s dst size is not supported by fimc : f_w=%d f_h=%d x=%d y=%d w=%d h=%d (ow=%d oh=%d) format=0x%x", __func__, 
			params->dst.full_width, params->dst.full_height, params->dst.start_x, params->dst.start_y, 
			params->dst.width, params->dst.height, dst_rect->w, dst_rect->h, params->dst.color_space);
		return -1;
	}

	/* set configuration related to source (DMA-INPUT)
	 *   - set input format & size 
	 *   - crop input size 
	 *   - set input buffer
	 *   - set buffer type (V4L2_MEMORY_USERPTR)
	 */
	if (fimc_v4l2_set_src(s5p_fimc->dev_fd, s5p_fimc->hw_ver, &params->src) < 0) {
		return -1;
	}

	/* set configuration related to destination (DMA-OUT)
	 *   - set input format & size 
	 *   - crop input size 
	 *   - set input buffer
	 *   - set buffer type (V4L2_MEMORY_USERPTR)
	 */
	if (fimc_v4l2_set_dst(s5p_fimc->dev_fd, &params->dst, rotate_value, dst_phys_addr) < 0) {
		return -1;
	}

	// set input dma address (Y/RGB, Cb, Cr)
	switch (src_img->format) 
	{
	case COPYBIT_FORMAT_CUSTOM_YCbCr_420_SP:
	case COPYBIT_FORMAT_CUSTOM_YCrCb_420_SP:
		// for video contents zero copy case
		fimc_src_buf.base[0]	= params->src.buf_addr_phy_rgb_y;
		fimc_src_buf.base[1]	= params->src.buf_addr_phy_cb; 
		break;

	case COPYBIT_FORMAT_CUSTOM_YCbCr_422_I:
	case COPYBIT_FORMAT_CUSTOM_CbYCrY_422_I:
	case COPYBIT_FORMAT_RGB_565:
		// for camera capture zero copy case
		fimc_src_buf.base[0]	= params->src.buf_addr_phy_rgb_y;

	default:
		// set source image
		src_bpp		= get_yuv_bpp(src_color_space);
		src_planes 	= get_yuv_planes(src_color_space);

		fimc_src_buf.base[0]	= params->src.buf_addr_phy_rgb_y;

		if (2 == src_planes)
		{
			frame_size = params->src.full_width * params->src.full_height;
			params->src.buf_addr_phy_cb		= params->src.buf_addr_phy_rgb_y + frame_size;

			fimc_src_buf.base[1]	= params->src.buf_addr_phy_cb; 
		}
		else if (3 == src_planes)
		{
			frame_size = params->src.full_width * params->src.full_height;
			params->src.buf_addr_phy_cb		= params->src.buf_addr_phy_rgb_y + frame_size;

			if (12 == src_bpp)
				 params->src.buf_addr_phy_cr = params->src.buf_addr_phy_cb + (frame_size >> 2);
			else
				 params->src.buf_addr_phy_cr = params->src.buf_addr_phy_cb + (frame_size >> 1);
	
			fimc_src_buf.base[1]	= params->src.buf_addr_phy_cb; 
			fimc_src_buf.base[2]	= params->src.buf_addr_phy_cr; 
		}
	}

	if (fimc_handle_oneshot(s5p_fimc->dev_fd, &fimc_src_buf) < 0) {
		fimc_v4l2_clr_buf(s5p_fimc->dev_fd);
		return -1;
	}

	return 0;
}

#ifdef	USE_HW_PMEM
static int initPmem(struct copybit_context_t *ctx)
{
	int		master_fd, err = 0, i;
	void 	*base;
	unsigned int phys_base;
    size_t 	size, sub_size[NUM_OF_MEMORY_OBJECT];
   	struct pmem_region region;
	sec_pmem_t	*pm	= &(ctx->sec_pmem);

	#define	PMEM_DEVICE_DEV_NAME	"/dev/pmem_gpu1"

    master_fd = open(PMEM_DEVICE_DEV_NAME, O_RDWR, 0);
    if (master_fd < 0) 
	{
		pm->pmem_master_fd = -1;
		if (EACCES == errno) 
			return 0;
		else
		{
			LOGE("%s::open(%s) fail(%s)\n", __func__, PMEM_DEVICE_DEV_NAME, strerror(errno));
			return -errno;
		}
	}

    if (ioctl(master_fd, PMEM_GET_TOTAL_SIZE, &region) < 0) 
	{
        LOGE("PMEM_GET_TOTAL_SIZE failed, limp mode");
        size = 8<<20;   // 8 MiB
    } else {
        size = region.len;
    }

    base = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, master_fd, 0);
    if (base == MAP_FAILED) 
	{
        LOGE("[%s] mmap failed : %d (%s)", __func__, errno, strerror(errno));
        base = 0;
        close(master_fd);
        master_fd = -1;
		return -errno;
    }

    if (ioctl(master_fd, PMEM_GET_PHYS, &region) < 0) 
	{
        LOGE("PMEM_GET_PHYS failed, limp mode");
		region.offset = 0;
    }

    pm->pmem_master_fd 		= master_fd;
    pm->pmem_master_base 	= base;
	pm->pmem_total_size 	= size;
	//pm->pmem_master_phys_base	= region.offset;
	phys_base = region.offset;

	// sec_pmem_alloc[1] for temporary buffer for destination
	sub_size[1] = (ctx->s3c_fb.width * ctx->s3c_fb.height * (ctx->s3c_fb.bpp / 8));
   	sub_size[1] = roundUpToPageSize(sub_size[1]);

	// sec_pmem_alloc[0] for temporary buffer for source
	sub_size[0] = size - sub_size[1];
   	sub_size[0] = roundUpToPageSize(sub_size[0]);

	for (i=0; i<NUM_OF_MEMORY_OBJECT; i++)
	{
		sec_pmem_alloc_t	*pm_alloc = &(pm->sec_pmem_alloc[i]);
		int fd, ret;
		int offset	= i?sub_size[i-1]:0; 
        struct pmem_region sub = { offset, sub_size[i] };

		// create the "sub-heap"
		if(0 > (fd = open(PMEM_DEVICE_DEV_NAME, O_RDWR, 0)))
		{
        	LOGE("[%s][index=%d] open failed (%dL) : %d (%s)", __func__, i, __LINE__, errno, strerror(errno));
        	return -errno;
		}

		// connect to it
		if(0 != (ret = ioctl(fd, PMEM_CONNECT, pm->pmem_master_fd)))
		{
        	LOGE("[%s][index=%d] ioctl(PMEM_CONNECT) failed : %d (%s)", __func__, i, errno, strerror(errno));
        	return -errno;
		}

		// make it available to the client process
		if(0 != (ret = ioctl(fd, PMEM_MAP, &sub)))
		{
        	LOGE("[%s][index=%d] ioctl(PMEM_MAP) failed : %d (%s)", __func__, i, errno, strerror(errno));
        	return -errno;
		}

		pm_alloc->fd		= fd;
		pm_alloc->total_size= sub_size[i];
		pm_alloc->offset	= offset;
		pm_alloc->virt_addr	= (unsigned int)base + (unsigned int)offset;
		pm_alloc->phys_addr = (unsigned int)phys_base + (unsigned int)offset;
	}

	return err;
}

static int destroyPmem(struct copybit_context_t *ctx)
{
	int	i, err;
	sec_pmem_t	*pm	= &(ctx->sec_pmem);

	for (i=0; i<NUM_OF_MEMORY_OBJECT; i++)
	{
		sec_pmem_alloc_t	*pm_alloc = &(pm->sec_pmem_alloc[i]);

		if (0 <= pm_alloc->fd)
		{
    		struct pmem_region sub = { pm_alloc->offset, pm_alloc->total_size };
			
			if(0 > (err = ioctl(pm_alloc->fd, PMEM_UNMAP, &sub)))
			{
				LOGE("[%s][index=%d] ioctl(PMEM_UNMAP) failed : %d (%s)\n", __func__, i, errno, strerror(errno));
			}

			close(pm_alloc->fd);

			pm_alloc->fd		= -1;
			pm_alloc->total_size= 0;
			pm_alloc->offset	= 0;
			pm_alloc->virt_addr	= 0;
			pm_alloc->phys_addr = 0;
		}
	}

	if (0 <= pm->pmem_master_fd)
	{
		munmap(pm->pmem_master_base, pm->pmem_total_size);
		close(pm->pmem_master_fd);
    	pm->pmem_master_fd 		= -1;
	}

	pm->pmem_master_base		= 0;
	pm->pmem_total_size			= 0;

	return 0;
}

int checkPmem(struct copybit_context_t *ctx, unsigned int index, unsigned int requested_size)
{
	sec_pmem_alloc_t	*pm_alloc	= &(ctx->sec_pmem.sec_pmem_alloc[index]);

	if (0 < pm_alloc->virt_addr && requested_size <= (unsigned int)(pm_alloc->total_size))
		return 0;

	pm_alloc->size	= 0;
	return -1;
}
#endif

static int createMem(struct copybit_context_t *ctx, unsigned int index, unsigned int memory_size)
{
	#define S3C_MEM_DEV_NAME "/dev/s3c-mem"

	struct s3c_mem_alloc *s3c_mem;
	struct s3c_mem_alloc mem_alloc_info;

	if (index >= NUM_OF_MEMORY_OBJECT) {
		LOGE("%s::invalid index (%d >= %d)\n", __func__, index, NUM_OF_MEMORY_OBJECT);
		return -1;
	}

	s3c_mem = &ctx->s3c_mem.mem_alloc[index];

	if(ctx->s3c_mem.dev_fd < 0) {
		ctx->s3c_mem.dev_fd = open(S3C_MEM_DEV_NAME, O_RDWR);

    	if(ctx->s3c_mem.dev_fd < 0) {
    		LOGE("%s::open(%s) fail(%s)\n", __func__, S3C_MEM_DEV_NAME, strerror(errno));
    		ctx->s3c_mem.dev_fd = -1;
    		return -1;
    	}
	}
	
	if (0 == memory_size) 
		return 0;

	mem_alloc_info.size = memory_size;

	if(ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_CACHEABLE_ALLOC, &mem_alloc_info) < 0) {	
		LOGE("%s::S3C_MEM_ALLOC(size : %d)  fail\n", __func__, mem_alloc_info.size);
		return -1;
	}

	s3c_mem->phy_addr = mem_alloc_info.phy_addr;
	s3c_mem->vir_addr = mem_alloc_info.vir_addr;
	s3c_mem->size     = mem_alloc_info.size;
		
	return 0;
}

static int destroyMem(struct copybit_context_t *ctx)
{
	int i;
	struct s3c_mem_alloc *s3c_mem;

	if(0 > ctx->s3c_mem.dev_fd) return 0;

	for (i = 0; i < NUM_OF_MEMORY_OBJECT; i++) {
	    s3c_mem = &ctx->s3c_mem.mem_alloc[i];

		if (0 != s3c_mem->vir_addr) {
			if (ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_FREE, s3c_mem) < 0) {
				LOGE("%s::S3C_MEM_FREE fail\n", __func__);
				return -1;
			}

			s3c_mem->phy_addr = 0;
			s3c_mem->vir_addr = 0;
			s3c_mem->size     = 0;
		}
	}
	
	close(ctx->s3c_mem.dev_fd);
	ctx->s3c_mem.dev_fd = -1;

	return 0;
}

int checkMem(struct copybit_context_t *ctx, unsigned int index, unsigned int requested_size)
{
	int ret;
	struct s3c_mem_alloc *s3c_mem;
	struct s3c_mem_alloc mem_alloc_info;

	if (index >= NUM_OF_MEMORY_OBJECT) {
		LOGE("%s::invalid index (%d >= %d)\n", __func__, index, NUM_OF_MEMORY_OBJECT);
		return -1;
	}

	if (ctx->s3c_mem.dev_fd < 0) {
		ret = createMem(ctx, index, requested_size);
		return ret;
	}

	s3c_mem = &ctx->s3c_mem.mem_alloc[index];

	if (s3c_mem->size < requested_size)
	{
		if (0 < s3c_mem->size) {
		    // free allocated mem
		    if (ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_FREE, s3c_mem) < 0) {
		    	LOGE("%s::S3C_MEM_FREE fail\n", __func__);
		    	return -1;
		    }
		}

		// allocate mem with requested size
		mem_alloc_info.size = requested_size;
		if(ioctl(ctx->s3c_mem.dev_fd, S3C_MEM_CACHEABLE_ALLOC, &mem_alloc_info) < 0) {	
			LOGE("%s::S3C_MEM_ALLOC(size : %d)  fail\n", __func__, mem_alloc_info.size);
			return -1;
		}
	
		s3c_mem->phy_addr = mem_alloc_info.phy_addr;
		s3c_mem->vir_addr = mem_alloc_info.vir_addr;
		s3c_mem->size     = mem_alloc_info.size;
	}

	return 0;
}

static unsigned int get_yuv_bpp(unsigned int fmt)
{
	int i, sel = -1;

	for (i = 0; i < (int)(sizeof(yuv_list) / sizeof(struct yuv_fmt_list)); i++) {
		if (yuv_list[i].fmt == fmt) {
			sel = i;
			break;
		}
	}

	if (sel == -1)
		return sel;
	else
		return yuv_list[sel].bpp;
}

static unsigned int get_yuv_planes(unsigned int fmt)
{
	int i, sel = -1;

	for (i = 0; i < (int)(sizeof(yuv_list) / sizeof(struct yuv_fmt_list)); i++) {
		if (yuv_list[i].fmt == fmt) {
			sel = i;
			break;
		}
	}

	if (sel == -1)
		return sel;
	else
		return yuv_list[sel].planes;
}

static inline int colorFormatCopybit2PP(int format)
{
	switch (format) 
	{
		// rbg
		case COPYBIT_FORMAT_RGBA_8888:    			return V4L2_PIX_FMT_RGB32;
		case COPYBIT_FORMAT_RGBX_8888:    			return V4L2_PIX_FMT_RGB32;
		case COPYBIT_FORMAT_BGRA_8888:    			return V4L2_PIX_FMT_RGB32;
		case COPYBIT_FORMAT_RGB_565:      			return V4L2_PIX_FMT_RGB565;		

		// 422 / 420 2 plane										
		case COPYBIT_FORMAT_YCbCr_422_SP:			return V4L2_PIX_FMT_NV16;
		case COPYBIT_FORMAT_YCrCb_422_SP:			return V4L2_PIX_FMT_NV61;
		case COPYBIT_FORMAT_YCbCr_420_SP: 			return V4L2_PIX_FMT_NV12;
		case COPYBIT_FORMAT_YCrCb_420_SP: 			return V4L2_PIX_FMT_NV21;

		// 422 / 420 3 plane
		case COPYBIT_FORMAT_YCbCr_422_P:			return V4L2_PIX_FMT_YUV422P;
		case COPYBIT_FORMAT_YCbCr_420_P: 			return V4L2_PIX_FMT_YUV420;

		// 422 1 plane
		case COPYBIT_FORMAT_YCbCr_422_I: 			return V4L2_PIX_FMT_YUYV;
		case COPYBIT_FORMAT_CbYCrY_422_I: 			return V4L2_PIX_FMT_UYVY;

		// customed format
		case COPYBIT_FORMAT_CUSTOM_CbYCrY_422_I:	return V4L2_PIX_FMT_UYVY; //Kamat
		case COPYBIT_FORMAT_CUSTOM_YCbCr_422_I:		return V4L2_PIX_FMT_YUYV;
		case COPYBIT_FORMAT_CUSTOM_YCbCr_420_SP: 	return V4L2_PIX_FMT_NV12T;
		case COPYBIT_FORMAT_CUSTOM_YCrCb_420_SP: 	return V4L2_PIX_FMT_NV21; //Kamat

		// unsupported format by fimc
		case COPYBIT_FORMAT_RGB_888:
		case COPYBIT_FORMAT_RGBA_5551:
		case COPYBIT_FORMAT_RGBA_4444:
		case COPYBIT_FORMAT_YCbCr_420_I:
		case COPYBIT_FORMAT_CbYCrY_420_I:
		default :
			LOGE("%s::not matched frame format : %d\n", __func__, format);
			break;
	}
    return -1;
}

static int fimc_v4l2_streamon(int fp)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

	ret = ioctl(fp, VIDIOC_STREAMON, &type);
	if (ret < 0) {
    		LOGE("VIDIOC_STREAMON failed\n");
    		return -1;
  	}

	return ret;
}

static int fimc_v4l2_streamoff(int fp)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

	ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
    		LOGE("VIDIOC_STREAMOFF failed\n");
    		return -1;
  	}

	return ret;
}

static int fimc_v4l2_start_overlay(int fd)
{
    int ret, start = 1;

    ret = ioctl(fd, VIDIOC_OVERLAY, &start);
    if (ret < 0)
    {
        LOGE("fimc_v4l2_start_overlay() VIDIOC_OVERLAY failed\n");
        return -1;
    }

    LOGV("fimc_v4l2_start_overlay() success\n");
    return ret;
}

static int fimc_v4l2_stop_overlay(int fd)
{
    int ret, start = 0;

    ret = ioctl(fd, VIDIOC_OVERLAY, &start);
    if (ret < 0)
    {
        LOGE("fimc_v4l2_stop_overlay() VIDIOC_OVERLAY failed\n");
        return -1;
    }

    LOGV("fimc_v4l2_stop_overlay() success\n");
    return ret;
}

static int fimc_v4l2_s_crop(int fp, int target_width, int target_height, int h_offset, int v_offset)
{
	struct v4l2_crop crop;
	int ret;

	crop.type 		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.left 	= h_offset;
	crop.c.top 		= v_offset;
	crop.c.width 	= target_width;
	crop.c.height 	= target_height;
		

	ret = ioctl(fp, VIDIOC_S_CROP, &crop);
	if (ret < 0) {
		LOGE("VIDIOC_S_crop failed\n");
		return -1;
	}

	return ret;
}

static inline unsigned int getFrameSize(int colorformat, int width, int height)
{
	unsigned int frame_size = 0;
	unsigned int size = width * height;

	switch(colorformat)
	{
		case COPYBIT_FORMAT_RGBA_8888: 
		case COPYBIT_FORMAT_BGRA_8888: 		
			frame_size = width * height * 4;
			break;

		case COPYBIT_FORMAT_YCbCr_420_SP: 
		case COPYBIT_FORMAT_YCrCb_420_SP: 
		case COPYBIT_FORMAT_CUSTOM_YCbCr_420_SP: 
		case COPYBIT_FORMAT_CUSTOM_YCrCb_420_SP: //Kamat (30-3-2010)
		case COPYBIT_FORMAT_YCbCr_420_P: 
			//frame_size = width * height * 3 / 2;
			frame_size = size + (2 * ( size / 4));
			break;

		case COPYBIT_FORMAT_RGB_565: 
		case COPYBIT_FORMAT_RGBA_5551: 
		case COPYBIT_FORMAT_RGBA_4444: 
		case COPYBIT_FORMAT_YCbCr_422_I :
		case COPYBIT_FORMAT_YCbCr_422_SP :
		case COPYBIT_FORMAT_CUSTOM_YCbCr_422_I :
		case COPYBIT_FORMAT_CbYCrY_422_I : //Kamat
		case COPYBIT_FORMAT_CUSTOM_CbYCrY_422_I : //Kamat
			frame_size = width * height * 2;
			break;

		default :  // hell.~~
			LOGE("%s::no matching source colorformat(%d), width(%d), height(%d) \n",
			     __func__, colorformat, width, height);
			frame_size = 0;
			break;
	}
	return frame_size;
}

static inline int rotateValueCopybit2PP(unsigned char flags)
{
	int rotate_flag = flags & 0x7;

	switch (rotate_flag)
	{
		case COPYBIT_TRANSFORM_ROT_90: 	return 90;
		case COPYBIT_TRANSFORM_ROT_180: return 180;
		case COPYBIT_TRANSFORM_ROT_270: return 270;
	}
	return 0;
}

static inline int heightOfPP(int pp_color_format, int number)
{
	switch(pp_color_format)
	{
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV12T:
		case V4L2_PIX_FMT_YUV420: 
			return multipleOf2(number);

		default :
			return number;
			break;
	}
	return number;
}

static inline int widthOfPP(unsigned int ver, int pp_color_format, int number)
{
	if (0x50 == ver)
	{
		switch(pp_color_format)
		{
			/* 422 1/2/3 plane */
			case V4L2_PIX_FMT_YUYV:
			case V4L2_PIX_FMT_UYVY:
			case V4L2_PIX_FMT_NV61:
			case V4L2_PIX_FMT_NV16:
			case V4L2_PIX_FMT_YUV422P:
	
			/* 420 2/3 plane */
			case V4L2_PIX_FMT_NV21:
			case V4L2_PIX_FMT_NV12:
			case V4L2_PIX_FMT_NV12T:
			case V4L2_PIX_FMT_YUV420:
				return multipleOf2(number);
	
			default :
				return number;
		}
	}
	else
	{
		switch(pp_color_format)
		{
			case V4L2_PIX_FMT_RGB565:
				return multipleOf8(number);
	
			case V4L2_PIX_FMT_RGB32:
				return multipleOf4(number);
	
			case V4L2_PIX_FMT_YUYV:
			case V4L2_PIX_FMT_UYVY:
				return multipleOf4(number);
	
			case V4L2_PIX_FMT_NV61:
			case V4L2_PIX_FMT_NV16:
				return multipleOf8(number);
	
			case V4L2_PIX_FMT_YUV422P:
				return multipleOf16(number);
	
			case V4L2_PIX_FMT_NV21:
			case V4L2_PIX_FMT_NV12:
			case V4L2_PIX_FMT_NV12T:
				return multipleOf8(number);
	
			case V4L2_PIX_FMT_YUV420: 
				return multipleOf16(number);
	
			default :
				return number;
		}
	}
	return number;
}

static inline int multipleOf2(int number)
{
	if(number % 2 == 1)
		return (number - 1);
	else
		return number;
}

static inline int multipleOf4(int number)
{
	int remain_number = number % 4;

	if(remain_number != 0)
		return (number - remain_number);
	else
		return number;
}

static inline int multipleOf8(int number)
{
	int remain_number = number % 8;

	if(remain_number != 0)
		return (number - remain_number);
	else
		return number;
}

static inline int multipleOf16(int number)
{
	int remain_number = number % 16;

	if(remain_number != 0)
		return (number - remain_number);
	else
		return number;
}

static int can_support_rgb(struct copybit_context_t *ctx, int32_t format)
{
	switch(format)
	{
		//case COPYBIT_FORMAT_RGB_565:
		case COPYBIT_FORMAT_RGBA_8888:
		case COPYBIT_FORMAT_RGBX_8888:
		case COPYBIT_FORMAT_BGRA_8888:
		case COPYBIT_FORMAT_RGBA_5551:
		case COPYBIT_FORMAT_RGBA_4444:
		case COPYBIT_FORMAT_RGB_888:
			return -1;

		// yuv case
		default:
			return 0;
	}
	return -1;
}
