#pragma once
#include "config.hpp"
#include <functional>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/objdetect/face.hpp>
namespace blurcam {
struct Face {
  cv::Rect2f box;
  float confidence = 1;
  int id = -1;
  bool reliable = true;
  std::vector<cv::Point2f> points;
  cv::Rect2f cover;
};
class Detector {
public:
  virtual ~Detector() = default;
  virtual std::vector<Face> detect(const cv::Mat &) = 0;
  virtual std::string backend() const = 0;
};
class YuNet final : public Detector {
  cv::Ptr<cv::FaceDetectorYN> net_;
  Config config_;
  std::string backend_;
  bool auto_fallback_ = false;

public:
  explicit YuNet(const Config &);
  std::vector<Face> detect(const cv::Mat &) override;
  std::string backend() const override { return backend_; }
};
class Tracker {
  cv::Mat previous_;
  std::vector<Face> tracks_;
  int next_id_ = 0;
  bool lost_ = false;
  Config config_;

public:
  explicit Tracker(const Config &c) : config_(c) {};
  void predict(const cv::Mat &);
  void refresh(const std::vector<Face> &, const cv::Mat &);
  void reset();
  const std::vector<Face> &tracks() const { return tracks_; }
  bool lost() const { return lost_; }
};
cv::Rect padded_rect(cv::Rect2f, cv::Size, double);
struct EffectInfo {
  std::string name, description, parameters;
};
const std::vector<EffectInfo> &effect_catalog();
class Effects {
  Config config_;
  cv::Mat overlay_;
  cv::RNG random_;
  void apply(cv::Mat &, const std::string &, uint64_t);

public:
  explicit Effects(const Config &);
  void render(cv::Mat &, const std::vector<cv::Rect> &, uint64_t);
  void change(const Config &);
};
struct Metrics {
  double detector_ms = 0, tracker_ms = 0, effects_ms = 0, total_ms = 0;
  int faces = 0;
  bool unsafe = true, detected = false;
  std::string reason;
};
class Pipeline {
  Config config_;
  std::unique_ptr<Detector> detector_;
  Tracker tracker_;
  Effects effects_;
  uint64_t frame_ = 0;
  cv::Size size_;
  bool last_unsafe_ = false;
  int recovery_ = 0;

public:
  explicit Pipeline(const Config &, std::unique_ptr<Detector> = {});
  Metrics process(cv::Mat &);
  void change_effect(const std::string &);
  void adjust_strength(int);
  void toggle_debug();
  std::string backend() const { return detector_->backend(); }
};
std::string model_digest(const std::filesystem::path &);
inline constexpr const char *MODEL_SHA =
    "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4";
std::filesystem::path default_model();
void install_model();
void list_models();
} // namespace blurcam
