/**
 * @file LIVESTREAMVideoCapturer.c
 * @brief KVS video capturer backed by the USB forwarder FIFO - RW612 / Zephyr
 *
 * Implements the VideoCapturerHandle interface used by the KVS embedded
 * producer SDK.  Frames arrive from the N6 via the USB forwarder FIFO
 * (populated by usb_forwarder_sink.c) and are handed to the KVS SDK one
 * at a time via videoCapturerGetFrame().
 *
 * Timestamp strategy
 * ------------------
 * The N6 timestamps frames with HAL_GetTick() (ms since N6 boot).  The
 * RW612 has a wall-clock epoch via SNTP.  On the first frame of each
 * stream we anchor:
 *   rw612_epoch_us  = getEpochTimestampInUs()      (RW612 wall clock)
 *   n6_origin_ms    = new_item->timestamp           (N6 HAL_GetTick)
 *
 * Subsequent frames:
 *   timestamp_us = rw612_epoch_us
 *                + (new_item->timestamp - n6_origin_ms) * USEC_PER_MSEC
 *
 * This gives KVS a monotonically increasing timestamp anchored to real
 * wall time without requiring TIME_SYNC packets.
 *
 * @author Andrew Nyland
 * @date 2025-01-03  original
 * @date 2026-05-20  rewrite
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "LIVESTREAMCommon.h"
#include "LIVESTREAMPort.h"
#include "com/amazonaws/kinesis/video/capturer/VideoCapturerLIVESTREAM.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <theia/usbforwardertypes.h>
#include <theia/usb_forwarder_sink.h>
#include <theiacommon/usb_protocol_cdcacm.h>

LOG_MODULE_REGISTER(LIVESTREAMVideoCapturer, LOG_LEVEL_DBG);

#define LIVESTREAM_HANDLE_GET(x) \
    LIVESTREAMVideoCapturer *imageHandle = (LIVESTREAMVideoCapturer *)(x)

/* Extra bytes allocated for in-place Annex-B -> AVCC conversion.
 * Must match EXTRA_AVCC_SPACE in usb_forwarder_sink.c               */
#define EXTRA_AVCC_SPACE  50

extern struct k_fifo usbforwarder;

/* Timestamp anchors — reset each stream in videoCapturerReleaseStream() */
static uint64_t rw612_epoch_us  = 0;
static uint32_t n6_origin_ms    = 0;

typedef struct {
  VideoCapturerStatus status;
  VideoCapability     capability;
  VideoFormat         format;
  VideoResolution     resolution;
} LIVESTREAMVideoCapturer;


/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */
static int setStatus(VideoCapturerHandle handle,
                     const VideoCapturerStatus newStatus)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);

  if (newStatus != imageHandle->status) {
    imageHandle->status = newStatus;
    LOG_DBG("VideoCapturer status -> %d", newStatus);
  }
  return 0;
}


/* -----------------------------------------------------------------------
 * Public: VideoCapturerHandle API
 * --------------------------------------------------------------------- */

VideoCapturerHandle videoCapturerCreate(void)
{
  LIVESTREAMVideoCapturer *h = malloc(sizeof(LIVESTREAMVideoCapturer));
  if (h == NULL) {
    LOG_ERR("OOM allocating capturer handle");
    return NULL;
  }

  memset(h, 0, sizeof(*h));
  h->capability.formats     = (1 << (VID_FMT_H264 - 1));
  h->capability.resolutions = (1 << (VID_RES_480P - 1));

  setStatus((VideoCapturerHandle)h, VID_CAP_STATUS_STREAM_OFF);
  return (VideoCapturerHandle)h;
}

VideoCapturerStatus videoCapturerGetStatus(const VideoCapturerHandle handle)
{
  if (!handle) {
    return VID_CAP_STATUS_NOT_READY;
  }
  LIVESTREAM_HANDLE_GET(handle);
  return imageHandle->status;
}

int videoCapturerGetCapability(const VideoCapturerHandle handle,
                                VideoCapability *pCapability)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);
  if (!pCapability) {
    return -EAGAIN;
  }
  *pCapability = imageHandle->capability;
  return 0;
}

int videoCapturerSetFormat(VideoCapturerHandle handle,
                            const VideoFormat format,
                            const VideoResolution resolution)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);
  LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_OFF);

  switch (format) {
    case VID_FMT_H264:
      break;
    default:
      LOG_ERR("Unsupported format %d", format);
      return -EINVAL;
  }

  switch (resolution) {
    case VID_RES_1080P:
    case VID_RES_720P:
    case VID_RES_480P:
      break;
    default:
      LOG_ERR("Unsupported resolution %d", resolution);
      return -EINVAL;
  }

  imageHandle->format     = format;
  imageHandle->resolution = resolution;
  return 0;
}

int videoCapturerGetFormat(const VideoCapturerHandle handle,
                            VideoFormat *pFormat,
                            VideoResolution *pResolution)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);
  *pFormat     = imageHandle->format;
  *pResolution = imageHandle->resolution;
  return 0;
}

int videoCapturerAcquireStream(VideoCapturerHandle handle)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);

  LOG_DBG("Acquiring stream");

  /* Reset timestamp anchors for this stream session */
  rw612_epoch_us = 0;
  n6_origin_ms   = 0;

  /* Don't reset here — N6 should already be stopped from
   * videoCapturerReleaseStream. Just send START and force a keyframe. */
  usb_fm_send_ctrl(0, USBF_FM_ACTION_START);
  k_sleep(K_MSEC(10));

  /* Give the N6 time to start the encoder before we expect frames.
   * 40 ms covers one frame interval at 25 fps. */
  k_sleep(K_MSEC(40));

  return setStatus(handle, VID_CAP_STATUS_STREAM_ON);
}

/**
 * @brief Block until a frame is available and return it to the KVS SDK.
 *
 * The KVS SDK owns *pFrameDataBuffer after this returns and will free it
 * (or the buffer may be reused — depends on SDK version).  We allocate
 * via k_malloc in rx_work_handler; the SDK must not call free() directly.
 * Confirm with the KVS embedded SDK's memory contract if this changes.
 */
int videoCapturerGetFrame(VideoCapturerHandle handle,
                           void **pFrameDataBuffer,
                           const size_t frameDataBufferSize,
                           uint64_t *pTimestamp,
                           size_t *pFrameSize)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);
  LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

  if (!pFrameDataBuffer || !pTimestamp || !pFrameSize) {
    LOG_ERR("NULL argument");
    return -EINVAL;
  }

  struct data_item_var_t *item = k_fifo_get(&usbforwarder, K_MSEC(1200));
  if (item == NULL) {
    LOG_ERR("FIFO timeout — no frame from USB forwarder");
    return -ENOENT;
  }

  LOG_DBG("Frame received: len=%d ts=%u", item->len, item->timestamp);

  /* Anchor timestamps on the first frame of this stream */
  if (rw612_epoch_us == 0) {
    rw612_epoch_us = getEpochTimestampInUs();
    n6_origin_ms   = item->timestamp;
  }

  *pFrameDataBuffer = item->data;
  *pFrameSize       = item->len;
  *pTimestamp       = rw612_epoch_us
                      + ((uint64_t)(item->timestamp - n6_origin_ms)
                         * USEC_PER_MSEC);

  k_free(item);  /* free the FIFO wrapper; caller owns item->data */

  return 0;
}

int videoCapturerReleaseStream(VideoCapturerHandle handle)
{
  LIVESTREAM_HANDLE_NULL_CHECK(handle);
  LIVESTREAM_HANDLE_GET(handle);
  LIVESTREAM_HANDLE_STATUS_CHECK(imageHandle, VID_CAP_STATUS_STREAM_ON);

  LOG_DBG("Releasing stream");

  /* Tell N6 to stop encoding before draining USB buffers */
  usb_fm_send_ctrl(0, USBF_FM_ACTION_STOP);

  /* Reset timestamp anchors for the next stream session */
  rw612_epoch_us = 0;
  n6_origin_ms   = 0;

  /* Drain all USB buffers and reset parser state */
  usbf_shutdown_and_reset();

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