/**
 * @file UVCVideoCapturer.c
 * @brief KVS video capturer backed by USBH/UVC host-mode capture - RW612 / Zephyr
 *
 * Implements the VideoCapturerHandle interface used by the KVS embedded
 * producer SDK. Frames arrive from the N6 camera board over USB (host-mode
 * UVC, task/BNCC-801) instead of the CDC-ACM forwarder link, using Zephyr's
 * video API (video_dequeue/video_enqueue against the usbh_uvc class
 * device). Ported from the validated standalone POC at
 * rw612-uvc-poc/app_host_uvc/src/main.c.
 *
 * Buffer ownership
 * -----------------
 * Unlike LIVESTREAM's zero-copy CDC-ACM path (unbounded k_malloc per
 * frame), the video buffer pool here is a small fixed pool
 * (CONFIG_VIDEO_BUFFER_POOL_NUM_MAX buffers) that must be requeued to the
 * UVC class driver after every frame to keep streaming - it can never be
 * handed to KVS permanently. videoCapturerGetFrame therefore uses the
 * generic VideoCapturer.h copy-based contract (STATICIMAGE/ANIMATION
 * style): memcpy out of the dequeued video_buffer, then immediately
 * re-enqueue it.
 *
 * Timestamp strategy
 * -------------------
 * Mirrors LIVESTREAMVideoCapturer.c: struct video_buffer::timestamp is
 * ms since RW612 boot (k_uptime-based, set by the UVC host class driver
 * on frame completion). On the first frame of each stream we anchor:
 *   rw612_epoch_us = getEpochTimestampInUs()   (RW612 wall clock)
 *   origin_ms      = vbuf->timestamp
 * Subsequent frames: timestamp_us = rw612_epoch_us
 *                                  + (vbuf->timestamp - origin_ms) * USEC_PER_MSEC
 *
 * @author Andrew Nyland
 * @date 2026-08-26
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "UVCCommon.h"
#include "UVCPort.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturer.h"

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbh.h>

LOG_MODULE_REGISTER(UVCVideoCapturer, LOG_LEVEL_DBG);

#define UVC_HANDLE_GET(x) UVCVideoCapturer *imageHandle = (UVCVideoCapturer *)(x)

/* Fixed ceiling for a single H.264 keyframe, matched to
 * CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE / CONFIG_VIDEO_BUFFER_POOL_NUM_MAX in
 * boards/uvc_host.conf (92160 / 2 = 40960/buffer) - see that file's
 * comments for the hardware measurement (peak 720p keyframe 28,697 B)
 * this headroom is sized against. Must stay in sync with kvs_cli.c's
 * UVC_MAX_FRAME_SIZE, which sizes the buffer KVS hands to us here. */
#define UVC_H264_FRAME_CEILING 40960

/* Bounded wait for the N6 to enumerate on the USB host port, polled the
 * same way the POC's wait_for_video_connection() does. Unlike that POC
 * (which loops forever - fine for a standalone bring-up sample), a
 * production AcquireStream call must not hang the KVS video thread
 * indefinitely if the N6 never shows up. */
#define UVC_CONNECT_POLL_INTERVAL_MS 10
#define UVC_CONNECT_TIMEOUT_MS       5000

/* Timeout on a single dequeue - same 1200 ms convention as LIVESTREAM's
 * FIFO get (one frame interval's worth of slack at typical frame rates). */
#define UVC_DEQUEUE_TIMEOUT_MS 1200

USBH_CONTROLLER_DEFINE(uvc_uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    const struct device *dev;
    bool usbh_started;
    struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];
    uint8_t vbuf_count;
    uint64_t rw612_epoch_us;
    uint32_t origin_ms;
} UVCVideoCapturer;

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);

    if (newStatus != imageHandle->status) {
        imageHandle->status = newStatus;
        LOG_DBG("VideoCapturer status -> %d", newStatus);
    }

    return 0;
}

static int resolutionToDims(const VideoResolution resolution, uint32_t *width, uint32_t *height)
{
    switch (resolution) {
        case VID_RES_480P:
            *width = 640;
            *height = 480;
            return 0;
        case VID_RES_720P:
            *width = 1280;
            *height = 720;
            return 0;
        default:
            return -EINVAL;
    }
}

static void releaseVideoBuffers(UVCVideoCapturer *imageHandle)
{
    for (int i = 0; i < imageHandle->vbuf_count; i++) {
        if (imageHandle->vbufs[i]) {
            video_buffer_release(imageHandle->vbufs[i]);
            imageHandle->vbufs[i] = NULL;
        }
    }
    imageHandle->vbuf_count = 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    UVCVideoCapturer *imageHandle = malloc(sizeof(UVCVideoCapturer));
    if (imageHandle == NULL) {
        LOG_ERR("OOM allocating capturer handle");
        return NULL;
    }

    memset(imageHandle, 0, sizeof(*imageHandle));

    imageHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    imageHandle->capability.resolutions = (1 << (VID_RES_480P - 1)) | (1 << (VID_RES_720P - 1));

    imageHandle->dev = device_get_binding("usbh_uvc_0");
    if (imageHandle->dev == NULL) {
        LOG_ERR("usbh_uvc_0 device not found - is CONFIG_THEIA_USB_HOST_UVC enabled?");
    }

    setStatus((VideoCapturerHandle)imageHandle, VID_CAP_STATUS_STREAM_OFF);
    return (VideoCapturerHandle)imageHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }
    UVC_HANDLE_GET(handle);
    return imageHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability *pCapability)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);
    if (!pCapability) {
        return -EAGAIN;
    }
    *pCapability = imageHandle->capability;
    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);
    UVC_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_OFF);

    uint32_t width, height;

    if (format != VID_FMT_H264) {
        LOG_ERR("Unsupported format %d", format);
        return -EINVAL;
    }

    if (resolutionToDims(resolution, &width, &height) != 0) {
        LOG_ERR("Unsupported resolution %d", resolution);
        return -EINVAL;
    }

    imageHandle->format = format;
    imageHandle->resolution = resolution;
    return 0;
}

int videoCapturerGetFormat(const VideoCapturerHandle handle, VideoFormat *pFormat, VideoResolution *pResolution)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);
    *pFormat = imageHandle->format;
    *pResolution = imageHandle->resolution;
    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);

    if (imageHandle->dev == NULL) {
        LOG_ERR("No usbh_uvc device bound");
        return -ENODEV;
    }

    uint32_t width, height;
    if (resolutionToDims(imageHandle->resolution, &width, &height) != 0) {
        LOG_ERR("videoCapturerSetFormat was not called (or set an unsupported resolution)");
        return -EINVAL;
    }

    int ret;

    if (!imageHandle->usbh_started) {
        ret = usbh_init(&uvc_uhs_ctx);
        if (ret != 0) {
            LOG_ERR("usbh_init failed: %d", ret);
            return ret;
        }
        ret = usbh_enable(&uvc_uhs_ctx);
        if (ret != 0) {
            LOG_ERR("usbh_enable failed: %d", ret);
            return ret;
        }
        imageHandle->usbh_started = true;
    }

    LOG_DBG("Waiting for N6 UVC device to enumerate");
    struct video_format fmt = {.type = VIDEO_BUF_TYPE_OUTPUT};
    int waited_ms = 0;
    while (video_get_format(imageHandle->dev, &fmt) != 0) {
        if (waited_ms >= UVC_CONNECT_TIMEOUT_MS) {
            LOG_ERR("Timed out waiting for N6 UVC device");
            return -ENODEV;
        }
        k_sleep(K_MSEC(UVC_CONNECT_POLL_INTERVAL_MS));
        waited_ms += UVC_CONNECT_POLL_INTERVAL_MS;
        fmt.type = VIDEO_BUF_TYPE_OUTPUT;
    }

    fmt.pixelformat = VIDEO_PIX_FMT_H264;
    fmt.width = width;
    fmt.height = height;
    ret = video_set_format(imageHandle->dev, &fmt);
    if (ret != 0) {
        LOG_ERR("video_set_format failed: %d", ret);
        return ret;
    }

    struct video_caps caps = {.type = VIDEO_BUF_TYPE_OUTPUT};
    ret = video_get_caps(imageHandle->dev, &caps);
    if (ret != 0) {
        LOG_ERR("video_get_caps failed: %d", ret);
        return ret;
    }
    if (caps.min_vbuf_count > CONFIG_VIDEO_BUFFER_POOL_NUM_MAX) {
        LOG_ERR("Device requires %u buffers, pool only holds %u", caps.min_vbuf_count,
                CONFIG_VIDEO_BUFFER_POOL_NUM_MAX);
        return -ENOMEM;
    }

    for (imageHandle->vbuf_count = 0; imageHandle->vbuf_count < CONFIG_VIDEO_BUFFER_POOL_NUM_MAX;
         imageHandle->vbuf_count++) {
        struct video_buffer *vbuf =
            video_buffer_aligned_alloc(UVC_H264_FRAME_CEILING, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_MSEC(1000));
        if (vbuf == NULL) {
            LOG_ERR("Unable to alloc video buffer %u/%u", imageHandle->vbuf_count,
                    CONFIG_VIDEO_BUFFER_POOL_NUM_MAX);
            releaseVideoBuffers(imageHandle);
            return -ENOMEM;
        }
        vbuf->type = caps.type;
        imageHandle->vbufs[imageHandle->vbuf_count] = vbuf;
    }

    for (int i = 0; i < imageHandle->vbuf_count; i++) {
        video_enqueue(imageHandle->dev, imageHandle->vbufs[i]);
    }

    ret = video_stream_start(imageHandle->dev, caps.type);
    if (ret != 0) {
        LOG_ERR("video_stream_start failed: %d", ret);
        releaseVideoBuffers(imageHandle);
        return ret;
    }

    imageHandle->rw612_epoch_us = 0;
    imageHandle->origin_ms = 0;

    LOG_DBG("Acquired stream: %ux%u H.264", width, height);
    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void *pFrameDataBuffer, const size_t frameDataBufferSize,
                           uint64_t *pTimestamp, size_t *pFrameSize)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);
    UVC_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        LOG_ERR("NULL argument");
        return -EINVAL;
    }

    struct video_buffer *vbuf = NULL;
    int ret = video_dequeue(imageHandle->dev, &vbuf, K_MSEC(UVC_DEQUEUE_TIMEOUT_MS));
    if (ret != 0) {
        if (ret != -ENODEV) {
            LOG_ERR("video_dequeue failed: %d", ret);
        }
        return ret;
    }

    if (vbuf->bytesused > frameDataBufferSize) {
        LOG_ERR("Frame %u B exceeds caller buffer %zu B - dropping", vbuf->bytesused, frameDataBufferSize);
        video_enqueue(imageHandle->dev, vbuf);
        return -ENOSPC;
    }

    if (imageHandle->rw612_epoch_us == 0) {
        imageHandle->rw612_epoch_us = getEpochTimestampInUs();
        imageHandle->origin_ms = vbuf->timestamp;
    }

    memcpy(pFrameDataBuffer, vbuf->buffer, vbuf->bytesused);
    *pFrameSize = vbuf->bytesused;
    *pTimestamp = imageHandle->rw612_epoch_us +
                  ((uint64_t)(vbuf->timestamp - imageHandle->origin_ms) * USEC_PER_MSEC);

    ret = video_enqueue(imageHandle->dev, vbuf);
    if (ret != 0 && ret != -ENODEV) {
        LOG_ERR("video_enqueue failed: %d", ret);
    }

    return 0;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    UVC_HANDLE_NULL_CHECK(handle);
    UVC_HANDLE_GET(handle);
    UVC_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    LOG_DBG("Releasing stream");

    int ret = video_stream_stop(imageHandle->dev, VIDEO_BUF_TYPE_OUTPUT);
    if (ret != 0 && ret != -ENODEV) {
        LOG_ERR("video_stream_stop failed: %d", ret);
    }

    releaseVideoBuffers(imageHandle);

    imageHandle->rw612_epoch_us = 0;
    imageHandle->origin_ms = 0;

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestory(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }
    UVC_HANDLE_GET(handle);

    if (imageHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(handle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);
    free(handle);
}
