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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "STATICIMAGECommon.h"
#include "STATICIMAGEPort.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturer.h"

// image source
#include "keyframe.h"

// #define FRAME_STATICIMAGE_POSTFIX_H264     ".h264"
// #define FRAME_STATICIMAGE_START_INDEX_H264 (1)
#define FRAME_STATICIMAGE_START_INDEX_H264 (0)
#define FRAME_STATICIMAGE_END_INDEX_H264   (1)
// #define FRAME_STATICIMAGE_END_INDEX_H264   (240)
// #define FRAME_STATICIMAGE_PATH_FORMAT_H264 FRAME_STATICIMAGE_PATH_PREFIX "h264/frame-%03d" FRAME_STATICIMAGE_POSTFIX_H264
// #define FRAME_STATICIMAGE_DURATION_US_H264 (1000 * 1000 / 25UL)

#define STATICIMAGE_HANDLE_GET(x) STATICIMAGEVideoCapturer* imageHandle = (STATICIMAGEVideoCapturer*) ((x))

#define FRAME_STATICIMAGE_DURATION_US_H264 (1000 * 1000 / 25UL)

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    // char* framePathFormat;
    size_t frameIndex;
    size_t frameIndexStart;     // for animation
    size_t frameIndexEnd;       // for animation
    // STATICIMAGE* frameFile;
    char *buffer;
    size_t buffer_size;
} STATICIMAGEVideoCapturer;

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    if (newStatus != imageHandle->status) {
        imageHandle->status = newStatus;
        LOG("VideoCapturer new status[%d]", newStatus);
    }

    return 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    STATICIMAGEVideoCapturer* imageHandle = NULL;


    if (!(imageHandle = (STATICIMAGEVideoCapturer*) malloc(sizeof(STATICIMAGEVideoCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(imageHandle, 0, sizeof(STATICIMAGEVideoCapturer));

    // Now we have sample frames for H.264, 1080p
    imageHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    imageHandle->capability.resolutions = (1 << (VID_RES_1080P - 1));

    imageHandle->buffer = keyframe_h264;
    imageHandle->buffer_size = sizeof(keyframe_h264);


    setStatus((VideoCapturerHandle) imageHandle, VID_CAP_STATUS_STREAM_OFF);

    return (VideoCapturerHandle) imageHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }

    STATICIMAGE_HANDLE_GET(handle);
    return imageHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability* pCapability)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    if (!pCapability) {
        return -EAGAIN;
    }

    *pCapability = imageHandle->capability;

    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_OFF);

    switch (format) {
        case VID_FMT_H264:
            imageHandle->frameIndexStart = FRAME_STATICIMAGE_START_INDEX_H264;
            imageHandle->frameIndexEnd = FRAME_STATICIMAGE_END_INDEX_H264;
            break;

        default:
            LOG("Unsupported format %d", format);
            return -EINVAL;
    }

    switch (resolution) {
        case VID_RES_1080P:
            break;

        default:
            LOG("Unsupported resolution %d", resolution);
            return -EINVAL;
    }

    imageHandle->format = format;
    imageHandle->resolution = resolution;

    return 0;
}

int videoCapturerGetFormat(const VideoCapturerHandle handle, VideoFormat* pFormat, VideoResolution* pResolution)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    *pFormat = imageHandle->format;
    *pResolution = imageHandle->resolution;

    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    imageHandle->frameIndex = imageHandle->frameIndexStart;

    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void* pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        return -EINVAL;
    }

    int ret = 0;

    size_t frameSize = imageHandle->buffer_size;

    // increment frame index
    imageHandle->frameIndex++ % imageHandle->frameIndexEnd;

    pTimestamp = getEpochTimestampInUs();

    // read image
    // TODO check available buffer size relative to frame size
    memcpy(pFrameDataBuffer, imageHandle->buffer, frameSize); // TODO if out of RAM, do a shallow copy

    usleep(FRAME_STATICIMAGE_DURATION_US_H264);

    return ret;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestory(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }

    STATICIMAGE_HANDLE_GET(handle);

    if (imageHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(handle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);

    free(handle);
}
