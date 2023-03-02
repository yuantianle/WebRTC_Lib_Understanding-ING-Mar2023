/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_CODING_NETEQ_NETEQ_IMPL_H_
#define MODULES_AUDIO_CODING_NETEQ_NETEQ_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/optional.h"
#include "api/audio/audio_frame.h"
#include "api/neteq/neteq.h"
#include "api/neteq/neteq_controller.h"
#include "api/neteq/neteq_controller_factory.h"
#include "api/neteq/tick_timer.h"
#include "api/rtp_packet_info.h"
#include "modules/audio_coding/neteq/audio_multi_vector.h"
#include "modules/audio_coding/neteq/expand_uma_logger.h"
#include "modules/audio_coding/neteq/packet.h"
#include "modules/audio_coding/neteq/random_vector.h"
#include "modules/audio_coding/neteq/statistics_calculator.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

// Forward declarations.
class Accelerate;
class BackgroundNoise;
class Clock;
class ComfortNoise;
class DecoderDatabase;
class DtmfBuffer;
class DtmfToneGenerator;
class Expand;
class Merge;
class NackTracker;
class Normal;
class PacketBuffer;
class RedPayloadSplitter;
class PostDecodeVad;
class PreemptiveExpand;
class RandomVector;
class SyncBuffer;
class TimestampScaler;
struct AccelerateFactory;
struct DtmfEvent;
struct ExpandFactory;
struct PreemptiveExpandFactory;

class NetEqImpl : public webrtc::NetEq {
 public:
  enum class OutputType {
    kNormalSpeech,
    kPLC,
    kCNG,
    kPLCCNG,
    kVadPassive,
    kCodecPLC
  };

  enum ErrorCodes {
    kNoError = 0,
    kOtherError,
    kUnknownRtpPayloadType,
    kDecoderNotFound,
    kInvalidPointer,
    kAccelerateError,
    kPreemptiveExpandError,
    kComfortNoiseErrorCode,
    kDecoderErrorCode,
    kOtherDecoderError,
    kInvalidOperation,
    kDtmfParsingError,
    kDtmfInsertError,
    kSampleUnderrun,
    kDecodedTooMuch,
    kRedundancySplitError,
    kPacketBufferCorruption
  };

  struct Dependencies {
    // The constructor populates the Dependencies struct with the default
    // implementations of the objects. They can all be replaced by the user
    // before sending the struct to the NetEqImpl constructor. However, there
    // are dependencies between some of the classes inside the struct, so
    // swapping out one may make it necessary to re-create another one.
    Dependencies(const NetEq::Config& config,
                 Clock* clock,
                 const rtc::scoped_refptr<AudioDecoderFactory>& decoder_factory,
                 const NetEqControllerFactory& controller_factory);
    ~Dependencies();

    Clock* const clock;
    std::unique_ptr<TickTimer> tick_timer;
    std::unique_ptr<StatisticsCalculator> stats;
    std::unique_ptr<DecoderDatabase> decoder_database;
    std::unique_ptr<DtmfBuffer> dtmf_buffer;
    std::unique_ptr<DtmfToneGenerator> dtmf_tone_generator;
    std::unique_ptr<PacketBuffer> packet_buffer;
    std::unique_ptr<NetEqController> neteq_controller;
    std::unique_ptr<RedPayloadSplitter> red_payload_splitter;
    std::unique_ptr<TimestampScaler> timestamp_scaler;
    std::unique_ptr<AccelerateFactory> accelerate_factory;
    std::unique_ptr<ExpandFactory> expand_factory;
    std::unique_ptr<PreemptiveExpandFactory> preemptive_expand_factory;
  };

  // Creates a new NetEqImpl object.
  NetEqImpl(const NetEq::Config& config,
            Dependencies&& deps,
            bool create_components = true);

  ~NetEqImpl() override;

  NetEqImpl(const NetEqImpl&) = delete;
  NetEqImpl& operator=(const NetEqImpl&) = delete;

  // !Inserts a new packet into NetEq.
  // Inserts a new packet into NetEq. Returns 0 on success, -1 on failure.
  int InsertPacket(const RTPHeader& rtp_header,                           
                   rtc::ArrayView<const uint8_t> payload) override;

  // !Lets NetEq know that a packet arrived with an empty payload. This typically
  // !happens when empty packets are used for probing the network channel, and
  // !these packets use RTP sequence numbers from the same series as the actual
  // !audio packets.
  void InsertEmptyPacket(const RTPHeader& rtp_header) override;

  // !Instructs NetEq to deliver 10 ms of audio data. The data is written to
  // !`audio_frame`. All data in `audio_frame` is wiped; `data_`, `speech_type_`,
  // !`num_channels_`, `sample_rate_hz_`, `samples_per_channel_`, and
  // !`vad_activity_` are updated upon success. If an error is returned, some
  // !fields may not have been updated, or may contain inconsistent values.
  // !If muted state is enabled (through Config::enable_muted_state), `muted`
  // !may be set to true after a prolonged expand period. When this happens, the
  // !`data_` in `audio_frame` is not written, but should be interpreted as being
  // !all zeros. For testing purposes, an override can be supplied in the
  // !`action_override` argument, which will cause NetEq to take this action
  // !next, instead of the action it would normally choose. An optional output
  // !argument for fetching the current sample rate can be provided, which
  // !will return the same value as last_output_sample_rate_hz() but will avoid
  // !additional synchronization.
  // !Returns kOK on success, or kFail in case of an error.
  int GetAudio(
      AudioFrame* audio_frame,
      bool* muted,
      int* current_sample_rate_hz = nullptr,
      absl::optional<Operation> action_override = absl::nullopt) override;

  // ^Set decoder type
  // !Replaces the current set of decoders with the given one.
  void SetCodecs(const std::map<int, SdpAudioFormat>& codecs) override;
  
  //  ^Playload body settings
  // !Associates `rtp_payload_type` with the given codec, which NetEq will
  // !instantiate when it needs it. Returns true iff successful.
  bool RegisterPayloadType(int rtp_payload_type,
                           const SdpAudioFormat& audio_format) override;

  // Removes `rtp_payload_type` from the codec database. Returns 0 on success,
  // -1 on failure.
  int RemovePayloadType(uint8_t rtp_payload_type) override;

  // !Removes all payload types from the codec database.
  void RemoveAllPayloadTypes() override;

  // ^Maximum/minimum delays
  // !Sets a minimum delay in millisecond for packet buffer. The minimum is
  // !maintained unless a higher latency is dictated by channel condition.
  // !Returns true if the minimum is successfully applied, otherwise false is
  // !returned.
  bool SetMinimumDelay(int delay_ms) override;

  // !Sets a maximum delay in milliseconds for packet buffer. The latency will
  // !not exceed the given value, even required delay (given the channel
  // !conditions) is higher. Calling this method has the same effect as setting
  // !the `max_delay_ms` value in the NetEq::Config struct.
  bool SetMaximumDelay(int delay_ms) override;

  // !Sets a base minimum delay in milliseconds for packet buffer. The minimum
  // !delay which is set via `SetMinimumDelay` can't be lower than base minimum
  // !delay. Calling this method is similar to setting the `min_delay_ms` value
  // !in the NetEq::Config struct. Returns true if the base minimum is
  // !successfully applied, otherwise false is returned.
  bool SetBaseMinimumDelayMs(int delay_ms) override;

  // !Returns current value of base minimum delay in milliseconds.
  int GetBaseMinimumDelayMs() const override;

  // !Returns the current target delay in ms. This includes any extra delay
  // !requested through SetMinimumDelay.
  int TargetDelayMs() const override;

  // !Returns the current total delay (packet buffer and sync buffer) in ms,
  // !with smoothing applied to even out short-time fluctuations due to jitter.
  // !The packet buffer part of the delay is not updated during DTX/CNG periods.
  int FilteredCurrentDelayMs() const override;

  // ^Statistics information
  // Writes the current network statistics to `stats`. The statistics are reset
  // after the call.
  int NetworkStatistics(NetEqNetworkStatistics* stats) override;

  // !Return current values only, not resetting any state.
  NetEqNetworkStatistics CurrentNetworkStatistics() const override;

  // !Returns a copy of this class's lifetime statistics. These statistics are
  // !never reset.
  NetEqLifetimeStatistics GetLifetimeStatistics() const override;

  // !Returns statistics about the performed operations and internal state. These
  // !statistics are never reset.
  NetEqOperationsAndState GetOperationsAndState() const override;

  // ^VAD (Voice Activity Detection)
  // Enables post-decode VAD. When enabled, GetAudio() will return
  // kOutputVADPassive when the signal contains no speech.
  void EnableVad() override;

  // Disables post-decode VAD.
  void DisableVad() override;

  // ^Timestamp
  // !Returns the RTP timestamp for the last sample delivered by GetAudio().
  // !The return value will be empty if no valid timestamp is available.
  absl::optional<uint32_t> GetPlayoutTimestamp() const override;

  // !Returns the sample rate in Hz of the audio produced in the last GetAudio
  // !call. If GetAudio has not been called yet, the configured sample rate
  // !(Config::sample_rate_hz) is returned.
  int last_output_sample_rate_hz() const override;

  // !Returns the decoder info for the given payload type. Returns empty if no
  // !such payload type was registered.
  absl::optional<DecoderFormat> GetDecoderFormat(
      int payload_type) const override;

  // ^Flush buffers
  // Flushes both the packet buffer and the sync buffer.
  void FlushBuffers() override;

  // ^NACK (Negative Acknowledgement)
  // !Enables NACK and sets the maximum size of the NACK list, which should be
  // !positive and no larger than Nack::kNackListSizeLimit. If NACK is already
  // !enabled then the maximum NACK list size is modified accordingly.
  void EnableNack(size_t max_nack_list_size) override;

  void DisableNack() override;

  // !Returns a list of RTP sequence numbers corresponding to packets to be
  // !retransmitted, given an estimate of the round-trip time in milliseconds.
  std::vector<uint16_t> GetNackList(int64_t round_trip_time_ms) const override;
  
  // ^Sync Buffer (audio buffer)
  // !Returns the length of the audio yet to play in the sync buffer.
  // !Mainly intended for testing.
  int SyncBufferSizeMs() const override;

  // This accessor method is only intended for testing purposes.
  const SyncBuffer* sync_buffer_for_test() const;
  Operation last_operation_for_test() const;

 protected:
  static const int kOutputSizeMs = 10;
  static const size_t kMaxFrameSize = 5760;  // 120 ms @ 48 kHz.
  // TODO(hlundin): Provide a better value for kSyncBufferSize.
  // Current value is kMaxFrameSize + 60 ms * 48 kHz, which is enough for
  // calculating correlations of current frame against history.
  static const size_t kSyncBufferSize = kMaxFrameSize + 60 * 48;

  // ^Inserts a new packet into NetEq. This is used by the InsertPacket method
  // ^above. Returns 0 on success, otherwise an error code.
  // TODO(hlundin): Merge this with InsertPacket above?
  int InsertPacketInternal(const RTPHeader& rtp_header,
                           rtc::ArrayView<const uint8_t> payload)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // ^Outgoing packet processing
  // Delivers 10 ms of audio data. The data is written to `audio_frame`.
  // Returns 0 on success, otherwise an error code.
  int GetAudioInternal(AudioFrame* audio_frame,
                       bool* muted,
                       absl::optional<Operation> action_override)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // ^Refer neteq/decision_logic.cc
  // Provides a decision to the GetAudioInternal method. The decision what to
  // do is written to `operation`. Packets to decode are written to
  // `packet_list`, and a DTMF event to play is written to `dtmf_event`. When
  // DTMF should be played, `play_dtmf` is set to true by the method.
  // Returns 0 on success, otherwise an error code.
  int GetDecision(Operation* operation,
                  PacketList* packet_list,
                  DtmfEvent* dtmf_event,
                  bool* play_dtmf,
                  absl::optional<Operation> action_override)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // ^Decoder sub methods
  // Decodes the speech packets in `packet_list`, and writes the results to
  // `decoded_buffer`, which is allocated to hold `decoded_buffer_length`
  // elements. The length of the decoded data is written to `decoded_length`.
  // The speech type -- speech or (codec-internal) comfort noise -- is written
  // to `speech_type`. If `packet_list` contains any SID frames for RFC 3389
  // comfort noise, those are not decoded.
  int Decode(PacketList* packet_list,
             Operation* operation,
             int* decoded_length,
             AudioDecoder::SpeechType* speech_type)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method to Decode(). Performs codec internal CNG.
  int DecodeCng(AudioDecoder* decoder,
                int* decoded_length,
                AudioDecoder::SpeechType* speech_type)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method to Decode(). Performs the actual decoding.
  int DecodeLoop(PacketList* packet_list,
                 const Operation& operation,
                 AudioDecoder* decoder,
                 int* decoded_length,
                 AudioDecoder::SpeechType* speech_type)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the Normal class to perform the normal operation.
  void DoNormal(const int16_t* decoded_buffer,
                size_t decoded_length,
                AudioDecoder::SpeechType speech_type,
                bool play_dtmf) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the Merge class to perform the merge operation.
  void DoMerge(int16_t* decoded_buffer,
               size_t decoded_length,
               AudioDecoder::SpeechType speech_type,
               bool play_dtmf) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  bool DoCodecPlc() RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the Expand class to perform the expand operation.
  int DoExpand(bool play_dtmf) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the Accelerate class to perform the accelerate
  // operation.
  int DoAccelerate(int16_t* decoded_buffer,
                   size_t decoded_length,
                   AudioDecoder::SpeechType speech_type,
                   bool play_dtmf,
                   bool fast_accelerate) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the PreemptiveExpand class to perform the
  // preemtive expand operation.
  int DoPreemptiveExpand(int16_t* decoded_buffer,
                         size_t decoded_length,
                         AudioDecoder::SpeechType speech_type,
                         bool play_dtmf) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sub-method which calls the ComfortNoise class to generate RFC 3389 comfort
  // noise. `packet_list` can either contain one SID frame to update the
  // noise parameters, or no payload at all, in which case the previously
  // received parameters are used.
  int DoRfc3389Cng(PacketList* packet_list, bool play_dtmf)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Calls the audio decoder to generate codec-internal comfort noise when
  // no packet was received.
  void DoCodecInternalCng(const int16_t* decoded_buffer, size_t decoded_length)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Calls the DtmfToneGenerator class to generate DTMF tones.
  int DoDtmf(const DtmfEvent& dtmf_event, bool* play_dtmf)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Overdub DTMF on top of `output`.
  int DtmfOverdub(const DtmfEvent& dtmf_event,
                  size_t num_channels,
                  int16_t* output) const RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // ^Data --> Packet Buffer
  // Extracts packets from `packet_buffer_` to produce at least
  // `required_samples` samples. The packets are inserted into `packet_list`.
  // Returns the number of samples that the packets in the list will produce, or
  // -1 in case of an error.
  int ExtractPackets(size_t required_samples, PacketList* packet_list)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Resets various variables and objects to new values based on the sample rate
  // `fs_hz` and `channels` number audio channels.
  void SetSampleRateAndChannels(int fs_hz, size_t channels)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns the output type for the audio produced by the latest call to
  // GetAudio().
  OutputType LastOutputType() RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Updates Expand and Merge.
  virtual void UpdatePlcComponents(int fs_hz, size_t channels)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  NetEqNetworkStatistics CurrentNetworkStatisticsInternal() const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Clock* const clock_;

  mutable Mutex mutex_;
  const std::unique_ptr<TickTimer> tick_timer_ RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<DecoderDatabase> decoder_database_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<DtmfBuffer> dtmf_buffer_ RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<DtmfToneGenerator> dtmf_tone_generator_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<PacketBuffer> packet_buffer_ RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<RedPayloadSplitter> red_payload_splitter_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<TimestampScaler> timestamp_scaler_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<PostDecodeVad> vad_ RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<ExpandFactory> expand_factory_ RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<AccelerateFactory> accelerate_factory_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<PreemptiveExpandFactory> preemptive_expand_factory_
      RTC_GUARDED_BY(mutex_);
  const std::unique_ptr<StatisticsCalculator> stats_ RTC_GUARDED_BY(mutex_);

  std::unique_ptr<BackgroundNoise> background_noise_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<NetEqController> controller_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<AudioMultiVector> algorithm_buffer_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<SyncBuffer> sync_buffer_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<Expand> expand_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<Normal> normal_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<Merge> merge_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<Accelerate> accelerate_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<PreemptiveExpand> preemptive_expand_ RTC_GUARDED_BY(mutex_);
  RandomVector random_vector_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<ComfortNoise> comfort_noise_ RTC_GUARDED_BY(mutex_);
  int fs_hz_ RTC_GUARDED_BY(mutex_);
  int fs_mult_ RTC_GUARDED_BY(mutex_);
  int last_output_sample_rate_hz_ RTC_GUARDED_BY(mutex_);
  size_t output_size_samples_ RTC_GUARDED_BY(mutex_);
  size_t decoder_frame_length_ RTC_GUARDED_BY(mutex_);
  Mode last_mode_ RTC_GUARDED_BY(mutex_);
  Operation last_operation_ RTC_GUARDED_BY(mutex_);
  size_t decoded_buffer_length_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<int16_t[]> decoded_buffer_ RTC_GUARDED_BY(mutex_);
  uint32_t playout_timestamp_ RTC_GUARDED_BY(mutex_);
  bool new_codec_ RTC_GUARDED_BY(mutex_);
  uint32_t timestamp_ RTC_GUARDED_BY(mutex_);
  bool reset_decoder_ RTC_GUARDED_BY(mutex_);
  absl::optional<uint8_t> current_rtp_payload_type_ RTC_GUARDED_BY(mutex_);
  absl::optional<uint8_t> current_cng_rtp_payload_type_ RTC_GUARDED_BY(mutex_);
  bool first_packet_ RTC_GUARDED_BY(mutex_);
  bool enable_fast_accelerate_ RTC_GUARDED_BY(mutex_);
  std::unique_ptr<NackTracker> nack_ RTC_GUARDED_BY(mutex_);
  bool nack_enabled_ RTC_GUARDED_BY(mutex_);
  const bool enable_muted_state_ RTC_GUARDED_BY(mutex_);
  AudioFrame::VADActivity last_vad_activity_ RTC_GUARDED_BY(mutex_) =
      AudioFrame::kVadPassive;
  std::unique_ptr<TickTimer::Stopwatch> generated_noise_stopwatch_
      RTC_GUARDED_BY(mutex_);
  std::vector<RtpPacketInfo> last_decoded_packet_infos_ RTC_GUARDED_BY(mutex_);
  ExpandUmaLogger expand_uma_logger_ RTC_GUARDED_BY(mutex_);
  ExpandUmaLogger speech_expand_uma_logger_ RTC_GUARDED_BY(mutex_);
  bool no_time_stretching_ RTC_GUARDED_BY(mutex_);  // Only used for test.
  rtc::BufferT<int16_t> concealment_audio_ RTC_GUARDED_BY(mutex_);
};

}  // namespace webrtc
#endif  // MODULES_AUDIO_CODING_NETEQ_NETEQ_IMPL_H_
