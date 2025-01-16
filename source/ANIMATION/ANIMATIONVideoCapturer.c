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

#include "ANIMATIONCommon.h"
#include "ANIMATIONPort.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturer.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// image source
#include "animationfromcamera.h"

LOG_MODULE_REGISTER(animationVideoCapturer, LOG_LEVEL_DBG);

// #define FRAME_ANIMATION_POSTFIX_H264     ".h264"
// #define FRAME_ANIMATION_START_INDEX_H264 (1)
#define FRAME_ANIMATION_START_INDEX_H264 (0)
#define FRAME_ANIMATION_END_INDEX_H264   (NUM_ANIMATION_FRAMES)
// #define FRAME_ANIMATION_END_INDEX_H264   (240)
// #define FRAME_ANIMATION_PATH_FORMAT_H264 FRAME_ANIMATION_PATH_PREFIX "h264/frame-%03d" FRAME_ANIMATION_POSTFIX_H264
// #define FRAME_ANIMATION_DURATION_US_H264 (1000 * 1000 / 25UL)

#define ANIMATION_HANDLE_GET(x) ANIMATIONVideoCapturer* imageHandle = (ANIMATIONVideoCapturer*) ((x))

#define FRAME_ANIMATION_DURATION_US_H264 (1000 * 1000 / 25UL)

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    // char* framePathFormat;
    size_t frameIndex;
    size_t frameIndexStart;     // for animation
    size_t frameIndexEnd;       // for animation
    // ANIMATION* frameFile;
    char *buffer;
    size_t buffer_size;
} ANIMATIONVideoCapturer;

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    if (newStatus != imageHandle->status) {
        imageHandle->status = newStatus;
        LOG("VideoCapturer new status[%d]", newStatus);
    }

    return 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    ANIMATIONVideoCapturer* imageHandle = NULL;


    if (!(imageHandle = (ANIMATIONVideoCapturer*) malloc(sizeof(ANIMATIONVideoCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(imageHandle, 0, sizeof(ANIMATIONVideoCapturer));

    // Now we have sample frames for H.264, 1080p
    imageHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    imageHandle->capability.resolutions = (1 << (VID_RES_480P - 1));

    imageHandle->buffer = animation_frames[imageHandle->frameIndex];
    imageHandle->buffer_size = animation_frame_sizes[imageHandle->frameIndex];


    setStatus((VideoCapturerHandle) imageHandle, VID_CAP_STATUS_STREAM_OFF);

    return (VideoCapturerHandle) imageHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }

    ANIMATION_HANDLE_GET(handle);
    return imageHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability* pCapability)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    if (!pCapability) {
        return -EAGAIN;
    }

    *pCapability = imageHandle->capability;

    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    ANIMATION_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_OFF);

    switch (format) {
        case VID_FMT_H264:
            imageHandle->frameIndexStart = FRAME_ANIMATION_START_INDEX_H264;
            imageHandle->frameIndexEnd = FRAME_ANIMATION_END_INDEX_H264;
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
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    *pFormat = imageHandle->format;
    *pResolution = imageHandle->resolution;

    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    LOG_DBG("Acquiring stream");

    imageHandle->frameIndex = imageHandle->frameIndexStart;

    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void* pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    ANIMATION_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        return -EINVAL;
    }

    int ret = 0;

    // update buffer
    imageHandle->buffer = animation_frames[imageHandle->frameIndex];
    imageHandle->buffer_size = animation_frame_sizes[imageHandle->frameIndex];

    size_t frameSize = imageHandle->buffer_size;

    LOG_DBG("Frame index: %d with size: %d - %p %p", imageHandle->frameIndex, frameSize, imageHandle->buffer, pFrameDataBuffer);

    *pTimestamp = getEpochTimestampInUs();
    *pFrameSize = frameSize;

    // read image
    // TODO check available buffer size relative to frame size
    memcpy(pFrameDataBuffer, imageHandle->buffer, frameSize); // TODO if out of RAM, do a shallow copy
    // LOG_HEXDUMP_DBG(pFrameDataBuffer+frameSize-30, 20, "Frame data");

    // increment frame index
    imageHandle->frameIndex++;
    imageHandle->frameIndex %= (imageHandle->frameIndexEnd + 1);
    
    usleep(FRAME_ANIMATION_DURATION_US_H264);

    return ret;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    ANIMATION_HANDLE_NULL_CHECK(handle);
    ANIMATION_HANDLE_GET(handle);

    ANIMATION_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);
    LOG_DBG("Releasing stream");

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestory(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }

    ANIMATION_HANDLE_GET(handle);

    if (imageHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(handle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);

    free(handle);
}
