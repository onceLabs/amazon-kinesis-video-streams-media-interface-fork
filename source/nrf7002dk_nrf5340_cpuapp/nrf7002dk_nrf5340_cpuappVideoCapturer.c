/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "nrf7002dk_nrf5340_cpuappCommon.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturer.h"

// Temp fixes to enable compilation
#define Zephyr_HANDLE_NULL_CHECK(x)                                                                                                                    \
    if (!(x)) {                                                                                                                                      \
        return -EINVAL;                                                                                                                              \
    }
#define Zephyr_HANDLE_STATUS_CHECK(zephyrHandle, expectedStatus)                                                                                         \
    if ((zephyrHandle)->status != (expectedStatus)) {                                                                                                  \
        return -EAGAIN;                                                                                                                              \
    }

typedef VideoCapturerHandle ZephyrCapturerHandle;

typedef enum {
    Zephyr_CAP_FMT_H264 = 0,
} ZephyrCapturerFormat;

void* zephyrCapturerOpen(const char* deviceName)
{
    return NULL;
}
int zephyrCapturerConfig(ZephyrCapturerHandle handle, uint32_t width, uint32_t height, ZephyrCapturerFormat format, uint32_t targetBitrate)
{
    return 0;
}
int zephyrCapturerStartStreaming(ZephyrCapturerHandle handle)
{
    return 0;
}
int zephyrCapturerSyncGetFrame(ZephyrCapturerHandle handle, uint32_t timeoutSec, void* pFrameDataBuffer, size_t frameDataBufferSize, size_t* pFrameSize)
{
    return 0;
}
int zephyrCapturerStopStreaming(ZephyrCapturerHandle handle)
{
    return 0;
}
void zephyrCapturerClose(ZephyrCapturerHandle handle)
{
}


#include "nrf7002dk_nrf5340_cpuappPort.h"
// #include "nrf7002dk_nrf5340_cpuappCapturer.h" TODO fix this

#define Zephyr_TARGET_BITRATE             (5 * 1024 * 1024LL)
#define Zephyr_SYNC_GET_FRAME_TIMEOUT_SEC (1)

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    ZephyrCapturerHandle privHandle;
} ZephyrVideoCapturer;

#define Zephyr_HANDLE_GET(x) ZephyrVideoCapturer* zephyrHandle = (ZephyrVideoCapturer*) ((x))

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    if (newStatus != zephyrHandle->status) {
        zephyrHandle->status = newStatus;
        LOG("VideoCapturer new status[%d]", newStatus);
    }

    return 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    ZephyrVideoCapturer* zephyrHandle = NULL;

    if (!(zephyrHandle = (ZephyrVideoCapturer*) malloc(sizeof(ZephyrVideoCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(zephyrHandle, 0, sizeof(ZephyrVideoCapturer));

    zephyrHandle->privHandle = zephyrCapturerOpen("/dev/video0");

    if (!zephyrHandle->privHandle) {
        LOG("Failed to open /dev/video0");
        videoCapturerDestroy((VideoCapturerHandle) zephyrHandle);
        return NULL;
    }

    // Now implementation supports H.264, 1080p, 720p, 480p, 360p and 320p
    zephyrHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    zephyrHandle->capability.resolutions =
        (1 << (VID_RES_1080P - 1)) | (1 << (VID_RES_720P - 1)) | (1 << (VID_RES_480P - 1)) | (1 << (VID_RES_360P - 1)) | (1 << (VID_RES_320P - 1));

    setStatus((VideoCapturerHandle) zephyrHandle, VID_CAP_STATUS_STREAM_OFF);

    return (VideoCapturerHandle) zephyrHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }

    Zephyr_HANDLE_GET(handle);
    return zephyrHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability* pCapability)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    if (!pCapability) {
        return -EINVAL;
    }

    *pCapability = zephyrHandle->capability;

    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    Zephyr_HANDLE_STATUS_CHECK(zephyrHandle, VID_CAP_STATUS_STREAM_OFF);

    uint32_t width, height = 0;
    switch (resolution) {
        case VID_RES_1080P:
            width = 1920;
            height = 1080;
            break;
        case VID_RES_720P:
            width = 1080;
            height = 720;
            break;
        case VID_RES_480P:
            width = 720;
            height = 480;
            break;
        case VID_RES_360P:
            width = 480;
            height = 360;
            break;
        case VID_RES_320P:
            width = 416;
            height = 320;
            break;

        default:
            LOG("Unsupported resolution %d", resolution);
            return -EINVAL;
    }

    ZephyrCapturerFormat privFormat = Zephyr_CAP_FMT_H264;
    switch (format) {
        case VID_FMT_H264:
            privFormat = Zephyr_CAP_FMT_H264;
            break;

        default:
            LOG("Unsupported format %d", format);
            return -EINVAL;
            break;
    }

    if (zephyrCapturerConfig(zephyrHandle->privHandle, width, height, privFormat, Zephyr_TARGET_BITRATE)) {
        LOG("Failed to config Zephyr Capturer");
        return -EAGAIN;
    }

    zephyrHandle->format = format;
    zephyrHandle->resolution = resolution;

    return 0;
}

int videoCapturerGetFormat(const VideoCapturerHandle handle, VideoFormat* pFormat, VideoResolution* pResolution)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    *pFormat = zephyrHandle->format;
    *pResolution = zephyrHandle->resolution;

    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    if (zephyrCapturerStartStreaming(zephyrHandle->privHandle)) {
        LOG("Failed to acquire stream");
        return -EAGAIN;
    }

    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void* pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    Zephyr_HANDLE_STATUS_CHECK(zephyrHandle, VID_CAP_STATUS_STREAM_ON);

    int ret = zephyrCapturerSyncGetFrame(zephyrHandle->privHandle, Zephyr_SYNC_GET_FRAME_TIMEOUT_SEC, pFrameDataBuffer, frameDataBufferSize, pFrameSize);
    if (!ret) {
        *pTimestamp = getEpochTimestampInUs();
    }

    return ret;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    Zephyr_HANDLE_NULL_CHECK(handle);
    Zephyr_HANDLE_GET(handle);

    if (zephyrCapturerStopStreaming(zephyrHandle->privHandle)) {
        LOG("Failed to release stream");
        return -EAGAIN;
    }

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestroy(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }
    Zephyr_HANDLE_GET(handle);

    if (zephyrHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(zephyrHandle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);

    zephyrCapturerClose(zephyrHandle->privHandle);

    free(handle);
}
