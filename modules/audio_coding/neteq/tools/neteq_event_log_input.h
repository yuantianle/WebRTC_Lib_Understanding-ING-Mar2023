/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_CODING_NETEQ_TOOLS_NETEQ_EVENT_LOG_INPUT_H_
#define MODULES_AUDIO_CODING_NETEQ_TOOLS_NETEQ_EVENT_LOG_INPUT_H_

#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "modules/audio_coding/neteq/tools/neteq_packet_source_input.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"

namespace webrtc {
namespace test {

class RtcEventLogSource;

// Implementation of NetEqPacketSourceInput to be used with an
// RtcEventLogSource.
class NetEqEventLogInput final : public NetEqPacketSourceInput {
 public:
  static NetEqEventLogInput* CreateFromFile(
      absl::string_view file_name,
      absl::optional<uint32_t> ssrc_filter);
  static NetEqEventLogInput* CreateFromString(
      absl::string_view file_contents,
      absl::optional<uint32_t> ssrc_filter);

  absl::optional<int64_t> NextOutputEventTime() const override;
  absl::optional<SetMinimumDelayInfo> NextSetMinimumDelayInfo() const override;
  void AdvanceOutputEvent() override;
  void AdvanceSetMinimumDelay() override;

 protected:
  PacketSource* source() override;

 private:
  NetEqEventLogInput(std::unique_ptr<RtcEventLogSource> source);
  std::unique_ptr<RtcEventLogSource> source_;
  absl::optional<SetMinimumDelayInfo> next_minimum_delay_event_info_;
};

}  // namespace test
}  // namespace webrtc
#endif  // MODULES_AUDIO_CODING_NETEQ_TOOLS_NETEQ_EVENT_LOG_INPUT_H_
