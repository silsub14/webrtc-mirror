/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_REMOTE_BITRATE_ESTIMATOR_TEST_BWE_TEST_FRAMEWORK_H_
#define WEBRTC_MODULES_REMOTE_BITRATE_ESTIMATOR_TEST_BWE_TEST_FRAMEWORK_H_

#include <assert.h>
#include <math.h>

#include <algorithm>
#include <limits>
#include <list>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "webrtc/modules/bitrate_controller/include/bitrate_controller.h"
#include "webrtc/modules/interface/module_common_types.h"
#include "webrtc/modules/pacing/include/paced_sender.h"
#include "webrtc/modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "webrtc/modules/remote_bitrate_estimator/test/bwe_test_logging.h"
#include "webrtc/modules/rtp_rtcp/interface/rtp_rtcp_defines.h"
#include "webrtc/system_wrappers/interface/clock.h"
#include "webrtc/system_wrappers/interface/scoped_ptr.h"

namespace webrtc {

class RtcpBandwidthObserver;

namespace testing {
namespace bwe {

class DelayCapHelper;
class RateCounter;

typedef std::set<int> FlowIds;
const FlowIds CreateFlowIds(const int *flow_ids_array, size_t num_flow_ids);

template <typename T>
bool DereferencingComparator(const T* const& a, const T* const& b) {
  return *a < *b;
}

template<typename T> class Stats {
 public:
  Stats()
      : data_(),
        last_mean_count_(0),
        last_variance_count_(0),
        last_minmax_count_(0),
        mean_(0),
        variance_(0),
        min_(0),
        max_(0) {
  }

  void Push(T data_point) {
    data_.push_back(data_point);
  }

  T GetMean() {
    if (last_mean_count_ != data_.size()) {
      last_mean_count_ = data_.size();
      mean_ = std::accumulate(data_.begin(), data_.end(), static_cast<T>(0));
      assert(last_mean_count_ != 0);
      mean_ /= static_cast<T>(last_mean_count_);
    }
    return mean_;
  }
  T GetVariance() {
    if (last_variance_count_ != data_.size()) {
      last_variance_count_ = data_.size();
      T mean = GetMean();
      variance_ = 0;
      for (const auto& sample : data_) {
        T diff = (sample - mean);
        variance_ += diff * diff;
      }
      assert(last_variance_count_ != 0);
      variance_ /= static_cast<T>(last_variance_count_);
    }
    return variance_;
  }
  T GetStdDev() {
    return sqrt(static_cast<double>(GetVariance()));
  }
  T GetMin() {
    RefreshMinMax();
    return min_;
  }
  T GetMax() {
    RefreshMinMax();
    return max_;
  }

  std::string AsString() {
    std::stringstream ss;
    ss << (GetMean() >= 0 ? GetMean() : -1) << ", " <<
        (GetStdDev() >= 0 ? GetStdDev() : -1);
    return ss.str();
  }

  void Log(const std::string& units) {
    BWE_TEST_LOGGING_LOG5("", "%f %s\t+/-%f\t[%f,%f]",
        GetMean(), units.c_str(), GetStdDev(), GetMin(), GetMax());
  }

 private:
  void RefreshMinMax() {
    if (last_minmax_count_ != data_.size()) {
      last_minmax_count_ = data_.size();
      min_ = max_ = 0;
      if (data_.empty()) {
        return;
      }
      typename std::vector<T>::const_iterator it = data_.begin();
      min_ = max_ = *it;
      while (++it != data_.end()) {
        min_ = std::min(min_, *it);
        max_ = std::max(max_, *it);
      }
    }
  }

  std::vector<T> data_;
  typename std::vector<T>::size_type last_mean_count_;
  typename std::vector<T>::size_type last_variance_count_;
  typename std::vector<T>::size_type last_minmax_count_;
  T mean_;
  T variance_;
  T min_;
  T max_;
};

class Random {
 public:
  explicit Random(uint32_t seed);

  // Return pseudo random number in the interval [0.0, 1.0].
  float Rand();

  // Normal Distribution.
  int Gaussian(int mean, int standard_deviation);

  // TODO(solenberg): Random from histogram.
  // template<typename T> int Distribution(const std::vector<T> histogram) {

 private:
  uint32_t a_;
  uint32_t b_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Random);
};

class Packet {
 public:
  enum Type { kMedia, kFeedback };

  Packet();
  Packet(int flow_id, int64_t send_time_us, size_t payload_size);
  virtual ~Packet();

  virtual bool operator<(const Packet& rhs) const;

  virtual int flow_id() const { return flow_id_; }
  virtual int64_t creation_time_us() const { return creation_time_us_; }
  virtual void set_send_time_us(int64_t send_time_us);
  virtual int64_t send_time_us() const { return send_time_us_; }
  virtual size_t payload_size() const { return payload_size_; }
  virtual Packet::Type GetPacketType() const = 0;

 private:
  int flow_id_;
  int64_t creation_time_us_;  // Time when the packet was created.
  int64_t send_time_us_;   // Time the packet left last processor touching it.
  size_t payload_size_;    // Size of the (non-existent, simulated) payload.
};

class MediaPacket : public Packet {
 public:
  MediaPacket();
  MediaPacket(int flow_id,
              int64_t send_time_us,
              size_t payload_size,
              const RTPHeader& header);
  MediaPacket(int64_t send_time_us, uint32_t sequence_number);
  virtual ~MediaPacket() {}

  int64_t GetAbsSendTimeInMs() const;
  void SetAbsSendTimeMs(int64_t abs_send_time_ms);
  const RTPHeader& header() const { return header_; }
  virtual Packet::Type GetPacketType() const { return kMedia; }

 private:
  RTPHeader header_;
};

class FeedbackPacket : public Packet {
 public:
  FeedbackPacket(int flow_id, int64_t send_time_us)
      : Packet(flow_id, send_time_us, 0) {}
  virtual ~FeedbackPacket() {}

  virtual Packet::Type GetPacketType() const { return kFeedback; }
};

class RembFeedback : public FeedbackPacket {
 public:
  RembFeedback(int flow_id,
               int64_t send_time_us,
               uint32_t estimated_bps,
               RTCPReportBlock report_block);
  virtual ~RembFeedback() {}

  uint32_t estimated_bps() const { return estimated_bps_; }
  RTCPReportBlock report_block() const { return report_block_; }

 private:
  const uint32_t estimated_bps_;
  const RTCPReportBlock report_block_;
};

class SendSideBweFeedback : public FeedbackPacket {
 public:
  typedef std::map<uint16_t, int64_t> ArrivalTimesMap;
  SendSideBweFeedback(int flow_id,
                      int64_t send_time_us,
                      const std::vector<PacketInfo>& packet_feedback_vector);
  virtual ~SendSideBweFeedback() {}

  const std::vector<PacketInfo>& packet_feedback_vector() const {
    return packet_feedback_vector_;
  }

 private:
  const std::vector<PacketInfo> packet_feedback_vector_;
};

typedef std::list<Packet*> Packets;
typedef std::list<Packet*>::iterator PacketsIt;
typedef std::list<Packet*>::const_iterator PacketsConstIt;

bool IsTimeSorted(const Packets& packets);

class PacketProcessor;

enum ProcessorType { kSender, kReceiver, kRegular };

class PacketProcessorListener {
 public:
  virtual ~PacketProcessorListener() {}

  virtual void AddPacketProcessor(PacketProcessor* processor,
                                  ProcessorType type) = 0;
  virtual void RemovePacketProcessor(PacketProcessor* processor) = 0;
};

class PacketProcessor {
 public:
  PacketProcessor(PacketProcessorListener* listener, ProcessorType type);
  PacketProcessor(PacketProcessorListener* listener,
                  int flow_id,
                  ProcessorType type);
  PacketProcessor(PacketProcessorListener* listener,
                  const FlowIds& flow_ids,
                  ProcessorType type);
  virtual ~PacketProcessor();

  // Called after each simulation batch to allow the processor to plot any
  // internal data.
  virtual void Plot(int64_t timestamp_ms) {}

  // Run simulation for |time_ms| micro seconds, consuming packets from, and
  // producing packets into in_out. The outgoing packet list must be sorted on
  // |send_time_us_|. The simulation time |time_ms| is optional to use.
  virtual void RunFor(int64_t time_ms, Packets* in_out) = 0;

  const FlowIds& flow_ids() const { return flow_ids_; }

 private:
  PacketProcessorListener* listener_;
  FlowIds flow_ids_;

  DISALLOW_COPY_AND_ASSIGN(PacketProcessor);
};

class RateCounterFilter : public PacketProcessor {
 public:
  explicit RateCounterFilter(PacketProcessorListener* listener);
  RateCounterFilter(PacketProcessorListener* listener,
                    const std::string& name);
  RateCounterFilter(PacketProcessorListener* listener,
                    const FlowIds& flow_ids,
                    const std::string& name);
  virtual ~RateCounterFilter();

  uint32_t packets_per_second() const;
  uint32_t bits_per_second() const;

  void LogStats();
  Stats<double> GetBitrateStats() const;
  virtual void Plot(int64_t timestamp_ms);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

 private:
  scoped_ptr<RateCounter> rate_counter_;
  Stats<double> packets_per_second_stats_;
  Stats<double> kbps_stats_;
  std::string name_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RateCounterFilter);
};

class LossFilter : public PacketProcessor {
 public:
  explicit LossFilter(PacketProcessorListener* listener);
  virtual ~LossFilter() {}

  void SetLoss(float loss_percent);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

 private:
  Random random_;
  float loss_fraction_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(LossFilter);
};

class DelayFilter : public PacketProcessor {
 public:
  explicit DelayFilter(PacketProcessorListener* listener);
  virtual ~DelayFilter() {}

  void SetDelay(int64_t delay_ms);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

 private:
  int64_t delay_us_;
  int64_t last_send_time_us_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(DelayFilter);
};

class JitterFilter : public PacketProcessor {
 public:
  explicit JitterFilter(PacketProcessorListener* listener);
  virtual ~JitterFilter() {}

  void SetJitter(int64_t stddev_jitter_ms);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

 private:
  Random random_;
  int64_t stddev_jitter_us_;
  int64_t last_send_time_us_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(JitterFilter);
};

// Reorders two consecutive packets with a probability of reorder_percent.
class ReorderFilter : public PacketProcessor {
 public:
  explicit ReorderFilter(PacketProcessorListener* listener);
  virtual ~ReorderFilter() {}

  void SetReorder(float reorder_percent);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

 private:
  Random random_;
  float reorder_fraction_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ReorderFilter);
};

// Apply a bitrate choke with an infinite queue on the packet stream.
class ChokeFilter : public PacketProcessor {
 public:
  explicit ChokeFilter(PacketProcessorListener* listener);
  ChokeFilter(PacketProcessorListener* listener, const FlowIds& flow_ids);
  virtual ~ChokeFilter();

  void SetCapacity(uint32_t kbps);
  void SetMaxDelay(int max_delay_ms);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

  Stats<double> GetDelayStats() const;

 private:
  uint32_t kbps_;
  int64_t last_send_time_us_;
  scoped_ptr<DelayCapHelper> delay_cap_helper_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ChokeFilter);
};

class TraceBasedDeliveryFilter : public PacketProcessor {
 public:
  explicit TraceBasedDeliveryFilter(PacketProcessorListener* listener);
  TraceBasedDeliveryFilter(PacketProcessorListener* listener,
                           const std::string& name);
  virtual ~TraceBasedDeliveryFilter();

  // The file should contain nanosecond timestamps corresponding to the time
  // when the network can accept another packet. The timestamps should be
  // separated by new lines, e.g., "100000000\n125000000\n321000000\n..."
  bool Init(const std::string& filename);
  virtual void Plot(int64_t timestamp_ms);
  virtual void RunFor(int64_t time_ms, Packets* in_out);

  void SetMaxDelay(int max_delay_ms);
  Stats<double> GetDelayStats() const;
  Stats<double> GetBitrateStats() const;

 private:
  void ProceedToNextSlot();

  typedef std::vector<int64_t> TimeList;
  int64_t current_offset_us_;
  TimeList delivery_times_us_;
  TimeList::const_iterator next_delivery_it_;
  int64_t local_time_us_;
  scoped_ptr<RateCounter> rate_counter_;
  std::string name_;
  scoped_ptr<DelayCapHelper> delay_cap_helper_;
  Stats<double> packets_per_second_stats_;
  Stats<double> kbps_stats_;

  DISALLOW_COPY_AND_ASSIGN(TraceBasedDeliveryFilter);
};

class VideoSource {
 public:
  VideoSource(int flow_id,
              float fps,
              uint32_t kbps,
              uint32_t ssrc,
              int64_t first_frame_offset_ms);
  virtual ~VideoSource() {}

  uint32_t max_payload_size_bytes() const { return kMaxPayloadSizeBytes; }
  uint32_t bytes_per_second() const { return bytes_per_second_; }

  virtual void RunFor(int64_t time_ms, Packets* in_out);
  virtual void SetBitrateBps(int bitrate_bps) {}
  virtual int flow_id() const { return flow_id_; }

 protected:
  virtual uint32_t NextFrameSize();
  virtual uint32_t NextPacketSize(uint32_t frame_size,
                                  uint32_t remaining_payload);

  const uint32_t kMaxPayloadSizeBytes;
  const uint32_t kTimestampBase;
  const int flow_id_;
  const double frame_period_ms_;
  uint32_t bytes_per_second_;
  uint32_t frame_size_bytes_;

 private:
  double next_frame_ms_;
  double now_ms_;
  RTPHeader prototype_header_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(VideoSource);
};

class AdaptiveVideoSource : public VideoSource {
 public:
  AdaptiveVideoSource(int flow_id,
                      float fps,
                      uint32_t kbps,
                      uint32_t ssrc,
                      int64_t first_frame_offset_ms);
  virtual ~AdaptiveVideoSource() {}

  virtual void SetBitrateBps(int bitrate_bps) OVERRIDE;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AdaptiveVideoSource);
};

class PeriodicKeyFrameSource : public AdaptiveVideoSource {
 public:
  PeriodicKeyFrameSource(int flow_id,
                         float fps,
                         uint32_t kbps,
                         uint32_t ssrc,
                         int64_t first_frame_offset_ms,
                         int key_frame_interval);
  virtual ~PeriodicKeyFrameSource() {}

 protected:
  virtual uint32_t NextFrameSize() OVERRIDE;
  virtual uint32_t NextPacketSize(uint32_t frame_size,
                                  uint32_t remaining_payload) OVERRIDE;

 private:
  int key_frame_interval_;
  uint32_t frame_counter_;
  int compensation_bytes_;
  int compensation_per_frame_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(PeriodicKeyFrameSource);
};

class SendSideBwe : public Module {
 public:
  SendSideBwe() {}
  virtual ~SendSideBwe() {}

  virtual int GetFeedbackIntervalMs() const = 0;
  virtual void GiveFeedback(const FeedbackPacket& feedback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(SendSideBwe);
};

class NullSendSideBwe : public SendSideBwe {
 public:
  NullSendSideBwe() {}
  virtual ~NullSendSideBwe() {}

  virtual int GetFeedbackIntervalMs() const OVERRIDE { return 1000; }
  virtual void GiveFeedback(const FeedbackPacket& feedback) OVERRIDE {}
  virtual int64_t TimeUntilNextProcess() OVERRIDE {
    return std::numeric_limits<int64_t>::max();
  }
  virtual int Process() OVERRIDE { return 0; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullSendSideBwe);
};

class RembSendSideBwe : public SendSideBwe {
 public:
  RembSendSideBwe(int kbps, BitrateObserver* observer, Clock* clock);
  virtual ~RembSendSideBwe();

  virtual int GetFeedbackIntervalMs() const OVERRIDE { return 100; }
  virtual void GiveFeedback(const FeedbackPacket& feedback) OVERRIDE;
  virtual int64_t TimeUntilNextProcess() OVERRIDE;
  virtual int Process() OVERRIDE;

 protected:
  scoped_ptr<BitrateController> bitrate_controller_;
  scoped_ptr<RtcpBandwidthObserver> feedback_observer_;

 private:
  Clock* clock_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RembSendSideBwe);
};

class FullSendSideBwe : public SendSideBwe, public RemoteBitrateObserver {
 public:
  FullSendSideBwe(int kbps, BitrateObserver* observer, Clock* clock);
  virtual ~FullSendSideBwe();

  virtual int GetFeedbackIntervalMs() const OVERRIDE { return 100; }
  virtual void GiveFeedback(const FeedbackPacket& feedback) OVERRIDE;
  virtual void OnReceiveBitrateChanged(const std::vector<unsigned int>& ssrcs,
                                       unsigned int bitrate) OVERRIDE;
  virtual int64_t TimeUntilNextProcess() OVERRIDE;
  virtual int Process() OVERRIDE;

 protected:
  scoped_ptr<BitrateController> bitrate_controller_;
  scoped_ptr<RemoteBitrateEstimator> rbe_;
  scoped_ptr<RtcpBandwidthObserver> feedback_observer_;

 private:
  Clock* const clock_;
  RTCPReportBlock report_block_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FullSendSideBwe);
};

enum BandwidthEstimatorType {
  kNullEstimator,
  kRembEstimator,
  kFullSendSideEstimator
};

class PacketSender : public PacketProcessor, public BitrateObserver {
 public:
  PacketSender(PacketProcessorListener* listener,
               VideoSource* source,
               BandwidthEstimatorType estimator);
  virtual ~PacketSender();

  // Call GiveFeedback() with the returned interval in milliseconds, provided
  // there is a new estimate available.
  // Note that changing the feedback interval affects the timing of when the
  // output of the estimators is sampled and therefore the baseline files may
  // have to be regenerated.
  virtual int GetFeedbackIntervalMs() const;
  virtual void GiveFeedback(const FeedbackPacket& feedback);
  virtual void RunFor(int64_t time_ms, Packets* in_out) OVERRIDE;

  virtual VideoSource* source() const { return source_; }

  // Implements BitrateObserver.
  virtual void OnNetworkChanged(uint32_t target_bitrate_bps,
                                uint8_t fraction_lost,
                                int64_t rtt) OVERRIDE;

 protected:
  SimulatedClock clock_;
  VideoSource* source_;
  scoped_ptr<SendSideBwe> bwe_;
  int64_t start_of_run_ms_;
  std::list<Module*> modules_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PacketSender);
};

class PacedVideoSender : public PacketSender, public PacedSender::Callback {
 public:
  PacedVideoSender(PacketProcessorListener* listener,
                   VideoSource* source,
                   BandwidthEstimatorType estimator);
  virtual ~PacedVideoSender();

  virtual void RunFor(int64_t time_ms, Packets* in_out) OVERRIDE;

  // Implements PacedSender::Callback.
  virtual bool TimeToSendPacket(uint32_t ssrc,
                                uint16_t sequence_number,
                                int64_t capture_time_ms,
                                bool retransmission) OVERRIDE;
  virtual size_t TimeToSendPadding(size_t bytes) OVERRIDE;

  // Implements BitrateObserver.
  virtual void OnNetworkChanged(uint32_t target_bitrate_bps,
                                uint8_t fraction_lost,
                                int64_t rtt) OVERRIDE;

 private:
  class ProbingPacedSender : public PacedSender {
   public:
    ProbingPacedSender(Clock* clock,
                       Callback* callback,
                       int bitrate_kbps,
                       int max_bitrate_kbps,
                       int min_bitrate_kbps)
        : PacedSender(clock,
                      callback,
                      bitrate_kbps,
                      max_bitrate_kbps,
                      min_bitrate_kbps) {}

    virtual bool ProbingExperimentIsEnabled() const OVERRIDE { return true; }
  };

  int64_t TimeUntilNextProcess(const std::list<Module*>& modules);
  void CallProcess(const std::list<Module*>& modules);
  void QueuePackets(Packets* batch, int64_t end_of_batch_time_us);

  ProbingPacedSender pacer_;
  Packets queue_;
  Packets pacer_queue_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(PacedVideoSender);
};
}  // namespace bwe
}  // namespace testing
}  // namespace webrtc

#endif  // WEBRTC_MODULES_REMOTE_BITRATE_ESTIMATOR_TEST_BWE_TEST_FRAMEWORK_H_
