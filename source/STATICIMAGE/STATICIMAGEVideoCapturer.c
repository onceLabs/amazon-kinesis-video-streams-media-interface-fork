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

#define FRAME_STATICIMAGE_POSTFIX_H264     ".h264"
#define FRAME_STATICIMAGE_START_INDEX_H264 (1)
#define FRAME_STATICIMAGE_END_INDEX_H264   (240)
#define FRAME_STATICIMAGE_PATH_FORMAT_H264 FRAME_STATICIMAGE_PATH_PREFIX "h264/frame-%03d" FRAME_STATICIMAGE_POSTFIX_H264
#define FRAME_STATICIMAGE_DURATION_US_H264 (1000 * 1000 / 25UL)

#define STATICIMAGE_HANDLE_GET(x) STATICIMAGEVideoCapturer* fileHandle = (STATICIMAGEVideoCapturer*) ((x))

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    char* framePathFormat;
    size_t frameIndex;
    size_t frameIndexStart;
    size_t frameIndexEnd;
    STATICIMAGE* frameFile;
} STATICIMAGEVideoCapturer;

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    if (newStatus != fileHandle->status) {
        fileHandle->status = newStatus;
        LOG("VideoCapturer new status[%d]", newStatus);
    }

    return 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    STATICIMAGEVideoCapturer* fileHandle = NULL;

    if (!(fileHandle = (STATICIMAGEVideoCapturer*) malloc(sizeof(STATICIMAGEVideoCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(fileHandle, 0, sizeof(STATICIMAGEVideoCapturer));

    // Now we have sample frames for H.264, 1080p
    fileHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    fileHandle->capability.resolutions = (1 << (VID_RES_1080P - 1));

    setStatus((VideoCapturerHandle) fileHandle, VID_CAP_STATUS_STREAM_OFF);

    return (VideoCapturerHandle) fileHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }

    STATICIMAGE_HANDLE_GET(handle);
    return fileHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability* pCapability)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    if (!pCapability) {
        return -EAGAIN;
    }

    *pCapability = fileHandle->capability;

    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(fileHandle, VID_CAP_STATUS_STREAM_OFF);

    switch (format) {
        case VID_FMT_H264:
            fileHandle->framePathFormat = FRAME_STATICIMAGE_PATH_FORMAT_H264;
            fileHandle->frameIndexStart = FRAME_STATICIMAGE_START_INDEX_H264;
            fileHandle->frameIndexEnd = FRAME_STATICIMAGE_END_INDEX_H264;
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

    fileHandle->format = format;
    fileHandle->resolution = resolution;

    return 0;
}

int videoCapturerGetFormat(const VideoCapturerHandle handle, VideoFormat* pFormat, VideoResolution* pResolution)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    *pFormat = fileHandle->format;
    *pResolution = fileHandle->resolution;

    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    fileHandle->frameIndex = fileHandle->frameIndexStart;

    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void* pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(fileHandle, VID_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        return -EINVAL;
    }

    int ret = 0;

    if (fileHandle->frameFile) {
        CLOSE_STATICIMAGE(fileHandle->frameFile);
    }

    char filePath[FRAME_STATICIMAGE_PATH_MAX_LENGTH] = {0};
    snprintf(filePath, FRAME_STATICIMAGE_PATH_MAX_LENGTH, fileHandle->framePathFormat, fileHandle->frameIndex);
    if (fileHandle->frameIndex < fileHandle->frameIndexEnd) {
        fileHandle->frameIndex++;
    } else {
        fileHandle->frameIndex = fileHandle->frameIndexStart;
    }

    size_t frameSize = 0;
    fileHandle->frameFile = fopen(filePath, "r");
    if (fileHandle->frameFile) {
        GET_STATICIMAGE_SIZE(fileHandle->frameFile, frameSize);
        if (frameSize >= 0) {
            if (frameDataBufferSize >= frameSize) {
                *pFrameSize = fread(pFrameDataBuffer, 1, frameSize, fileHandle->frameFile);
                *pTimestamp = getEpochTimestampInUs();
            } else {
                //LOG("FrameDataBufferSize(%ld) < frameSize(%ld), frame dropped", frameDataBufferSize, frameSize);
                *pFrameSize = 0;
                ret = -ENOMEM;
            }
        } else {
            LOG("Failed to get size of file %s", filePath);
            ret = -EAGAIN;
        }
        CLOSE_STATICIMAGE(fileHandle->frameFile);
    } else {
        LOG("Failed to open file %s", filePath);
        ret = -EAGAIN;
    }

    usleep(FRAME_STATICIMAGE_DURATION_US_H264);

    return ret;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    STATICIMAGE_HANDLE_NULL_CHECK(handle);
    STATICIMAGE_HANDLE_GET(handle);

    STATICIMAGE_HANDLE_STATUS_CHECK(fileHandle, VID_CAP_STATUS_STREAM_ON);

    if (fileHandle->frameFile) {
        CLOSE_STATICIMAGE(fileHandle->frameFile);
    }

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestory(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }

    STATICIMAGE_HANDLE_GET(handle);

    if (fileHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(handle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);

    free(handle);
}
