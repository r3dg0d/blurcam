#include "blurcam/vision.hpp"
#include <iostream>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
namespace blurcam {
YuNet::YuNet(const Config &c) : config_(c) {
  auto model = c.model.empty() ? default_model() : std::filesystem::path(c.model);
  if (!std::filesystem::exists(model))
    throw std::runtime_error(
        "Face model missing. Run: blurcam models install (or --model /path/yunet.onnx)");
  if (model_digest(model) != MODEL_SHA)
    throw std::runtime_error("YuNet SHA-256 mismatch; expected the pinned 2023mar model. Reinstall "
                             "with blurcam models install");
  auto targets = cv::dnn::getAvailableTargets(cv::dnn::DNN_BACKEND_CUDA);
  bool cuda = !targets.empty() && c.acceleration != "cpu";
  if (c.acceleration == "cuda" && !cuda)
    throw std::runtime_error(
        "CUDA DNN unavailable. Build OpenCV with CUDA/cuDNN or choose --backend cpu");
  backend_ = cuda ? "CUDA" : "CPU";
  auto_fallback_ = cuda && c.acceleration == "auto";
  net_ = cv::FaceDetectorYN::create(model.string(), "", {320, 320}, float(c.confidence), .3f, 5000,
                                    cuda ? cv::dnn::DNN_BACKEND_CUDA : cv::dnn::DNN_BACKEND_OPENCV,
                                    cuda ? cv::dnn::DNN_TARGET_CUDA : cv::dnn::DNN_TARGET_CPU);
}
std::vector<Face> YuNet::detect(const cv::Mat &image) {
  double scale = std::min(1., double(config_.detector_size) / std::max(image.cols, image.rows));
  cv::Mat small;
  cv::resize(image, small, {}, scale, scale);
  net_->setInputSize(small.size());
  cv::Mat faces;
  try {
    net_->detect(small, faces);
  } catch (const cv::Exception &) {
    if (!auto_fallback_)
      throw;
    Config cpu = config_;
    cpu.acceleration = "cpu";
    YuNet fallback(cpu);
    net_ = fallback.net_;
    backend_ = "CPU (CUDA failed)";
    auto_fallback_ = false;
    net_->setInputSize(small.size());
    net_->detect(small, faces);
    std::cerr << "blurcam: CUDA inference failed; using CPU\n";
  }
  std::vector<Face> out;
  double sx = double(image.cols) / small.cols, sy = double(image.rows) / small.rows;
  for (int i = 0; i < faces.rows; i++) {
    const auto *p = faces.ptr<float>(i);
    Face f;
    f.box = {float(p[0] * sx), float(p[1] * sy), float(p[2] * sx), float(p[3] * sy)};
    f.confidence = p[14];
    for (int j = 0; j < 5; j++)
      f.points.emplace_back(float(p[4 + j * 2] * sx), float(p[5 + j * 2] * sy));
    f.cover = f.box;
    if (!padded_rect(f.box, image.size(), 0).empty())
      out.push_back(f);
  }
  return out;
}
} // namespace blurcam
