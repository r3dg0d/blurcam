#include "blurcam/vision.hpp"
#include <algorithm>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
namespace blurcam {
const std::vector<EffectInfo> &effect_catalog() {
  static const std::vector<EffectInfo> v = {
      {"blur", "Gaussian or box blur", "--blur-radius 45 --blur-type gaussian|box"},
      {"pixelate", "Nearest-neighbor mosaic", "--pixel-size 20"},
      {"glitch", "Pixelation, channel splits, slice tearing, blocks and scanlines",
       "--pixel-size 20 --preset subtle|medium|chaos|privacy --seed 0"},
      {"rgb-glitch", "Alias for glitch", "--pixel-size 20 --seed 0"},
      {"rgbshift", "Chromatic channel separation (decorative)",
       "--pixel-size controls displacement"},
      {"scanlines", "Dark CRT lines (decorative)", "--pixel-size controls spacing"},
      {"vhs", "Analog-style noise, chroma drift and scanlines", "--seed 0"},
      {"censor", "Opaque black rectangle", "none"},
      {"invert", "Color negative (decorative)", "none"},
      {"thermal", "False-color heatmap (decorative)", "none"},
      {"posterize", "Reduced-color digital portrait (decorative)", "none"},
      {"neon", "Canny edges on dark background (decorative)", "none"},
      {"overlay", "Scaled RGBA image (transparent pixels reveal input)", "--image image.png"}};
  return v;
}
cv::Rect padded_rect(cv::Rect2f r, cv::Size size, double p) {
  if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
      !std::isfinite(r.height) || r.width <= 0 || r.height <= 0)
    return {};
  const auto left = std::clamp(std::floor(r.x - r.width * p), 0.0, double(size.width));
  const auto top = std::clamp(std::floor(r.y - r.height * p), 0.0, double(size.height));
  const auto right = std::clamp(std::ceil(r.x + r.width * (1 + p)), 0.0, double(size.width));
  const auto bottom = std::clamp(std::ceil(r.y + r.height * (1 + p)), 0.0, double(size.height));
  return {int(left), int(top), std::max(0, int(right - left)), std::max(0, int(bottom - top))};
}
Effects::Effects(const Config &c) : config_(c), random_(uint64_t(c.seed)) { change(c); }
void Effects::change(const Config &c) {
  config_ = c;
  if (std::find(c.effects.begin(), c.effects.end(), "overlay") != c.effects.end()) {
    overlay_ = cv::imread(c.image, cv::IMREAD_UNCHANGED);
    if (overlay_.empty() || overlay_.channels() != 4 || overlay_.depth() != CV_8U)
      throw std::runtime_error("Overlay must be an 8-bit RGBA image");
  }
}
void Effects::apply(cv::Mat &roi, const std::string &name, uint64_t frame) {
  if (name == "censor") {
    roi.setTo(cv::Scalar::all(0));
    return;
  }
  if (name == "blur") {
    int k = config_.blur_radius | 1;
    if (config_.blur_type == "box")
      cv::blur(roi, roi, {k, k});
    else
      cv::GaussianBlur(roi, roi, {k, k}, 0);
    return;
  }
  auto pixel = [&] {
    cv::Mat low;
    cv::resize(
        roi, low,
        {std::max(1, roi.cols / config_.pixel_size), std::max(1, roi.rows / config_.pixel_size)}, 0,
        0, cv::INTER_AREA);
    cv::resize(low, roi, roi.size(), 0, 0, cv::INTER_NEAREST);
  };
  if (name == "pixelate") {
    pixel();
    return;
  }
  auto rgb = [&] {
    cv::Mat original = roi.clone();
    int offset = std::max(1, config_.pixel_size / 3);
    for (int y = 0; y < roi.rows; y++)
      for (int x = 0; x < roi.cols; x++) {
        auto &p = roi.at<cv::Vec3b>(y, x);
        p[2] = original.at<cv::Vec3b>(y, std::clamp(x - offset, 0, roi.cols - 1))[2];
        p[0] = original.at<cv::Vec3b>(y, std::clamp(x + offset, 0, roi.cols - 1))[0];
      }
  };
  auto lines = [&] {
    for (int y = int(frame % 3); y < roi.rows; y += 3)
      roi.row(y).convertTo(roi.row(y), -1, .55);
  };
  if (name == "rgbshift") {
    rgb();
    return;
  }
  if (name == "scanlines") {
    lines();
    return;
  }
  if (name == "glitch" || name == "rgb-glitch" || name == "vhs") {
    if (name != "vhs")
      pixel();
    rgb();
    for (int i = 0; i < 4; i++) {
      int y = random_.uniform(0, roi.rows), h = std::min(random_.uniform(2, 20), roi.rows - y),
          shift = random_.uniform(-config_.pixel_size, config_.pixel_size + 1);
      cv::Mat src = roi(cv::Rect(0, y, roi.cols, h)).clone();
      for (int x = 0; x < roi.cols; x++)
        src.col(std::clamp(x + shift, 0, roi.cols - 1)).copyTo(roi(cv::Rect(x, y, 1, h)));
    }
    for (int i = 0; i < 3; i++) {
      int x = random_.uniform(0, roi.cols), y = random_.uniform(0, roi.rows);
      cv::Rect b(x, y, std::min(config_.pixel_size, roi.cols - x),
                 std::min(config_.pixel_size / 2 + 1, roi.rows - y));
      roi(b).setTo(
          cv::Scalar(random_.uniform(0, 90), random_.uniform(0, 255), random_.uniform(0, 255)));
    }
    if (name == "vhs") {
      cv::Mat noise(roi.size(), CV_8SC3);
      random_.fill(noise, cv::RNG::NORMAL, 0, 12);
      cv::add(roi, noise, roi, cv::noArray(), CV_8UC3);
    }
    lines();
    return;
  }
  if (name == "invert") {
    cv::bitwise_not(roi, roi);
    return;
  }
  if (name == "thermal") {
    cv::Mat gray;
    cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    cv::applyColorMap(gray, roi, cv::COLORMAP_TURBO);
    return;
  }
  if (name == "posterize") {
    for (int y = 0; y < roi.rows; y++)
      for (int x = 0; x < roi.cols; x++)
        for (int k = 0; k < 3; k++) {
          auto &p = roi.at<cv::Vec3b>(y, x)[k];
          p = uchar((p / 64) * 85);
        }
    return;
  }
  if (name == "neon") {
    cv::Mat gray, edge;
    cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, edge, 60, 150);
    roi.setTo(cv::Scalar::all(0));
    roi.setTo(cv::Scalar(240, 255, 30), edge);
    return;
  }
  if (name == "overlay") {
    cv::Mat image;
    cv::resize(overlay_, image, roi.size());
    for (int y = 0; y < roi.rows; y++)
      for (int x = 0; x < roi.cols; x++) {
        auto a = image.at<cv::Vec4b>(y, x);
        auto &b = roi.at<cv::Vec3b>(y, x);
        for (int k = 0; k < 3; k++)
          b[k] = uchar((a[k] * a[3] + b[k] * (255 - a[3]) + 127) / 255);
      }
    return;
  }
  throw std::runtime_error("Unknown effect " + name);
}
void Effects::render(cv::Mat &image, const std::vector<cv::Rect> &regions, uint64_t frame) {
  // Compute each face from the same source, then union masks. Overlapping effects cannot undo a
  // censor.
  std::vector<cv::Rect> clipped;
  std::vector<cv::Mat> originals;
  for (auto r : regions) {
    r &= cv::Rect(0, 0, image.cols, image.rows);
    if (!r.empty()) {
      clipped.push_back(r);
      originals.push_back(image(r).clone());
    }
  }
  for (size_t i = 0; i < clipped.size(); ++i) {
    auto r = clipped[i];
    r &= cv::Rect(0, 0, image.cols, image.rows);
    if (r.empty())
      continue;
    cv::Mat roi = originals[i];
    for (auto &e : config_.effects)
      apply(roi, e, frame);
    if (config_.mask == "ellipse") {
      cv::Mat mask(r.size(), CV_8U, cv::Scalar(0));
      cv::ellipse(mask, {r.width / 2, r.height / 2},
                  {std::max(1, r.width / 2), std::max(1, r.height / 2)}, 0, 0, 360, 255, -1);
      roi.copyTo(image(r), mask);
    } else
      roi.copyTo(image(r));
  }
}
} // namespace blurcam
