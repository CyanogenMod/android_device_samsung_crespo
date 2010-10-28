/*
 * Copyright 2009 Google Inc. All Rights Reserved.
 * Author: rschultz@google.com (Rebecca Schultz Zavin)
 */

#ifndef ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_
#define ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_

#include "s5p_fimc.h"

int v4l2_overlay_open(int id);
int v4l2_overlay_get_caps(int fd, struct v4l2_capability *caps);
int v4l2_overlay_req_buf(int fd, uint32_t *num_bufs, int cacheable_buffers,
                         int zerocopy);
int v4l2_overlay_query_buffer(int fd, int index, struct v4l2_buffer *buf);
int v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len);
int v4l2_overlay_unmap_buf(void *start, size_t len);
int v4l2_overlay_stream_on(int fd);
int v4l2_overlay_stream_off(int fd);
int v4l2_overlay_q_buf(int fd, int index, int zerocopy);
int v4l2_overlay_dq_buf(int fd, int *index, int zerocopy);
int v4l2_overlay_init(int fd, uint32_t w, uint32_t h, uint32_t fmt,
                      uint32_t addr);
int v4l2_overlay_get_input_size(int fd, uint32_t *w, uint32_t *h,
                                uint32_t *fmt);
int v4l2_overlay_set_position(int fd, int32_t x, int32_t y, int32_t w,
        int32_t h, int rotation);
int v4l2_overlay_get_position(int fd, int32_t *x, int32_t *y, int32_t *w,
        int32_t *h);
int v4l2_overlay_set_crop(int fd, uint32_t x, uint32_t y, uint32_t w,
        uint32_t h);
int v4l2_overlay_get_crop(int fd, uint32_t *x, uint32_t *y, uint32_t *w,
        uint32_t *h);
int v4l2_overlay_set_flip(int fd, int degree);
int v4l2_overlay_set_rotation(int fd, int degree, int step);
int v4l2_overlay_set_colorkey(int fd, int enable, int colorkey);
int v4l2_overlay_set_global_alpha(int fd, int enable, int alpha);
int v4l2_overlay_set_local_alpha(int fd, int enable);
int v4l2_overlay_init_fimc(int fd, s5p_fimc_t *s5p_fimc);
int v4l2_overlay_s_fbuf(int fd, int rotation);

enum {
    V4L2_OVERLAY_PLANE_GRAPHICS,
    V4L2_OVERLAY_PLANE_VIDEO1,
    V4L2_OVERLAY_PLANE_VIDEO2,
};

enum {
    /* support customed format for zero copy */
    HAL_PIXEL_FORMAT_YCbCr_420_SP = 0x21,
    HAL_PIXEL_FORMAT_YCbCr_420_P = 0x13,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP = 0x100,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I = 0x101,
    HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I = 0x102,
    HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP = 0x103,
    HAL_PIXEL_FORMAT_CUSTOM_MAX
};

enum {
    PFT_RGB,
    PFT_YUV420,
    PFT_YUV422,
    PFT_YUV444,
};

struct mapping_data {
    int fd;
    size_t length;
    uint32_t offset;
    void *ptr;
};

#define ALL_BUFFERS_FLUSHED -66

#endif  /* ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_*/
