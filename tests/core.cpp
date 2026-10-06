#include "blurcam/vision.hpp"
#include <fstream>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <unistd.h>
using namespace blurcam;
static int checks = 0;
static void expect(bool condition, const char *text) {
  checks++;
  if (!condition)
    throw std::runtime_error(text);
}
static bool equal(const cv::Mat &a, const cv::Mat &b) { return cv::norm(a, b, cv::NORM_INF) == 0; }
struct Fake : Detector {
  std::vector<Face> results;
  bool throws = false;
  std::vector<Face> detect(const cv::Mat &) override {
    if (throws)
      throw std::runtime_error("simulated detector failure");
    return results;
  }
  std::string backend() const override { return "test"; }
};
int main() {
  try {
    auto golden_input = cv::imread(std::string(BLURCAM_FIXTURE_DIR) + "/gradient.ppm");
    auto golden_expected = cv::imread(std::string(BLURCAM_FIXTURE_DIR) + "/pixelate-expected.ppm");
    expect(!golden_input.empty() && !golden_expected.empty(), "Visual fixtures load");
    Config golden_config;
    golden_config.effects = {"pixelate"};
    golden_config.pixel_size = 4;
    Effects golden_effect(golden_config);
    golden_effect.render(golden_input, {{0, 0, 8, 8}}, 0);
    expect(equal(golden_input, golden_expected),
           "Pixelate matches independently computed visual golden");
    Config c;
    c.quiet = true;
    cv::setNumThreads(2);
    cv::Mat original(128, 160, CV_8UC3);
    cv::RNG rng(42);
    rng.fill(original, cv::RNG::UNIFORM, 0, 255);
    expect(padded_rect({-10, -10, 30, 30}, original.size(), .5) == cv::Rect(0, 0, 35, 35),
           "Padding/clipping");
    expect(padded_rect({150, 110, 30, 30}, original.size(), .25) == cv::Rect(142, 102, 18, 26),
           "Far edge clipping");
    for (auto &effect : effect_catalog()) {
      if (effect.name == "overlay")
        continue;
      c.effects = {effect.name};
      Effects e(c);
      auto image = original.clone();
      cv::Rect roi(40, 30, 50, 60);
      e.render(image, {roi}, 3);
      expect(!equal(image(roi), original(roi)), "Effect modifies ROI");
      expect(equal(image(cv::Rect(0, 0, 30, 128)), original(cv::Rect(0, 0, 30, 128))),
             "Effect preserves outside ROI");
      Effects deterministic(c);
      auto second = original.clone();
      deterministic.render(second, {roi}, 3);
      expect(equal(image, second), "Effect reproducibility");
    }
    c.effects = {"censor"};
    c.mask = "ellipse";
    Effects ellipse(c);
    auto image = original.clone();
    ellipse.render(image, {{40, 30, 50, 60}}, 0);
    expect(image.at<cv::Vec3b>(60, 65) == cv::Vec3b(0, 0, 0), "Ellipse center");
    expect(image.at<cv::Vec3b>(30, 40) == original.at<cv::Vec3b>(30, 40),
           "Ellipse corner unchanged");
    c.mask = "box";
    c.privacy_failsafe = true;
    c.effects = {"invert"};
    c.detection_interval = 1;
    auto fake = std::make_unique<Fake>();
    auto *detector = fake.get();
    Pipeline p(c, std::move(fake));
    auto f = original.clone();
    auto m = p.process(f);
    expect(m.unsafe && cv::norm(f) == 0, "No face must fail closed");
    Face face;
    face.box = {40, 30, 50, 60};
    face.cover = face.box;
    detector->results = {face};
    f = original.clone();
    expect(p.process(f).unsafe && cv::norm(f) == 0, "First confirmation remains censored");
    f = original.clone();
    expect(!p.process(f).unsafe, "Second confirmation reacquires");
    detector->throws = true;
    f = original.clone();
    expect(p.process(f).unsafe && cv::norm(f) == 0, "Detector error fails closed");
    detector->throws = false;
    detector->results.clear();
    f = original.clone();
    expect(p.process(f).unsafe && cv::norm(f) == 0, "Face disappearance fails closed");
    detector->results = {face};
    for (int i = 0; i < 2; i++) {
      f = original.clone();
      p.process(f);
    }
    f = cv::Mat(80, 120, CV_8UC3, cv::Scalar(123, 80, 40));
    expect(p.process(f).unsafe && cv::norm(f) == 0, "Resolution change needs renewed confirmation");
    c.max_faces = 1;
    fake = std::make_unique<Fake>();
    detector = fake.get();
    detector->results = {face, face};
    Pipeline crowded(c, std::move(fake));
    for (int i = 0; i < 3; i++) {
      f = original.clone();
      expect(crowded.process(f).unsafe && cv::norm(f) == 0, "Overflow fails closed");
    }
    // Uniform/occluded frames cannot sustain optical flow; full-frame protection is independent of
    // the chosen effect.
    c.max_faces = 32;
    c.detection_interval = 30;
    fake = std::make_unique<Fake>();
    detector = fake.get();
    detector->results = {face};
    Pipeline occluded(c, std::move(fake));
    for (int i = 0; i < 3; i++) {
      f = original.clone();
      occluded.process(f);
    }
    detector->results.clear();
    f = cv::Mat(original.size(), CV_8UC3, cv::Scalar::all(80));
    expect(occluded.process(f).unsafe && cv::norm(f) == 0, "Occlusion/flow failure fails closed");
    // Confidence is checked again in the safety gate, even for a backend that returns low scores.
    c.detection_interval = 1;
    fake = std::make_unique<Fake>();
    detector = fake.get();
    face.confidence = .2;
    detector->results = {face};
    Pipeline low(c, std::move(fake));
    f = original.clone();
    expect(low.process(f).unsafe && cv::norm(f) == 0, "Low confidence fails closed");
    face.confidence = 1;
    c.privacy_failsafe = true;
    c.failsafe_mode = "last-known-region";
    c.detection_interval = 1;
    fake = std::make_unique<Fake>();
    detector = fake.get();
    Face first, second;
    first.box = {20, 20, 30, 30};
    first.cover = first.box;
    second.box = {100, 70, 30, 30};
    second.cover = second.box;
    detector->results = {first, second};
    Pipeline regional(c, std::move(fake));
    for (int i = 0; i < 2; i++) {
      f = original.clone();
      regional.process(f);
    }
    detector->results = {first};
    f = original.clone();
    expect(regional.process(f).unsafe, "Loss of one of two faces activates regional fail-safe");
    expect(cv::norm(f(cv::Rect(100, 70, 30, 30))) == 0,
           "Regional fail-safe retains the disappeared face area");
    f = original.clone();
    regional.process(f);
    expect(cv::norm(f(cv::Rect(100, 70, 30, 30))) == 0,
           "Regional protection persists through confirmation recovery");
    c.failsafe_mode = "full-frame";
    c.privacy_failsafe = false;
    Tracker tracker(c);
    tracker.refresh({face}, original);
    int id = tracker.tracks()[0].id;
    auto moved = face;
    moved.box.x += 10;
    moved.cover = moved.box;
    tracker.refresh({moved}, original);
    expect(tracker.tracks()[0].id == id, "Stable associated identity");
    expect(tracker.tracks()[0].cover.x <= face.box.x &&
               tracker.tracks()[0].cover.br().x >= moved.box.br().x,
           "Smoothing covers raw movement");
    auto before_cover = tracker.tracks()[0].cover;
    tracker.predict(original);
    expect(tracker.tracks()[0].cover.br().x >= before_cover.br().x - 1,
           "Tracking retains fresh raw detection coverage");
    // Optical-flow translation of actual texture, including skipped-frame movement.
    tracker.reset();
    tracker.refresh({face}, original);
    cv::Mat translated;
    cv::Mat affine = (cv::Mat_<double>(2, 3) << 1, 0, 4, 0, 1, 3);
    cv::warpAffine(original, translated, affine, original.size());
    tracker.predict(translated);
    expect(!tracker.lost(), "Optical flow tracks texture");
    expect(std::abs(tracker.tracks()[0].box.x - 44) < 2, "Flow translation");
    Config config;
    apply_preset(config, "max-privacy");
    expect(config.privacy_failsafe && config.effects[0] == "censor", "Privacy preset");
    config.confidence = 2;
    bool rejected = false;
    try {
      validate(config);
    } catch (...) {
      rejected = true;
    }
    expect(rejected, "Invalid confidence rejected");
    auto dir =
        std::filesystem::temp_directory_path() / ("blurcam-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    auto path = dir / "config.toml";
    c.privacy_failsafe = true;
    c.effects = {"pixelate", "rgbshift"};
    save_config(c, path);
    Config loaded;
    load_config(loaded, path);
    expect(loaded.effects == c.effects && loaded.privacy_failsafe, "TOML roundtrip");
    rejected = false;
    try {
      save_config(c, path);
    } catch (...) {
      rejected = true;
    }
    expect(rejected, "Config cannot overwrite");
    auto png = dir / "overlay.png";
    cv::Mat rgba(10, 10, CV_8UC4, cv::Scalar(10, 20, 30, 255));
    cv::imwrite(png.string(), rgba);
    c.effects = {"overlay"};
    c.image = png.string();
    Effects overlay(c);
    f = original.clone();
    overlay.render(f, {{40, 30, 50, 60}}, 0);
    expect(f.at<cv::Vec3b>(50, 60) == cv::Vec3b(10, 20, 30), "Opaque RGBA overlay");
    std::filesystem::remove_all(dir);
    if (const auto *model = std::getenv("BLURCAM_TEST_MODEL")) {
      Config detector_config;
      detector_config.model = model;
      detector_config.acceleration = "cpu";
      detector_config.quiet = true;
      YuNet net(detector_config);
      auto fixture = cv::imread(std::string(BLURCAM_FIXTURE_DIR) + "/astronaut.png");
      expect(!fixture.empty(), "Public-domain detector fixture loads");
      auto found = net.detect(fixture);
      expect(found.size() == 1, "YuNet detects NASA sample face");
      cv::Mat two;
      cv::hconcat(fixture, fixture, two);
      expect(net.detect(two).size() == 2, "YuNet detects multiple faces");
      detector_config.effects = {"censor"};
      Pipeline actual(detector_config);
      auto processed = fixture.clone();
      actual.process(processed);
      auto region = padded_rect(found[0].box, fixture.size(), detector_config.padding);
      expect(cv::norm(processed(region)) == 0, "Real detector ROI is censored");
      expect(equal(fixture(cv::Rect(0, 180, 256, 76)), processed(cv::Rect(0, 180, 256, 76))),
             "Real detector preserves background");
    }
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Test failed: " << e.what() << '\n';
    return 1;
  }
}
