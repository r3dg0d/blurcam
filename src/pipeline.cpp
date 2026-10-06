#include "blurcam/vision.hpp"
#include <chrono>
#include <iostream>
#include <opencv2/imgproc.hpp>
namespace blurcam {
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a) {
  return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
}
Pipeline::Pipeline(const Config &c, std::unique_ptr<Detector> d)
    : config_(c), detector_(d ? std::move(d) : std::make_unique<YuNet>(c)), tracker_(c),
      effects_(c) {}
void Pipeline::change_effect(const std::string &e) {
  config_.effects = {e};
  effects_.change(config_);
}
void Pipeline::adjust_strength(int d) {
  config_.blur_radius = std::clamp(config_.blur_radius + d * 4, 1, 501);
  config_.pixel_size = std::clamp(config_.pixel_size + d * 2, 2, 1024);
  effects_.change(config_);
}
void Pipeline::toggle_debug() { config_.debug = !config_.debug; }
Metrics Pipeline::process(cv::Mat &frame) {
  if (frame.empty() || frame.type() != CV_8UC3)
    throw std::runtime_error("Pipeline requires a nonempty BGR8 frame");
  auto begin = Clock::now();
  Metrics m;
  bool changed = size_ != frame.size();
  if (changed) {
    tracker_.reset();
    size_ = frame.size();
    recovery_ = 0;
    known_region_ = {};
  }
  cv::Rect2f previous_region;
  for (const auto &face : tracker_.tracks())
    previous_region |= face.cover.empty() ? face.box : face.cover;
  auto start = Clock::now();
  try {
    tracker_.predict(frame);
  } catch (const cv::Exception &) {
    tracker_.reset();
    m.reason = "tracker failure";
  }
  m.tracker_ms = ms(start);
  bool unsafe = tracker_.lost() || tracker_.tracks().empty();
  if (frame_ % config_.detection_interval == 0 || unsafe || changed) {
    m.detected = true;
    start = Clock::now();
    try {
      auto faces = detector_->detect(frame);
      bool overflow = faces.size() > size_t(config_.max_faces);
      if (overflow)
        faces.resize(config_.max_faces);
      tracker_.refresh(faces, frame);
      unsafe = overflow || faces.empty() || tracker_.lost();
      if (overflow)
        m.reason = "face limit exceeded";
    } catch (const std::exception &e) {
      unsafe = true;
      m.reason = "detector failure";
      if (config_.verbose)
        std::cerr << "blurcam: " << e.what() << '\n';
    }
    m.detector_ms = ms(start);
  }
  cv::Rect2f current_region;
  std::vector<Face> selected;
  for (auto &f : tracker_.tracks()) {
    unsafe = unsafe || !f.reliable || f.confidence < config_.confidence;
    current_region |= f.cover.empty() ? f.box : f.cover;
    selected.push_back(f);
  }
  if (unsafe)
    recovery_ = 0;
  else if (m.detected)
    recovery_++;
  if (config_.privacy_failsafe && recovery_ < 2)
    unsafe = true;
  if (unsafe)
    known_region_ |= previous_region | current_region;
  else
    known_region_ = current_region;
  m.unsafe = unsafe;
  m.faces = int(selected.size());
  if (unsafe && m.reason.empty())
    m.reason = "tracking uncertain / awaiting confirmation";
  std::vector<cv::Rect> regions;
  if (config_.faces == "largest" && !selected.empty()) {
    auto f = *std::max_element(selected.begin(), selected.end(),
                               [](auto &a, auto &b) { return a.box.area() < b.box.area(); });
    selected = {f};
  } else if (config_.faces != "all" && config_.faces != "largest") {
    int id = std::stoi(config_.faces);
    std::erase_if(selected, [&](auto &f) { return f.id != id; });
  }
  for (auto &f : selected) {
    auto r = padded_rect(f.cover.empty() ? f.box : f.cover, frame.size(), config_.padding);
    if (!r.empty())
      regions.push_back(r);
  }
  start = Clock::now();
  if (config_.privacy_failsafe && unsafe) {
    // The safety layer uses opaque censoring, independent of decorative effect and mask settings.
    auto safety = padded_rect(known_region_, frame.size(), config_.padding);
    if (config_.failsafe_mode == "full-frame" || safety.empty() ||
        m.reason == "face limit exceeded")
      frame.setTo(cv::Scalar::all(0));
    else if (config_.failsafe_mode == "upper-body")
      frame(cv::Rect(0, 0, frame.cols, std::max(1, frame.rows * 3 / 4))).setTo(cv::Scalar::all(0));
    else {
      if (config_.failsafe_mode == "expanded-face")
        safety = padded_rect(cv::Rect2f(safety), frame.size(), .5);
      frame(safety).setTo(cv::Scalar::all(0));
    }
  } else
    effects_.render(frame, regions, frame_);
  m.effects_ms = ms(start);
  if (unsafe != last_unsafe_ && !config_.quiet) {
    std::cerr << "blurcam: " << (unsafe ? "TRACKING UNCERTAIN" : "tracking acquired")
              << (unsafe && config_.privacy_failsafe ? "; fail-safe active" : "") << '\n';
    last_unsafe_ = unsafe;
  }
  if (config_.debug) {
    for (auto &f : selected) {
      auto r = padded_rect(f.box, frame.size(), 0);
      cv::rectangle(frame, r, {0, 255, 255}, 2);
      cv::putText(frame,
                  "id=" + std::to_string(f.id) +
                      " score=" + std::to_string(f.confidence).substr(0, 4),
                  r.tl() + cv::Point(0, 15), cv::FONT_HERSHEY_SIMPLEX, .4, {0, 255, 255});
    }
    cv::putText(frame, unsafe ? "TRACKING UNCERTAIN" : "TRACKING", {10, 25},
                cv::FONT_HERSHEY_SIMPLEX, .6,
                unsafe ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0), 2);
  }
  frame_++;
  m.total_ms = ms(begin);
  return m;
}
} // namespace blurcam
