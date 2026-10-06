#include "blurcam/media.hpp"
#include <CLI/CLI.hpp>
#include <iostream>
#include <opencv2/dnn.hpp>
extern "C" {
#include <libavutil/log.h>
}
namespace bc = blurcam;
static void signal_handler(int) { bc::interrupted = 1; }
int main(int argc, char **argv) {
  try {
    bc::Config c;
    std::string config = bc::config_path().string(), preset, profile, save;
    // Bootstrap the precedence-sensitive options before binding the final CLI.
    for (int i = 1; i < argc; i++) {
      std::string s = argv[i];
      auto read = [&](const std::string &flag, std::string &value) {
        if (s == flag) {
          if (i + 1 >= argc)
            throw std::runtime_error(flag + " requires a value");
          value = argv[++i];
          return true;
        }
        if (s.starts_with(flag + "=")) {
          value = s.substr(flag.size() + 1);
          return true;
        }
        return false;
      };
      if (read("--config", config) || read("--preset", preset) || read("--profile", profile))
        continue;
    }
    if (std::filesystem::exists(config))
      bc::load_config(c, config);
    else if (config != bc::config_path().string())
      throw std::runtime_error("Config file does not exist: " + config);
    if (!preset.empty())
      bc::apply_preset(c, preset);
    if (!profile.empty())
      bc::load_config(c, config, profile);
    CLI::App app{"BlurCam — blur, pixelate and anonymize faces locally"};
    app.set_version_flag("--version", "blurcam 0.1.0");
    app.require_subcommand(1);
    app.fallthrough();
    app.add_option("--config", config, "TOML configuration path");
    app.add_option(
        "--preset", preset,
        "privacy, anonymous, max-privacy, cyberpunk, glitch, VHS, subtle, medium, chaos");
    app.add_option("--profile", profile, "User profile in [profiles.NAME] TOML tables");
    app.add_option("--save-config", save, "Save effective options without overwriting");
    app.add_flag("-q,--quiet", c.quiet, "Suppress routine stderr logs");
    app.add_flag("-v,--verbose", c.verbose, "Show backend selection");
    app.add_flag("--debug", c.debug, "Render tracking IDs and uncertainty on output");
    app.add_option("-e,--effect", c.effects, "Effect stack, comma-separated or repeated")
        ->delimiter(',');
    app.add_option("--model", c.model, "Path to checksum-pinned YuNet ONNX model");
    app.add_option("--backend", c.acceleration, "Inference auto|cpu|cuda; camera uses --device");
    app.add_option("--confidence", c.confidence, "Detection score (0,1]");
    app.add_option("--detection-interval", c.detection_interval,
                   "Detect every N processed frames (1..60)");
    app.add_option("--detector-size", c.detector_size, "Max detector input side (160..1920)");
    app.add_option("--max-faces", c.max_faces,
                   "Max faces; overflow activates fail-safe when enabled");
    app.add_option("--tracking-smoothing", c.smoothing,
                   "Previous box weight [0,1]; coverage includes fresh detection");
    app.add_option("--padding", c.padding, "Fraction of face width/height added on every side");
    app.add_option("--faces", c.faces, "all|largest|tracking-ID");
    app.add_option("--mask", c.mask, "box|ellipse");
    app.add_flag("--privacy-failsafe", c.privacy_failsafe,
                 "Opaque covering on uncertainty, including startup");
    app.add_flag(
        "--no-privacy-failsafe", [&](int64_t) { c.privacy_failsafe = false; },
        "Explicitly disable preset/config fail-safe");
    app.add_option("--failsafe-mode", c.failsafe_mode,
                   "full-frame|upper-body|last-known-region|expanded-face");
    app.add_option("--blur-radius", c.blur_radius, "Blur kernel width (1..501, rounded up to odd)");
    app.add_option("--blur-type", c.blur_type, "gaussian|box");
    app.add_option("--pixel-size", c.pixel_size, "Mosaic block size in pixels");
    app.add_option("--seed", c.seed, "Deterministic effect RNG seed");
    app.add_option("--image", c.image, "RGBA overlay image");
    app.add_option("--threads", c.threads, "OpenCV/codec worker count (1..64)");
    auto *file = app.add_subcommand(
        "file", "Process file/stdin; preserve timestamps and copy all compatible audio streams");
    file->fallthrough();
    std::string input, output, positional;
    file->add_option("input", input, "Input video or - for stdin")->required();
    file->add_option("destination", positional, "Output .mp4/.mkv/.webm or - (streaming MKV)");
    file->add_option("-o,--output", output, "Output destination");
    file->add_option("--codec", c.codec,
                     "auto|h264|h265|vp9|av1|ffv1|h264_nvenc|hevc_nvenc|av1_nvenc");
    file->add_flag("--overwrite", c.overwrite, "Atomically replace an existing output");
    std::string camera = "/dev/video0", virtual_output = "/dev/video10", test_input, live_output;
    int limit = 0;
    bool no_preview = false;
    auto *preview =
        app.add_subcommand("preview", "Filtered webcam preview; q quits, 1..5 switch effects");
    auto *webcam = app.add_subcommand("webcam", "Filtered camera, optionally raw BGR24 stdout");
    auto *virtualcam =
        app.add_subcommand("virtualcam", "Publish to an existing v4l2loopback device");
    for (auto *sub : {preview, webcam, virtualcam}) {
      sub->fallthrough();
      sub->add_option("--device", camera, "Camera /dev/videoN or exact friendly name");
      sub->add_option("--input", test_input,
                      "Paced local file for replay/testing instead of camera");
      sub->add_option("--width", c.width, "Requested camera width");
      sub->add_option("--height", c.height, "Requested camera height");
      sub->add_option("--fps", c.fps, "Requested camera fps");
      sub->add_option("--limit-frames", limit,
                      "Stop after N processed frames; 0 runs until EOF/quit")
          ->check(CLI::Range(0, 100000));
    }
    virtualcam->add_option("-o,--output", virtual_output,
                           "Loopback device or - for raw BGR24 stdout");
    virtualcam->add_flag("--preview", no_preview, "Also show SDL preview");
    webcam->add_option("-o,--output", live_output, "Loopback device or - for raw BGR24 stdout");
    webcam->add_flag("--no-preview", no_preview, "Disable SDL preview (requires output)");
    auto *dev = app.add_subcommand("devices", "List accessible V4L2 devices and supported modes");
    bool short_devices = false;
    dev->add_flag("--short", short_devices, "Hide mode details");
    auto *eff = app.add_subcommand("effects", "List effects or inspect one");
    std::string effect;
    eff->add_option("name", effect);
    auto *bench = app.add_subcommand(
        "benchmark", "Measure detector, tracker, masks and effects on synthetic frames");
    bench->fallthrough();
    std::string resolution = "1280x720";
    bench->add_option("--resolution", resolution, "WIDTHxHEIGHT");
    bench->add_option("--fps", c.fps, "Target frame-rate budget");
    bench->add_option("--frames", c.frames, "Measured frames after ten warmup frames");
    auto *inf = app.add_subcommand("info", "Show runtime libraries and selected capabilities");
    auto *models = app.add_subcommand("models", "Install/list checksum-pinned offline model");
    models->require_subcommand(0, 1);
    auto *install = models->add_subcommand(
        "install", "Download fixed MIT YuNet weights over HTTPS and verify SHA256");
    models->add_subcommand("list", "Show model status");
    auto *completions = app.add_subcommand("completions", "Emit Bash, Zsh or Fish completion");
    std::string shell;
    completions->add_option("shell", shell)
        ->required()
        ->check(CLI::IsMember({"bash", "zsh", "fish"}));
    try {
      app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
      return app.exit(e);
    }
    if (*bench) {
      auto pos = resolution.find('x');
      if (pos == std::string::npos)
        throw std::runtime_error("resolution must be WIDTHxHEIGHT");
      size_t end = 0;
      c.width = std::stoi(resolution.substr(0, pos), &end);
      if (end != pos)
        throw std::runtime_error("Invalid resolution");
      auto height = resolution.substr(pos + 1);
      c.height = std::stoi(height, &end);
      if (end != height.size())
        throw std::runtime_error("Invalid resolution");
    }
    c.effects = bc::split_effects(c.effects);
    bc::validate(c);
    cv::setNumThreads(c.threads);
    av_log_set_level(c.verbose ? AV_LOG_INFO : AV_LOG_ERROR);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);
    if (!save.empty())
      bc::save_config(c, save);
    if (*file) {
      if (output.empty())
        output = positional;
      else if (!positional.empty())
        throw std::runtime_error("Choose positional output or --output, not both");
      if (output.empty())
        throw std::runtime_error("file requires an output path or -o");
      bc::process_file(input, output, c);
    } else if (*preview || *webcam || *virtualcam) {
      c.frames = limit;
      bc::live(camera, *virtualcam ? virtual_output : live_output, test_input, c,
               *preview || (*webcam && !no_preview) || (*virtualcam && no_preview));
    } else if (*dev)
      bc::devices(!short_devices);
    else if (*eff) {
      bool found = effect.empty();
      for (auto &e : bc::effect_catalog())
        if (effect.empty() || effect == e.name) {
          std::cout << e.name << " — " << e.description << "\n  CPU: yes; GPU: no\n  "
                    << e.parameters << '\n';
          found = true;
        }
      if (!found)
        throw std::runtime_error("Unknown effect: " + effect);
    } else if (*bench)
      bc::benchmark(c);
    else if (*inf)
      bc::info();
    else if (*models) {
      if (*install)
        bc::install_model();
      else
        bc::list_models();
    } else if (*completions) {
      const std::string words =
          "file webcam virtualcam preview devices effects benchmark info models completions --help "
          "--effect --preset --backend --privacy-failsafe --padding --confidence --device --output "
          "--model --debug --verbose";
      if (shell == "bash")
        std::cout << "_blurcam() { COMPREPLY=( $(compgen -W '" << words
                  << "' -- \"${COMP_WORDS[COMP_CWORD]}\") ); }; complete -F _blurcam blurcam\n";
      else if (shell == "zsh")
        std::cout << "#compdef blurcam\n_arguments '*:argument:(" << words << ")'\n";
      else
        std::cout << "complete -c blurcam -f -a '" << words << "'\n";
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "blurcam: " << e.what() << '\n';
    return bc::interrupted ? 130 : 1;
  }
}
