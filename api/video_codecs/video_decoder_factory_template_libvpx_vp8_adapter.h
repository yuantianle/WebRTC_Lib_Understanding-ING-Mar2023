/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_CODECS_VIDEO_DECODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_
#define API_VIDEO_CODECS_VIDEO_DECODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_

#include <memory>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"

namespace webrtc {
struct LibvpxVp8DecoderTemplateAdapter {
  static std::vector<SdpVideoFormat> SupportedFormats() {
    return {SdpVideoFormat("VP8")};
  }

  static std::unique_ptr<VideoDecoder> CreateDecoder(
      const SdpVideoFormat& format) {
    return VP8Decoder::Create();
  }
};
}  // namespace webrtc

#endif  // API_VIDEO_CODECS_VIDEO_DECODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_
