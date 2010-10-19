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

/* #define OVERLAY_DEBUG 1 */
#define LOG_TAG "v4l2_utils"

#include <fcntl.h>
#include <errno.h>
#include <cutils/log.h>
#include <hardware/overlay.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "v4l2_utils.h"


#define LOG_FUNCTION_NAME    LOGV("%s: %s",  __FILE__, __func__);

#define V4L2_CID_PRIV_OFFSET         0x0
#define V4L2_CID_PRIV_ROTATION       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 0)
#define V4L2_CID_PRIV_COLORKEY       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 1)
#define V4L2_CID_PRIV_COLORKEY_EN    (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 2)

extern unsigned int g_lcd_width;
extern unsigned int g_lcd_height;
extern unsigned int g_lcd_bpp;

int v4l2_overlay_get(int name)
{
    int result = -1;
    switch (name) {
    case OVERLAY_MINIFICATION_LIMIT:
        result = 4; /* 0 = no limit */
        break;
    case OVERLAY_MAGNIFICATION_LIMIT:
        result = 2; /* 0 = no limit */
        break;
    case OVERLAY_SCALING_FRAC_BITS:
        result = 0; /* 0 = infinite */
        break;
    case OVERLAY_ROTATION_STEP_DEG:
        result = 90; /* 90 rotation steps (for instance) */
        break;
    case OVERLAY_HORIZONTAL_ALIGNMENT:
        result = 1; /* 1-pixel alignment */
        break;
    case OVERLAY_VERTICAL_ALIGNMENT:
        result = 1; /* 1-pixel alignment */
        break;
    case OVERLAY_WIDTH_ALIGNMENT:
        result = 1; /* 1-pixel alignment */
        break;
    case OVERLAY_HEIGHT_ALIGNMENT:
        result = 1; /* 1-pixel alignment */
        break;
    }
    return result;
}

int v4l2_overlay_open(int id)
{
    LOG_FUNCTION_NAME
    return open("/dev/video1", O_RDWR);
}

int v4l2_overlay_init_fimc(int fd, s5p_fimc_t *s5p_fimc)
{
    int ret;
    struct v4l2_control    vc;

    if (fd < 0)
        return -1;

    vc.id = V4L2_CID_FIMC_VERSION;
    vc.value = 0;

    s5p_fimc->dev_fd = fd;

    ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_CTRL, &vc);
    if (ret < 0) {
        LOGE("Error in video VIDIOC_G_CTRL - V4L2_CID_FIMC_VERSION (%d)", ret);
        LOGE("FIMC version is set with default");
        vc.value = 0x43;
    }
    s5p_fimc->hw_ver = vc.value;
    return 0;
}

void dump_pixfmt(struct v4l2_pix_format *pix)
{
    LOGV("w: %d\n", pix->width);
    LOGV("h: %d\n", pix->height);
    LOGV("color: %x\n", pix->colorspace);

    switch (pix->pixelformat) {
    case V4L2_PIX_FMT_YUYV:
        LOGV("YUYV\n");
        break;
    case V4L2_PIX_FMT_UYVY:
        LOGV("UYVY\n");
        break;
    case V4L2_PIX_FMT_RGB565:
        LOGV("RGB565\n");
        break;
    case V4L2_PIX_FMT_RGB565X:
        LOGV("RGB565X\n");
        break;
    default:
        LOGV("not supported\n");
    }
}

void dump_crop(struct v4l2_crop *crop)
{
    LOGV("crop l: %d ", crop->c.left);
    LOGV("crop t: %d ", crop->c.top);
    LOGV("crop w: %d ", crop->c.width);
    LOGV("crop h: %d\n", crop->c.height);
}

void dump_window(struct v4l2_window *win)
{
    LOGV("window l: %d ", win->w.left);
    LOGV("window t: %d ", win->w.top);
    LOGV("window w: %d ", win->w.width);
    LOGV("window h: %d\n", win->w.height);
}

void v4l2_overlay_dump_state(int fd)
{
    struct v4l2_format format;
    struct v4l2_crop crop;
    int ret;

    LOGV("dumping driver state:");
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if (ret < 0)
        return;
    LOGV("output pixfmt:\n");
    dump_pixfmt(&format.fmt.pix);

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if (ret < 0)
        return;
    LOGV("v4l2_overlay window:\n");
    dump_window(&format.fmt.win);

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(fd, VIDIOC_G_CROP, &crop);
    if (ret < 0)
        return;
    LOGV("output crop:\n");
    dump_crop(&crop);
}

static void error(int fd, const char *msg)
{
    LOGE("Error = %s from %s", strerror(errno), msg);
#ifdef OVERLAY_DEBUG
    v4l2_overlay_dump_state(fd);
#endif
}

static int v4l2_overlay_ioctl(int fd, int req, void *arg, const char* msg)
{
    int ret;
    ret = ioctl(fd, req, arg);
    if (ret < 0) {
        error(fd, msg);
        return -1;
    }
    return 0;
}

#define v4l2_fourcc(a, b, c, d)  ((__u32)(a) | ((__u32)(b) << 8) | \
                                 ((__u32)(c) << 16) | ((__u32)(d) << 24))
/* 12  Y/CbCr 4:2:0 64x32 macroblocks */
#define V4L2_PIX_FMT_NV12T       v4l2_fourcc('T', 'V', '1', '2')

int configure_pixfmt(struct v4l2_pix_format *pix, int32_t fmt,
        uint32_t w, uint32_t h)
{
    LOG_FUNCTION_NAME
    int fd;

    switch (fmt) {
    case OVERLAY_FORMAT_RGBA_8888:
        return -1;
    case OVERLAY_FORMAT_RGB_565:
        pix->pixelformat = V4L2_PIX_FMT_RGB565;
        break;
    case OVERLAY_FORMAT_BGRA_8888:
        return -1;
    case OVERLAY_FORMAT_YCbYCr_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I:
        pix->pixelformat = V4L2_PIX_FMT_YUYV;
        break;
    case OVERLAY_FORMAT_CbYCrY_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I:
        pix->pixelformat = V4L2_PIX_FMT_UYVY;
        break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
        pix->pixelformat = V4L2_PIX_FMT_YUV420;
        break;
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP:
        pix->pixelformat = V4L2_PIX_FMT_NV12T;
        break;
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP:
        pix->pixelformat = V4L2_PIX_FMT_NV21;
        break;
    default:
        return -1;
    }
    pix->width = w;
    pix->height = h;
    return 0;
}

static void configure_window(struct v4l2_window *win, int32_t w,
        int32_t h, int32_t x, int32_t y)
{
    LOG_FUNCTION_NAME

    win->w.left = x;
    win->w.top = y;
    win->w.width = w;
    win->w.height = h;
}

void get_window(struct v4l2_format *format, int32_t *x,
        int32_t *y, int32_t *w, int32_t *h)
{
    LOG_FUNCTION_NAME

    *x = format->fmt.win.w.left;
    *y = format->fmt.win.w.top;
    *w = format->fmt.win.w.width;
    *h = format->fmt.win.w.height;
}

int v4l2_overlay_init(int fd, uint32_t w, uint32_t h, uint32_t fmt,
                      uint32_t addr)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    struct v4l2_framebuffer fbuf;
    int ret;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format, "get format");
    if (ret)
        return ret;
    LOGV("v4l2_overlay_init:: w=%d h=%d\n", format.fmt.pix.width,
         format.fmt.pix.height);

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    configure_pixfmt(&format.fmt.pix, fmt, w, h);
    LOGV("v4l2_overlay_init:: w=%d h=%d\n", format.fmt.pix.width,
         format.fmt.pix.height);
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format, "set output format");
    if (ret)
        return ret;

    ret = v4l2_overlay_s_fbuf(fd, 0);
    if (ret)
        return ret;

    return ret;
}

int v4l2_overlay_s_fbuf(int fd, int rotation)
{
    struct v4l2_framebuffer fbuf;
    int ret;

    /* configure the v4l2_overlay framebuffer */
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FBUF, &fbuf, "get fbuf");
    if (ret)
        return ret;

    /* if fbuf.base value is set by 0, using local DMA. */
    fbuf.base             = (void *)0;
    if (rotation == 0 || rotation == 180) {
        fbuf.fmt.width         = g_lcd_width;
        fbuf.fmt.height     = g_lcd_height;
    } else {
        fbuf.fmt.width         = g_lcd_height;
        fbuf.fmt.height     = g_lcd_width;
    }

    if (g_lcd_bpp == 32)
        fbuf.fmt.pixelformat     = V4L2_PIX_FMT_RGB32;
    else
        fbuf.fmt.pixelformat     = V4L2_PIX_FMT_RGB565;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FBUF, &fbuf, "set fbuf");
    if (ret)
        return ret;

    return ret;
}

int v4l2_overlay_get_input_size_and_format(int fd, uint32_t *w, uint32_t *h
                                                 , uint32_t *fmt)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    int ret;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format, "get format");
    *w = format.fmt.pix.width;
    *h = format.fmt.pix.height;
    if (format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        *fmt = OVERLAY_FORMAT_CbYCrY_422_I;
    else
        return -EINVAL;
    return ret;
}

int v4l2_overlay_set_position(int fd, int32_t x, int32_t y
                                    , int32_t w, int32_t h, int rotation)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    int ret;
    int rot_x = 0, rot_y = 0 , rot_w = 0, rot_h = 0;

    /* configure the src format pix */
    /* configure the dst v4l2_overlay window */
    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format,
            "get v4l2_overlay format");
    if (ret)
        return ret;
    LOGV("v4l2_overlay_set_position:: w=%d h=%d", format.fmt.win.w.width
                                                , format.fmt.win.w.height);

    if (rotation == 0) {
        rot_x = x;
        rot_y = y;
        rot_w = w;
        rot_h = h;
    } else if (rotation == 90) {
        rot_x = y;
        rot_y = g_lcd_width - (x + w);
        rot_w = h;
        rot_h = w;
    } else if (rotation == 180) {
        rot_x = g_lcd_width - (x + w);
        rot_y = g_lcd_height - (y + h);
        rot_w = w;
        rot_h = h;
    } else if (rotation == 270) {
        rot_x = g_lcd_height - (y + h);
        rot_y = x;
        rot_w = h;
        rot_h = w;
    }

    configure_window(&format.fmt.win, rot_w, rot_h, rot_x, rot_y);

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format,
            "set v4l2_overlay format");

    LOGV("v4l2_overlay_set_position:: w=%d h=%d rotation=%d"
                 , format.fmt.win.w.width, format.fmt.win.w.height, rotation);

    if (ret)
        return ret;
    v4l2_overlay_dump_state(fd);

    return 0;
}

int v4l2_overlay_get_position(int fd, int32_t *x, int32_t *y, int32_t *w,
                              int32_t *h)
{
    struct v4l2_format format;
    int ret;

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format,
                                 "get v4l2_overlay format");

    if (ret)
        return ret;

    get_window(&format, x, y, w, h);

    return 0;
}

int v4l2_overlay_set_crop(int fd, uint32_t x, uint32_t y, uint32_t w,
                          uint32_t h)
{
    LOG_FUNCTION_NAME

    struct v4l2_crop crop;
    int ret;

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_CROP, &crop, "get crop");
    crop.c.left = x;
    crop.c.top = y;
    crop.c.width = w;
    crop.c.height = h;
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    LOGV("%s:crop.c.left = %d\n", __func__, crop.c.left);
    LOGV("%s:crop.c.top = %d\n", __func__, crop.c.top);
    LOGV("%s:crop.c.width = %d\n", __func__, crop.c.width);
    LOGV("%s:crop.c.height = %d\n", __func__, crop.c.height);
    LOGV("%s:crop.type = 0x%x\n", __func__, crop.type);

    return v4l2_overlay_ioctl(fd, VIDIOC_S_CROP, &crop, "set crop");
}

int v4l2_overlay_get_crop(int fd, uint32_t *x, uint32_t *y, uint32_t *w,
                          uint32_t *h)
{
    LOG_FUNCTION_NAME

    struct v4l2_crop crop;
    int ret;

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_CROP, &crop, "get crop");
    *x = crop.c.left;
    *y = crop.c.top;
    *w = crop.c.width;
    *h = crop.c.height;
    return ret;
}

int v4l2_overlay_set_flip(int fd, int flip)
{
    LOG_FUNCTION_NAME

    int ret;
    struct v4l2_control ctrl_v;
    struct v4l2_control ctrl_h;

    switch (flip) {
    case 0:
        ctrl_v.value = 0;
        ctrl_h.value = 0;
        break;
    case V4L2_CID_HFLIP:
        ctrl_v.value = 0;
        ctrl_h.value = 1;
        break;
    case V4L2_CID_VFLIP:
        ctrl_v.value = 1;
        ctrl_h.value = 0;
        break;
    default:
        return -1;
    }

    ctrl_v.id = V4L2_CID_VFLIP;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_CTRL, &ctrl_v, "set vflip");
    if (ret) return ret;

    ctrl_h.id = V4L2_CID_HFLIP;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_CTRL, &ctrl_h, "set hflip");
    return ret;
}

int v4l2_overlay_set_rotation(int fd, int degree, int step)
{
    LOG_FUNCTION_NAME

    int ret;
    struct v4l2_control ctrl;

    ctrl.id = V4L2_CID_PRIV_ROTATION;
    ctrl.value = degree;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_CTRL, &ctrl, "set rotation");

    return ret;
}

int v4l2_overlay_set_colorkey(int fd, int enable, int colorkey)
{
    LOG_FUNCTION_NAME

    int ret;
    struct v4l2_framebuffer fbuf;
    struct v4l2_format fmt;

    memset(&fbuf, 0, sizeof(fbuf));
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FBUF, &fbuf,
                             "get transparency enables");

    if (ret)
        return ret;

    if (enable)
        fbuf.flags |= V4L2_FBUF_FLAG_CHROMAKEY;
    else
        fbuf.flags &= ~V4L2_FBUF_FLAG_CHROMAKEY;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FBUF, &fbuf, "enable colorkey");

    if (ret)
        return ret;

    if (enable) {
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
        ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &fmt, "get colorkey");

        if (ret)
            return ret;

        fmt.fmt.win.chromakey = colorkey & 0xFFFFFF;

        ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &fmt, "set colorkey");
    }

    return ret;
}

int v4l2_overlay_set_global_alpha(int fd, int enable, int alpha)
{
    LOG_FUNCTION_NAME

    int ret;
    struct v4l2_framebuffer fbuf;
    struct v4l2_format fmt;

    memset(&fbuf, 0, sizeof(fbuf));
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FBUF, &fbuf,
                             "get transparency enables");

    if (ret)
        return ret;

    if (enable)
        fbuf.flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;
    else
        fbuf.flags &= ~V4L2_FBUF_FLAG_GLOBAL_ALPHA;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FBUF, &fbuf, "enable global alpha");

    if (ret)
        return ret;

    if (enable) {
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
        ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &fmt, "get global alpha");

        if (ret)
            return ret;

        fmt.fmt.win.global_alpha = alpha & 0xFF;

        ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &fmt, "set global alpha");
    }

    return ret;
}

int v4l2_overlay_set_local_alpha(int fd, int enable)
{
    int ret;
    struct v4l2_framebuffer fbuf;

    ret = 0;

    return ret;
}

int v4l2_overlay_req_buf(int fd, uint32_t *num_bufs, int cacheable_buffers, int zerocopy)
{
    struct v4l2_requestbuffers reqbuf;
    int ret, i;

    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (zerocopy)
        reqbuf.memory = V4L2_MEMORY_USERPTR;
    else
        reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = *num_bufs;

    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) {
        error(fd, "reqbuf ioctl");
        return ret;
    }

    if (reqbuf.count > *num_bufs) {
        error(fd, "Not enough buffer structs passed to get_buffers");
        return -ENOMEM;
    }
    *num_bufs = reqbuf.count;

    return 0;
}

static int is_mmaped(struct v4l2_buffer *buf)
{
    return buf->flags == V4L2_BUF_FLAG_MAPPED;
}

static int is_queued(struct v4l2_buffer *buf)
{
    /* is either on the input or output queue in the kernel */
    return (buf->flags & V4L2_BUF_FLAG_QUEUED) ||
        (buf->flags & V4L2_BUF_FLAG_DONE);
}

static int is_dequeued(struct v4l2_buffer *buf)
{
    /* is on neither input or output queue in kernel */
    return !(buf->flags & V4L2_BUF_FLAG_QUEUED) &&
            !(buf->flags & V4L2_BUF_FLAG_DONE);
}

int v4l2_overlay_query_buffer(int fd, int index, struct v4l2_buffer *buf)
{
    LOG_FUNCTION_NAME

    memset(buf, 0, sizeof(struct v4l2_buffer));

    buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf->memory = V4L2_MEMORY_MMAP;
    buf->index = index;
    LOGV("query buffer, mem=%u type=%u index=%u\n",
         buf->memory, buf->type, buf->index);
    return v4l2_overlay_ioctl(fd, VIDIOC_QUERYBUF, buf, "querybuf ioctl");
}

int v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len)
{
    LOG_FUNCTION_NAME

    struct v4l2_buffer buf;
    int ret;

    ret = v4l2_overlay_query_buffer(fd, index, &buf);
    if (ret)
        return ret;

    if (is_mmaped(&buf)) {
        LOGE("Trying to mmap buffers that are already mapped!\n");
        return -EINVAL;
    }

    *len = buf.length;
    *start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
            fd, buf.m.offset);
    if (*start == MAP_FAILED) {
        LOGE("map failed, length=%u offset=%u\n", buf.length, buf.m.offset);
        return -EINVAL;
    }
    return 0;
}

int v4l2_overlay_unmap_buf(void *start, size_t len)
{
    LOG_FUNCTION_NAME
    return munmap(start, len);
}


int v4l2_overlay_get_caps(int fd, struct v4l2_capability *caps)
{
    return v4l2_overlay_ioctl(fd, VIDIOC_QUERYCAP, caps, "query cap");
}

int v4l2_overlay_stream_on(int fd)
{
    int ret;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = v4l2_overlay_set_local_alpha(fd, 1);
    if (ret)
        return ret;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_STREAMON, &type, "stream on");

    return ret;
}

int v4l2_overlay_stream_off(int fd)
{
    int ret;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = v4l2_overlay_set_local_alpha(fd, 0);
    if (ret)
        return ret;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_STREAMOFF, &type, "stream off");

    return ret;
}

int v4l2_overlay_q_buf(int fd, int buffer, int zerocopy)
{
    struct v4l2_buffer buf;
    int ret;

    if (zerocopy) {
        uint8_t *pPhyYAddr;
        uint8_t *pPhyCAddr;
        struct fimc_buf fimc_src_buf;
        uint8_t index;

        memcpy(&pPhyYAddr, (void *) buffer, sizeof(pPhyYAddr));
        memcpy(&pPhyCAddr, (void *) (buffer + sizeof(pPhyYAddr)),
               sizeof(pPhyCAddr));
        memcpy(&index,
               (void *) (buffer + sizeof(pPhyYAddr) + sizeof(pPhyCAddr)),
               sizeof(index));

        fimc_src_buf.base[0] = (dma_addr_t) pPhyYAddr;
        fimc_src_buf.base[1] = (dma_addr_t) pPhyCAddr;
        fimc_src_buf.base[2] =
               (dma_addr_t) (pPhyCAddr + (pPhyCAddr - pPhyYAddr)/4);

        buf.index = index;
        buf.memory      = V4L2_MEMORY_USERPTR;
        buf.m.userptr   = (unsigned long)&fimc_src_buf;
        buf.length      = 0;
    } else {
        buf.index = buffer;
        buf.memory      = V4L2_MEMORY_MMAP;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    buf.flags = 0;

    return v4l2_overlay_ioctl(fd, VIDIOC_QBUF, &buf, "qbuf");
}

int v4l2_overlay_dq_buf(int fd, int *index, int zerocopy)
{
    struct v4l2_buffer buf;
    int ret;

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (zerocopy)
        buf.memory = V4L2_MEMORY_USERPTR;
    else
        buf.memory = V4L2_MEMORY_MMAP;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_DQBUF, &buf, "dqbuf");
    if (ret)
        return ret;
    *index = buf.index;
    return 0;
}
