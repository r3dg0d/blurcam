#include "blurcam/config.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>
namespace blurcam {
static std::filesystem::path xdg(const char *key, const char *suffix) {
  if (const auto *v = std::getenv(key); v && *v && std::filesystem::path(v).is_absolute())
    return v;
  const auto *home = std::getenv("HOME");
  if (!home || !*home)
    throw std::runtime_error("HOME or an absolute XDG directory must be set");
  return std::filesystem::path(home) / suffix;
}
std::filesystem::path data_path() {
  return xdg("XDG_DATA_HOME", ".local/share") / "blurcam/models";
}
std::filesystem::path config_path() {
  return xdg("XDG_CONFIG_HOME", ".config") / "blurcam/config.toml";
}
std::vector<std::string> split_effects(const std::vector<std::string> &in) {
  std::vector<std::string> out;
  for (auto &s : in) {
    size_t pos = 0, end;
    do {
      end = s.find(',', pos);
      auto part = s.substr(pos, end - pos);
      if (part.empty())
        throw std::runtime_error("Empty effect in stack");
      out.push_back(part);
      pos = end + 1;
    } while (end != std::string::npos);
  }
  return out;
}
void load_config(Config &c, const std::filesystem::path &path, const std::string &profile) {
  auto t = toml::parse_file(path.string());
  if (!profile.empty()) {
    auto *p = t["profiles"][profile].as_table();
    if (!p)
      throw std::runtime_error("Unknown user profile: " + profile);
    auto selected = *p;
    t = std::move(selected);
  }
  auto get = [&](const char *section, const char *key, auto &value) {
    using T = std::decay_t<decltype(value)>;
    if (auto n = t[section][key]; n) {
      auto v = n.value<T>();
      if (!v)
        throw std::runtime_error(std::string("Invalid config type: ") + section + "." + key);
      value = *v;
    }
  };
  get("detection", "confidence", c.confidence);
  get("detection", "interval", c.detection_interval);
  get("detection", "size", c.detector_size);
  get("detection", "max_faces", c.max_faces);
  get("detection", "model", c.model);
  get("tracking", "smoothing", c.smoothing);
  get("tracking", "padding", c.padding);
  get("tracking", "privacy_failsafe", c.privacy_failsafe);
  get("tracking", "failsafe_mode", c.failsafe_mode);
  get("effects", "mask", c.mask);
  get("effects", "blur_radius", c.blur_radius);
  get("effects", "blur_type", c.blur_type);
  get("effects", "pixel_size", c.pixel_size);
  get("effects", "seed", c.seed);
  get("effects", "image", c.image);
  if (auto *a = t["effects"]["stack"].as_array()) {
    c.effects.clear();
    for (auto &n : *a) {
      auto v = n.value<std::string>();
      if (!v)
        throw std::runtime_error("Effect stack must contain strings");
      c.effects.push_back(*v);
    }
  }
  get("camera", "width", c.width);
  get("camera", "height", c.height);
  get("camera", "fps", c.fps);
  get("output", "codec", c.codec);
  get("compute", "backend", c.acceleration);
  get("compute", "threads", c.threads);
}
void apply_preset(Config &c, const std::string &name) {
  c.preset = name;
  if (name == "privacy" || name == "anonymous") {
    c.effects = {"pixelate"};
    c.pixel_size = 36;
    c.padding = .4;
    c.privacy_failsafe = true;
  } else if (name == "max-privacy") {
    c.effects = {"censor"};
    c.padding = .5;
    c.privacy_failsafe = true;
  } else if (name == "cyberpunk" || name == "glitch" || name == "medium") {
    c.effects = {"glitch"};
    c.pixel_size = 18;
  } else if (name == "chaos") {
    c.effects = {"glitch"};
    c.pixel_size = 30;
  } else if (name == "subtle") {
    c.effects = {"rgbshift", "scanlines"};
  } else if (name == "VHS" || name == "vhs") {
    c.effects = {"vhs"};
  } else
    throw std::runtime_error("Unknown built-in preset: " + name +
                             " (use --profile for TOML profiles)");
}
void validate(const Config &c) {
  auto fail = [](bool ok, const char *s) {
    if (!ok)
      throw std::runtime_error(s);
  };
  fail(std::isfinite(c.confidence) && c.confidence > 0 && c.confidence <= 1,
       "confidence must be in (0,1]");
  fail(std::isfinite(c.padding) && c.padding >= 0 && c.padding <= 3, "padding must be in [0,3]");
  fail(std::isfinite(c.smoothing) && c.smoothing >= 0 && c.smoothing <= 1,
       "tracking-smoothing must be in [0,1]");
  fail(c.detection_interval >= 1 && c.detection_interval <= 60, "detection-interval must be 1..60");
  fail(c.detector_size >= 160 && c.detector_size <= 1920, "detector-size must be 160..1920");
  fail(c.max_faces >= 1 && c.max_faces <= 1024, "max-faces must be 1..1024");
  fail(c.blur_radius >= 1 && c.blur_radius <= 501, "blur-radius must be 1..501");
  fail(c.pixel_size >= 2 && c.pixel_size <= 1024, "pixel-size must be 2..1024");
  fail(c.width >= 16 && c.height >= 16 && c.width <= 7680 && c.height <= 4320,
       "resolution must be 16x16..7680x4320");
  fail(c.fps >= 1 && c.fps <= 240 && c.frames >= 1 && c.frames <= 100000,
       "invalid fps or benchmark frame count");
  fail(c.threads >= 1 && c.threads <= 64, "threads must be 1..64");
  fail(c.mask == "box" || c.mask == "ellipse",
       "mask must be box or ellipse; face/head segmentation is not implemented");
  fail(c.blur_type == "gaussian" || c.blur_type == "box", "blur-type must be gaussian or box");
  fail(c.faces == "all" || c.faces == "largest" ||
           (!c.faces.empty() && c.faces.find_first_not_of("0123456789") == std::string::npos &&
            c.faces.size() < 9),
       "faces must be all, largest or a non-negative tracking ID");
  fail(c.acceleration == "auto" || c.acceleration == "cpu" || c.acceleration == "cuda",
       "backend must be auto, cpu or cuda");
  fail(c.failsafe_mode == "full-frame" || c.failsafe_mode == "upper-body" ||
           c.failsafe_mode == "last-known-region" || c.failsafe_mode == "expanded-face",
       "invalid failsafe-mode");
  const std::set<std::string> codecs{"auto",       "h264",       "h265",      "vp9", "av1",
                                     "h264_nvenc", "hevc_nvenc", "av1_nvenc", "ffv1"};
  fail(codecs.contains(c.codec), "unsupported codec");
  const std::set<std::string> known{"blur",      "pixelate", "glitch", "rgb-glitch", "rgbshift",
                                    "scanlines", "vhs",      "censor", "invert",     "thermal",
                                    "posterize", "overlay",  "neon"};
  fail(!c.effects.empty(), "effect stack cannot be empty");
  for (auto &e : c.effects)
    fail(known.contains(e), ("Unknown effect: " + e).c_str());
  fail(std::find(c.effects.begin(), c.effects.end(), "overlay") == c.effects.end() ||
           !c.image.empty(),
       "overlay requires --image PNG");
}
void save_config(const Config &c, const std::filesystem::path &p) {
  if (std::filesystem::exists(p))
    throw std::runtime_error("Refusing to overwrite config: " + p.string());
  std::filesystem::create_directories(p.parent_path().empty() ? "." : p.parent_path());
  toml::array effects;
  for (auto &e : c.effects)
    effects.push_back(e);
  toml::table t{{"effects", toml::table{{"stack", effects},
                                        {"mask", c.mask},
                                        {"blur_radius", c.blur_radius},
                                        {"blur_type", c.blur_type},
                                        {"pixel_size", c.pixel_size},
                                        {"image", c.image},
                                        {"seed", c.seed}}},
                {"detection", toml::table{{"confidence", c.confidence},
                                          {"interval", c.detection_interval},
                                          {"size", c.detector_size},
                                          {"max_faces", c.max_faces}}},
                {"tracking", toml::table{{"smoothing", c.smoothing},
                                         {"padding", c.padding},
                                         {"privacy_failsafe", c.privacy_failsafe},
                                         {"failsafe_mode", c.failsafe_mode}}},
                {"camera", toml::table{{"width", c.width}, {"height", c.height}, {"fps", c.fps}}},
                {"compute", toml::table{{"backend", c.acceleration}, {"threads", c.threads}}},
                {"output", toml::table{{"codec", c.codec}}}};
  // Exclusive creation closes the check/open race, including symlinks.
  const auto text = [&] {
    std::ostringstream s;
    s << t;
    return s.str();
  }();
  extern void exclusive_write(const std::filesystem::path &, const std::string &);
  exclusive_write(p, text);
}
} // namespace blurcam
