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

#include "LIVESTREAMCommon.h"
#include "LIVESTREAMPort.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturerLIVESTREAM.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "usbforwardertypes.h"
#include "usbcdcacm.h"


LOG_MODULE_REGISTER(LIVESTREAMVideoCapturer, LOG_LEVEL_DBG);

#define LIVESTREAM_HANDLE_GET(x) LIVESTREAMVideoCapturer* imageHandle = (LIVESTREAMVideoCapturer*) ((x))

#define EXTRA_AVCC_SPACE 50  // TODO task/BNCC-204

extern struct k_fifo usbforwarder;

static uint64_t current_timestamp = 0;

typedef struct {
    VideoCapturerStatus status;
    VideoCapability capability;
    VideoFormat format;
    VideoResolution resolution;
    char *buffer;
    size_t buffer_size;
} LIVESTREAMVideoCapturer;

static int setStatus(VideoCapturerHandle handle, const VideoCapturerStatus newStatus)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    if (newStatus != imageHandle->status) {
        imageHandle->status = newStatus;
        LOG("VideoCapturer new status[%d]", newStatus);
    }

    return 0;
}

VideoCapturerHandle videoCapturerCreate(void)
{
    LIVESTREAMVideoCapturer* imageHandle = NULL;


    if (!(imageHandle = (LIVESTREAMVideoCapturer*) malloc(sizeof(LIVESTREAMVideoCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(imageHandle, 0, sizeof(LIVESTREAMVideoCapturer));

    // Now we have sample frames for H.264, 1080p
    imageHandle->capability.formats = (1 << (VID_FMT_H264 - 1));
    imageHandle->capability.resolutions = (1 << (VID_RES_480P - 1));


    setStatus((VideoCapturerHandle) imageHandle, VID_CAP_STATUS_STREAM_OFF);

    return (VideoCapturerHandle) imageHandle;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
    if (!handle) {
        return VID_CAP_STATUS_NOT_READY;
    }

    LIVESTREAM_HANDLE_GET(handle);
    return imageHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle, VideoCapability* pCapability)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    if (!pCapability) {
        return -EAGAIN;
    }

    *pCapability = imageHandle->capability;

    return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle, const VideoFormat format, const VideoResolution resolution)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_OFF);

    switch (format) {
        case VID_FMT_H264:
            LOG_INF("Setting format to H264");
            break;

        default:
            LOG("Unsupported format %d", format);
            return -EINVAL;
    }

    switch (resolution) {
        case VID_RES_1080P:
            break;

        case VID_RES_720P:
            break;

        case VID_RES_480P:
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
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    *pFormat = imageHandle->format;
    *pResolution = imageHandle->resolution;

    return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LOG_DBG("Acquiring stream");

    // send start sending command
    add_data_to_usb(USB_START_COMMAND);
    k_sleep(K_MSEC(40)); // TODO check for event or determine better magic number

    current_timestamp = getEpochTimestampInUs();

    return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

int videoCapturerGetFrame(VideoCapturerHandle handle, void** pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        LOG_ERR("Invalid argument - found NULL pointer");
        return -EINVAL;
    }

    int ret = 0;

    struct data_item_var_t *new_item = k_fifo_get(&usbforwarder, K_FOREVER);
    if (new_item == NULL) {
        LOG_ERR("Failed to get item from USB Forwarder");
        return -ENOENT;
    }


    LOG_DBG("Received data from USB Forwarder of length: %d", new_item->len);

    *pFrameDataBuffer = new_item->data;

    // *pTimestamp = getEpochTimestampInUs();
    *pTimestamp = current_timestamp;
    *pFrameSize = new_item->len;

    current_timestamp += 1000000 / 30; // 30 fps

    k_free(new_item);

    add_data_to_usb("nextnext"); // useless character for some reason that's useful // TODO figure this out correctly

    return ret;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);
    LOG_DBG("Releasing stream");

    // send stop sending command
    add_data_to_usb(USB_STOP_COMMAND);

    return setStatus(handle, VID_CAP_STATUS_STREAM_OFF);
}

void videoCapturerDestory(VideoCapturerHandle handle)
{
    if (!handle) {
        return;
    }

    LIVESTREAM_HANDLE_GET(handle);

    if (imageHandle->status == VID_CAP_STATUS_STREAM_ON) {
        videoCapturerReleaseStream(handle);
    }

    setStatus(handle, VID_CAP_STATUS_NOT_READY);

    free(handle);
}
