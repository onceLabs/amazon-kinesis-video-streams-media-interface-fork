/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this LIVESTREAM except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" LIVESTREAM accompanying this LIVESTREAM. This LIVESTREAM is distributed
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
#include "com/amazonaws/kinesis/video/capturer/AudioCapturer.h"

#define FRAME_LIVESTREAM_POSTFIX_AAC       ".aac"
#define FRAME_LIVESTREAM_POSTFIX_G711A     ".alaw"
#define FRAME_LIVESTREAM_START_INDEX_AAC   (1)
#define FRAME_LIVESTREAM_START_INDEX_G711A (1)
#define FRAME_LIVESTREAM_END_INDEX_AAC     (760)
#define FRAME_LIVESTREAM_END_INDEX_G711A   (999)
#define FRAME_LIVESTREAM_PATH_FORMAT_AAC   FRAME_LIVESTREAM_PATH_PREFIX "aac/frame-%03d" FRAME_LIVESTREAM_POSTFIX_AAC
#define FRAME_LIVESTREAM_PATH_FORMAT_G711A FRAME_LIVESTREAM_PATH_PREFIX "g711a/frame-%03d" FRAME_LIVESTREAM_POSTFIX_G711A
#define FRAME_LIVESTREAM_DURATION_US_AAC   (1000 * 1000 / 48UL)
#define FRAME_LIVESTREAM_DURATION_US_G711A (1000 * 1000 / 25UL)

#define LIVESTREAM_HANDLE_GET(x) LIVESTREAMAudioCapturer* LIVESTREAMHandle = (LIVESTREAMAudioCapturer*) ((x))

typedef struct {
    AudioCapturerStatus status;
    AudioCapability capability;
    AudioFormat format;
    AudioChannel channel;
    AudioBitDepth bitDepth;
    AudioSampleRate sampleRate;
    char* framePathFormat;
    size_t frameIndex;
    size_t frameIndexStart;
    size_t frameIndexEnd;
    size_t frameDurationUs;
    LIVESTREAM* frameLIVESTREAM;
} LIVESTREAMAudioCapturer;

static int setStatus(AudioCapturerHandle handle, const AudioCapturerStatus newStatus)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    if (newStatus != LIVESTREAMHandle->status) {
        LIVESTREAMHandle->status = newStatus;
        LOG("AudioCapturer new status[%d]", newStatus);
    }

    return 0;
}

AudioCapturerHandle audioCapturerCreate(void)
{
    LIVESTREAMAudioCapturer* LIVESTREAMHandle = NULL;

    if (!(LIVESTREAMHandle = (LIVESTREAMAudioCapturer*) malloc(sizeof(LIVESTREAMAudioCapturer)))) {
        LOG("OOM");
        return NULL;
    }

    memset(LIVESTREAMHandle, 0, sizeof(LIVESTREAMAudioCapturer));

    // Now we have sample frames for G.711 ALAW and AAC, MONO, 8k, 16 bits
    LIVESTREAMHandle->capability.formats = (1 << (AUD_FMT_G711A - 1)) | (1 << (AUD_FMT_AAC - 1));
    LIVESTREAMHandle->capability.channels = (1 << (AUD_CHN_MONO - 1));
    LIVESTREAMHandle->capability.sampleRates = (1 << (AUD_SAM_8K - 1));
    LIVESTREAMHandle->capability.bitDepths = (1 << (AUD_BIT_16 - 1));

    setStatus((AudioCapturerHandle) LIVESTREAMHandle, AUD_CAP_STATUS_STREAM_OFF);

    return (AudioCapturerHandle) LIVESTREAMHandle;
}

AudioCapturerStatus audioCapturerGetStatus(const AudioCapturerHandle handle)
{
    if (!handle) {
        return AUD_CAP_STATUS_NOT_READY;
    }

    LIVESTREAM_HANDLE_GET(handle);
    return LIVESTREAMHandle->status;
}

int audioCapturerGetCapability(const AudioCapturerHandle handle, AudioCapability* pCapability)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    if (!pCapability) {
        return -EINVAL;
    }

    *pCapability = LIVESTREAMHandle->capability;

    return 0;
}

int audioCapturerSetFormat(AudioCapturerHandle handle, const AudioFormat format, const AudioChannel channel, const AudioSampleRate sampleRate,
                           const AudioBitDepth bitDepth)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(LIVESTREAMHandle, AUD_CAP_STATUS_STREAM_OFF);

    switch (format) {
        case AUD_FMT_G711A:
            LIVESTREAMHandle->framePathFormat = FRAME_LIVESTREAM_PATH_FORMAT_G711A;
            LIVESTREAMHandle->frameIndexStart = FRAME_LIVESTREAM_START_INDEX_G711A;
            LIVESTREAMHandle->frameIndexEnd = FRAME_LIVESTREAM_END_INDEX_G711A;
            LIVESTREAMHandle->frameDurationUs = FRAME_LIVESTREAM_DURATION_US_G711A;
            break;
        case AUD_FMT_AAC:
            LIVESTREAMHandle->framePathFormat = FRAME_LIVESTREAM_PATH_FORMAT_AAC;
            LIVESTREAMHandle->frameIndexStart = FRAME_LIVESTREAM_START_INDEX_AAC;
            LIVESTREAMHandle->frameIndexEnd = FRAME_LIVESTREAM_END_INDEX_AAC;
            LIVESTREAMHandle->frameDurationUs = FRAME_LIVESTREAM_DURATION_US_AAC;
            break;

        default:
            LOG("Unsupported format %d", format);
            return -EINVAL;
    }

    switch (channel) {
        case AUD_CHN_MONO:
            break;

        default:
            LOG("Unsupported channel num %d", channel);
            return -EINVAL;
    }

    switch (sampleRate) {
        case AUD_SAM_8K:
            break;

        default:
            LOG("Unsupported sample rate %d", sampleRate);
            return -EINVAL;
    }

    switch (bitDepth) {
        case AUD_BIT_16:
            break;

        default:
            LOG("Unsupported bit depth %d", bitDepth);
            return -EINVAL;
    }

    LIVESTREAMHandle->format = format;
    LIVESTREAMHandle->channel = channel;
    LIVESTREAMHandle->sampleRate = sampleRate;
    LIVESTREAMHandle->bitDepth = bitDepth;

    return 0;
}

int audioCapturerGetFormat(const AudioCapturerHandle handle, AudioFormat* pFormat, AudioChannel* pChannel, AudioSampleRate* pSampleRate,
                           AudioBitDepth* pBitDepth)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    *pFormat = LIVESTREAMHandle->format;
    *pChannel = LIVESTREAMHandle->channel;
    *pSampleRate = LIVESTREAMHandle->sampleRate;
    *pBitDepth = LIVESTREAMHandle->bitDepth;

    return 0;
}

int audioCapturerAcquireStream(AudioCapturerHandle handle)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAMHandle->frameIndex = LIVESTREAMHandle->frameIndexStart;

    return setStatus(handle, AUD_CAP_STATUS_STREAM_ON);
}

int audioCapturerGetFrame(AudioCapturerHandle handle, void* pFrameDataBuffer, const size_t frameDataBufferSize, uint64_t* pTimestamp,
                          size_t* pFrameSize)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(LIVESTREAMHandle, AUD_CAP_STATUS_STREAM_ON);

    if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
        return -EINVAL;
    }

    int ret = 0;

    if (LIVESTREAMHandle->frameLIVESTREAM) {
        CLOSE_LIVESTREAM(LIVESTREAMHandle->frameLIVESTREAM);
    }

    char LIVESTREAMPath[FRAME_LIVESTREAM_PATH_MAX_LENGTH] = {0};
    snprintf(LIVESTREAMPath, FRAME_LIVESTREAM_PATH_MAX_LENGTH, LIVESTREAMHandle->framePathFormat, LIVESTREAMHandle->frameIndex);
    if (LIVESTREAMHandle->frameIndex < LIVESTREAMHandle->frameIndexEnd) {
        LIVESTREAMHandle->frameIndex++;
    } else {
        LIVESTREAMHandle->frameIndex = LIVESTREAMHandle->frameIndexStart;
    }

    size_t frameSize = 0;
    LIVESTREAMHandle->frameLIVESTREAM = fopen(LIVESTREAMPath, "r");
    if (LIVESTREAMHandle->frameLIVESTREAM) {
        GET_LIVESTREAM_SIZE(LIVESTREAMHandle->frameLIVESTREAM, frameSize);
        if (frameSize >= 0) {
            if (frameDataBufferSize >= frameSize) {
                *pFrameSize = fread(pFrameDataBuffer, 1, frameSize, LIVESTREAMHandle->frameLIVESTREAM);
                *pTimestamp = getEpochTimestampInUs();
            } else {
                //LOG("FrameDataBufferSize(%ld) < frameSize(%ld), frame dropped", frameDataBufferSize, frameSize);
                *pFrameSize = 0;
                ret = -ENOMEM;
            }
        } else {
            LOG("Failed to get size of LIVESTREAM %s", LIVESTREAMPath);
            ret = -EAGAIN;
        }
        CLOSE_LIVESTREAM(LIVESTREAMHandle->frameLIVESTREAM);
    } else {
        LOG("Failed to open LIVESTREAM %s", LIVESTREAMPath);
        ret = -EAGAIN;
    }

    usleep(LIVESTREAMHandle->frameDurationUs);

    return ret;
}

int audioCapturerReleaseStream(AudioCapturerHandle handle)
{
    LIVESTREAM_HANDLE_NULL_CHECK(handle);
    LIVESTREAM_HANDLE_GET(handle);

    LIVESTREAM_HANDLE_STATUS_CHECK(LIVESTREAMHandle, AUD_CAP_STATUS_STREAM_ON);

    if (LIVESTREAMHandle->frameLIVESTREAM) {
        CLOSE_LIVESTREAM(LIVESTREAMHandle->frameLIVESTREAM);
    }

    return setStatus(handle, AUD_CAP_STATUS_STREAM_OFF);
}

void audioCapturerDestory(AudioCapturerHandle handle)
{
    if (!handle) {
        return;
    }

    LIVESTREAM_HANDLE_GET(handle);

    if (LIVESTREAMHandle->status == AUD_CAP_STATUS_STREAM_ON) {
        audioCapturerReleaseStream(handle);
    }

    setStatus(handle, AUD_CAP_STATUS_NOT_READY);

    free(handle);
}
