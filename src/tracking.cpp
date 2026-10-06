#include "blurcam/vision.hpp"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
namespace blurcam {
static float iou(cv::Rect2f a, cv::Rect2f b) {
  float area = (a & b).area();
  return area / std::max(1.f, a.area() + b.area() - area);
}
void Tracker::reset() {
  tracks_.clear();
  previous_.release();
  lost_ = true;
}
static cv::Mat tracking_gray(const cv::Mat &frame) {
  cv::Mat small, gray;
  const double scale = std::min(1., 640. / std::max(frame.cols, frame.rows));
  if (scale < 1)
    cv::resize(frame, small, {}, scale, scale, cv::INTER_LINEAR);
  else
    small = frame;
  cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
  return gray;
}
void Tracker::predict(const cv::Mat &frame) {
  auto gray = tracking_gray(frame);
  const float sx = float(frame.cols) / gray.cols, sy = float(frame.rows) / gray.rows;
  lost_ = false;
  if (previous_.empty() || previous_.size() != gray.size()) {
    if (!tracks_.empty())
      lost_ = true;
    previous_ = gray;
    return;
  }
  std::vector<cv::Mat> previous_pyramid, current_pyramid;
  if (!tracks_.empty()) {
    cv::buildOpticalFlowPyramid(previous_, previous_pyramid, {21, 21}, 3);
    cv::buildOpticalFlowPyramid(gray, current_pyramid, {21, 21}, 3);
  }
  for (auto &f : tracks_) {
    std::vector<cv::Point2f> pts;
    auto r = padded_rect({f.box.x / sx, f.box.y / sy, f.box.width / sx, f.box.height / sy},
                         gray.size(), 0);
    if (r.empty()) {
      f.reliable = false;
      lost_ = true;
      continue;
    }
    cv::goodFeaturesToTrack(previous_(r), pts, 40, .01, 4);
    for (auto &point : pts)
      point += cv::Point2f(float(r.x), float(r.y));
    if (pts.size() < 6) {
      f.reliable = false;
      lost_ = true;
      continue;
    }
    std::vector<cv::Point2f> next, back;
    std::vector<uchar> status, backstatus;
    std::vector<float> errors, backerrors;
    cv::calcOpticalFlowPyrLK(previous_pyramid, current_pyramid, pts, next, status, errors, {21, 21},
                             3);
    cv::calcOpticalFlowPyrLK(current_pyramid, previous_pyramid, next, back, backstatus, backerrors,
                             {21, 21}, 3);
    std::vector<float> dx, dy;
    for (size_t j = 0; j < pts.size(); j++)
      if (status[j] && backstatus[j] && errors[j] < 30 && cv::norm(pts[j] - back[j]) < 1.5 &&
          next[j].x >= 0 && next[j].y >= 0 && next[j].x < gray.cols && next[j].y < gray.rows) {
        dx.push_back(next[j].x - pts[j].x);
        dy.push_back(next[j].y - pts[j].y);
      }
    if (dx.size() < 6 || dx.size() < pts.size() / 2) {
      f.reliable = false;
      lost_ = true;
      continue;
    }
    auto median = [](std::vector<float> &v) {
      auto p = v.begin() + v.size() / 2;
      std::nth_element(v.begin(), p, v.end());
      return *p;
    };
    float x = median(dx) * sx, y = median(dy) * sy;
    if (std::abs(x) > f.box.width * .7 || std::abs(y) > f.box.height * .7) {
      f.reliable = false;
      lost_ = true;
      continue;
    }
    auto old = f.box;
    f.box.x += x;
    f.box.y += y;
    auto propagated = f.cover.empty() ? old : f.cover;
    propagated.x += x;
    propagated.y += y;
    f.cover = propagated | old | f.box;
    f.reliable = true;
    for (auto &p : f.points)
      p += cv::Point2f(x, y);
    if (padded_rect(f.box, frame.size(), 0).empty()) {
      f.reliable = false;
      lost_ = true;
    }
  }
  previous_ = gray;
}
void Tracker::refresh(const std::vector<Face> &detections, const cv::Mat &frame) {
  std::vector<bool> used(tracks_.size(), false);
  std::vector<Face> updated;
  bool missing = false;
  for (auto d : detections) {
    float best = .15f;
    int match = -1;
    for (size_t i = 0; i < tracks_.size(); i++)
      if (!used[i]) {
        float score = iou(d.box, tracks_[i].box);
        if (score > best) {
          best = score;
          match = int(i);
        }
      }
    if (match >= 0) {
      auto old = tracks_[match];
      used[match] = true;
      d.id = old.id;
      float a = float(config_.smoothing);
      d.box = {a * old.box.x + (1 - a) * d.box.x, a * old.box.y + (1 - a) * d.box.y,
               a * old.box.width + (1 - a) * d.box.width,
               a * old.box.height + (1 - a) * d.box.height};
      // Smoothing never contracts coverage away from a fresh detection.
      d.cover = d.box | detections[updated.size()].box | old.box;
    } else {
      d.id = next_id_++;
      d.cover = d.box;
    }
    updated.push_back(d);
  }
  for (size_t i = 0; i < tracks_.size(); i++)
    if (!used[i]) {
      auto f = tracks_[i];
      f.reliable = false;
      updated.push_back(f);
      missing = true;
    }
  // Stale boxes remain available for conservative recovery; cap memory after detector confirmation.
  if (updated.size() > size_t(config_.max_faces * 2)) {
    updated.resize(config_.max_faces * 2);
    missing = true;
  }
  tracks_ = std::move(updated);
  lost_ = missing;
  // Successful detections of all currently visible faces reset stale identities after one protected
  // frame.
  if (missing) {
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(), [](const Face &f) { return !f.reliable; }),
        tracks_.end());
  }
  previous_ = tracking_gray(frame);
}
} // namespace blurcam
