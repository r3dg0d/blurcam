#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace blurcam {
struct Config {
  std::vector<std::string> effects{"blur"};
  std::string preset, mask = "box", blur_type = "gaussian", faces = "all",
                      failsafe_mode = "full-frame";
  std::string acceleration = "auto", codec = "auto", model, image;
  double confidence = .75, padding = .3, smoothing = .45;
  int detection_interval = 5, detector_size = 640, max_faces = 32, blur_radius = 45,
      pixel_size = 20, seed = 0, threads = 2;
  int width = 1280, height = 720, fps = 30, frames = 120;
  bool privacy_failsafe = false, debug = false, verbose = false, quiet = false, overwrite = false;
};
std::filesystem::path data_path();
std::filesystem::path config_path();
void load_config(Config &, const std::filesystem::path &, const std::string &profile = "");
void apply_preset(Config &, const std::string &);
void validate(const Config &);
void save_config(const Config &, const std::filesystem::path &);
std::vector<std::string> split_effects(const std::vector<std::string> &);
} // namespace blurcam
