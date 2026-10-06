#include "blurcam/media.hpp"
#include <SDL.h>
#include <opencv2/core/version.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
}
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <linux/videodev2.h>
#include <optional>
#include <poll.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
namespace blurcam {
namespace {
struct Fd {
  int n = -1;
  explicit Fd(int v) : n(v) {}
  ~Fd() {
    if (n >= 0)
      close(n);
  }
};
struct VirtualSink {
  Fd fd;
  bool raw;
  int width, height, stride;
  SwsContext *convert = nullptr;
  std::vector<uint8_t> buffer;
  VirtualSink(const std::string &p, cv::Size size, AVRational fps)
      : fd(p == "-" ? dup(STDOUT_FILENO) : open(p.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC)),
        raw(p == "-"), width(size.width), height(size.height), stride(width * 2) {
    if (fd.n < 0)
      throw std::runtime_error("Cannot open virtual output. Configure v4l2loopback: see "
                               "docs/NIXOS.md; check video-group permissions");
    if (raw)
      return;
    if (width % 2)
      throw std::runtime_error("YUYV virtual camera requires an even width");
    v4l2_capability cap{};
    if (ioctl(fd.n, VIDIOC_QUERYCAP, &cap) < 0 ||
        std::string(reinterpret_cast<char *>(cap.driver)).find("loopback") == std::string::npos)
      throw std::runtime_error("Output must be a v4l2loopback device, never a physical camera");
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.bytesperline = width * 2;
    format.fmt.pix.sizeimage = width * height * 2;
    if (ioctl(fd.n, VIDIOC_S_FMT, &format) < 0 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
        format.fmt.pix.width != unsigned(width) || format.fmt.pix.height != unsigned(height))
      throw std::runtime_error(
          "Virtual camera rejected YUYV/resolution. Stop other producers and restart blurcam");
    v4l2_streamparm timing{};
    timing.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    timing.parm.output.timeperframe.numerator = fps.den;
    timing.parm.output.timeperframe.denominator = fps.num;
    if (ioctl(fd.n, VIDIOC_S_PARM, &timing) < 0)
      throw std::runtime_error("Virtual camera rejected frame-rate negotiation");
    stride = int(format.fmt.pix.bytesperline);
    if (stride < width * 2 || stride > width * 2 + 65536 ||
        format.fmt.pix.sizeimage > 128 * 1024 * 1024)
      throw std::runtime_error("Invalid virtual-camera buffer geometry");
    buffer.resize(std::max(size_t(stride) * height, size_t(format.fmt.pix.sizeimage)));
    convert = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUYV422,
                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!convert)
      throw std::runtime_error("Cannot create YUYV converter");
  }
  ~VirtualSink() { sws_freeContext(convert); }
  void send(const cv::Mat &f) {
    if (f.cols != width || f.rows != height)
      throw std::runtime_error("Camera resolution changed; restart blurcam");
    const uint8_t *bytes = f.data;
    size_t length = f.total() * f.elemSize();
    if (!raw) {
      const uint8_t *in[4] = {f.data, nullptr, nullptr, nullptr};
      int step[4] = {int(f.step), 0, 0, 0};
      uint8_t *out[4] = {buffer.data(), nullptr, nullptr, nullptr};
      int outstride[4] = {stride, 0, 0, 0};
      sws_scale(convert, in, step, 0, height, out, outstride);
      bytes = buffer.data();
      length = buffer.size();
    }
    size_t pos = 0;
    while (pos < length) {
      pollfd poller{fd.n, POLLOUT, 0};
      int ready = poll(&poller, 1, 200);
      if (ready < 0 && errno == EINTR)
        continue;
      if (ready <= 0)
        throw std::runtime_error("Virtual output stalled; stopping to bound latency");
      auto n = write(fd.n, bytes + pos, length - pos);
      if (n < 0 && (errno == EAGAIN || errno == EINTR))
        continue;
      if (n <= 0)
        throw std::runtime_error("Virtual output disconnected");
      pos += size_t(n);
    }
  }
};
struct Preview {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  cv::Size size;
  ~Preview() {
    if (texture)
      SDL_DestroyTexture(texture);
    if (renderer)
      SDL_DestroyRenderer(renderer);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }
  void open(cv::Size s) {
    size = s;
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
      throw std::runtime_error(std::string("SDL preview: ") + SDL_GetError());
    window =
        SDL_CreateWindow("BlurCam", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         std::min(1280, s.width), std::min(720, s.height), SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
      throw std::runtime_error(SDL_GetError());
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING,
                                s.width, s.height);
    if (!texture)
      throw std::runtime_error(SDL_GetError());
  }
  void show(const cv::Mat &f) {
    if (f.size() != size)
      throw std::runtime_error("Preview resolution changed; restart blurcam");
    if (SDL_UpdateTexture(texture, nullptr, f.data, int(f.step)) < 0)
      throw std::runtime_error(SDL_GetError());
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
  }
};
} // namespace
void devices(bool modes) {
  bool found = false;
  for (int i = 0; i < 256; i++) {
    std::string path = "/dev/video" + std::to_string(i);
    Fd fd(open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    if (fd.n < 0)
      continue;
    v4l2_capability cap{};
    if (ioctl(fd.n, VIDIOC_QUERYCAP, &cap) < 0)
      continue;
    found = true;
    std::cout << path << "  " << cap.card << "  [" << cap.driver << "]\n";
    if (!modes)
      continue;
    v4l2_fmtdesc fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd.n, VIDIOC_ENUM_FMT, &fmt) == 0) {
      std::cout << "  " << fmt.description << '\n';
      v4l2_frmsizeenum sz{};
      sz.pixel_format = fmt.pixelformat;
      while (ioctl(fd.n, VIDIOC_ENUM_FRAMESIZES, &sz) == 0) {
        if (sz.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
          std::cout << "    " << sz.discrete.width << 'x' << sz.discrete.height;
          v4l2_frmivalenum fps{};
          fps.pixel_format = fmt.pixelformat;
          fps.width = sz.discrete.width;
          fps.height = sz.discrete.height;
          while (ioctl(fd.n, VIDIOC_ENUM_FRAMEINTERVALS, &fps) == 0) {
            if (fps.type == V4L2_FRMIVAL_TYPE_DISCRETE && fps.discrete.numerator)
              std::cout << " " << double(fps.discrete.denominator) / fps.discrete.numerator
                        << "fps";
            fps.index++;
          }
          std::cout << '\n';
        } else {
          std::cout << "    variable " << sz.stepwise.min_width << 'x' << sz.stepwise.min_height
                    << ".." << sz.stepwise.max_width << 'x' << sz.stepwise.max_height << '\n';
        }
        sz.index++;
      }
      fmt.index++;
    }
  }
  if (!found)
    std::cout << "No accessible V4L2 devices. Check /dev/video*, permissions and sandbox access.\n";
}
static std::string resolve_device(const std::string &name) {
  if (name.starts_with("/"))
    return name;
  std::vector<std::string> matches;
  for (int i = 0; i < 256; i++) {
    std::string p = "/dev/video" + std::to_string(i);
    Fd fd(open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    v4l2_capability cap{};
    if (fd.n >= 0 && ioctl(fd.n, VIDIOC_QUERYCAP, &cap) == 0 &&
        std::string(reinterpret_cast<char *>(cap.card)) == name &&
        ((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) &
         V4L2_CAP_VIDEO_CAPTURE))
      matches.push_back(p);
  }
  if (matches.size() != 1)
    throw std::runtime_error(
        "Camera name not found or ambiguous; use blurcam devices and an explicit /dev/videoN");
  return matches[0];
}
void live(const std::string &device, const std::string &output, const std::string &test_input,
          const Config &c, bool preview) {
  std::string source = test_input.empty() ? resolve_device(device) : test_input;
  if (!output.empty() && output != "-" && std::filesystem::exists(source) &&
      std::filesystem::exists(output) && std::filesystem::equivalent(source, output))
    throw std::runtime_error("Camera input and virtual output must differ");
  Pipeline pipeline(c);
  Reader reader(source, c, test_input.empty());
  cv::Size size = reader.size();
  Preview view;
  if (preview)
    view.open(size);
  std::unique_ptr<VirtualSink> sink;
  if (!output.empty())
    sink = std::make_unique<VirtualSink>(output, size, reader.fps());
  if (!preview && !sink)
    throw std::runtime_error("Live mode needs a preview or output");
  if (c.verbose)
    std::cerr << "Detector: YuNet / " << pipeline.backend() << "; capture: FFmpeg "
              << (test_input.empty() ? "V4L2" : "paced file") << "; effects: CPU; output: "
              << (sink ? (output == "-" ? "BGR24 stdout" : "V4L2 YUYV") : "SDL") << '\n';
  struct Slot {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<VideoFrame> latest;
    bool done = false;
    std::exception_ptr error;
    uint64_t drops = 0, captured = 0;
    std::chrono::steady_clock::time_point captured_at;
  } slot;
  std::jthread capture([&] {
    try {
      VideoFrame frame;
      auto next = std::chrono::steady_clock::now();
      while (!interrupted && reader.next(frame)) {
        if (!test_input.empty()) {
          next += std::chrono::microseconds(int64_t(1000000. / av_q2d(reader.fps())));
          std::this_thread::sleep_until(next);
        }
        std::lock_guard lock(slot.mutex);
        if (slot.latest)
          slot.drops++;
        slot.latest = VideoFrame{frame.image.clone(), frame.pts, frame.duration};
        slot.captured++;
        slot.captured_at = std::chrono::steady_clock::now();
        slot.cv.notify_one();
      }
    } catch (...) {
      std::lock_guard lock(slot.mutex);
      slot.error = std::current_exception();
    }
    std::lock_guard lock(slot.mutex);
    slot.done = true;
    slot.cv.notify_one();
  });
  struct Shutdown {
    ~Shutdown() { interrupted = 1; }
  } shutdown;
  bool quit = false, show_fps = false;
  uint64_t count = 0;
  auto begin = std::chrono::steady_clock::now();
  double latency = 0;
  cv::Mat last(size, CV_8UC3, cv::Scalar(0));
  try {
    while (!interrupted && !quit) {
      std::optional<VideoFrame> f;
      bool done;
      std::exception_ptr error;
      std::chrono::steady_clock::time_point at;
      {
        std::unique_lock lock(slot.mutex);
        slot.cv.wait_for(lock, std::chrono::milliseconds(100),
                         [&] { return slot.latest.has_value() || slot.done || interrupted; });
        done = slot.done;
        error = slot.error;
        at = slot.captured_at;
        if (slot.latest) {
          f = std::move(slot.latest);
          slot.latest.reset();
        }
      }
      if (error)
        std::rethrow_exception(error);
      if (!f) {
        if (done)
          break;
        continue;
      }
      auto metrics = pipeline.process(f->image);
      last = f->image;
      if (sink)
        sink->send(last);
      if (preview) {
        view.show(last);
        SDL_SetWindowTitle(
            view.window,
            ("BlurCam | " +
             (metrics.unsafe ? std::string("TRACKING UNCERTAIN")
                             : std::to_string(metrics.faces) + " faces") +
             (show_fps
                  ? " | " +
                        std::to_string(count /
                                       std::max(.001, std::chrono::duration<double>(
                                                          std::chrono::steady_clock::now() - begin)
                                                          .count()))
                            .substr(0, 5) +
                        "fps"
                  : ""))
                .c_str());
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          if (e.type == SDL_QUIT)
            quit = true;
          if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
            case SDLK_q:
            case SDLK_ESCAPE:
              quit = true;
              break;
            case SDLK_1:
              pipeline.change_effect("blur");
              break;
            case SDLK_2:
              pipeline.change_effect("pixelate");
              break;
            case SDLK_3:
              pipeline.change_effect("glitch");
              break;
            case SDLK_4:
              pipeline.change_effect("censor");
              break;
            case SDLK_5:
              pipeline.change_effect("vhs");
              break;
            case SDLK_LEFTBRACKET:
              pipeline.adjust_strength(-1);
              break;
            case SDLK_RIGHTBRACKET:
              pipeline.adjust_strength(1);
              break;
            case SDLK_f:
              show_fps = !show_fps;
              break;
            case SDLK_d:
            case SDLK_t:
              pipeline.toggle_debug();
              break;
            default:
              break;
            }
          }
        }
      }
      latency =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count();
      count++;
      if (c.frames > 0 && count >= uint64_t(c.frames))
        break;
    }
  } catch (...) {
    last.setTo(cv::Scalar::all(0));
    try {
      if (sink)
        sink->send(last);
      if (preview)
        view.show(last);
    } catch (...) {
    }
    throw;
  }
  // Replace the last virtual frame with black when the producer stops.
  last.setTo(cv::Scalar::all(0));
  if (sink)
    sink->send(last);
  interrupted = 1;
  capture.join();
  if (!c.quiet)
    std::cerr << "Live: " << count << " frames; captured=" << slot.captured
              << " dropped=" << slot.drops << "; last capture-to-output=" << latency << "ms\n";
}
void benchmark(const Config &c) {
  cv::Mat sample(c.height, c.width, CV_8UC3);
  cv::RNG rng(c.seed);
  rng.fill(sample, cv::RNG::UNIFORM, 0, 255);
  auto detector = YuNet(c);
  Effects effects(c);
  Tracker tracker(c);
  cv::Rect roi(c.width / 3, c.height / 4, c.width / 4, c.height / 3);
  Face face;
  face.box = cv::Rect2f(roi);
  face.cover = face.box;
  std::vector<double> total;
  double detect = 0, track = 0, render = 0, mask = 0;
  int detections = 0;
  auto ms = [](auto start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
  };
  for (int i = 0; i < c.frames + 10; i++) {
    auto start = std::chrono::steady_clock::now();
    double d = 0;
    if (i % c.detection_interval == 0) {
      auto t = std::chrono::steady_clock::now();
      detector.detect(sample);
      d = ms(t);
    }
    auto t = std::chrono::steady_clock::now();
    tracker.predict(sample);
    if (i % c.detection_interval == 0)
      tracker.refresh({face}, sample);
    double tr = ms(t);
    t = std::chrono::steady_clock::now();
    auto r = padded_rect(face.box, sample.size(), c.padding);
    double ma = ms(t);
    auto image = sample.clone();
    t = std::chrono::steady_clock::now();
    effects.render(image, {r}, i);
    double e = ms(t);
    double elapsed = ms(start);
    if (i >= 10) {
      total.push_back(elapsed);
      detect += d;
      track += tr;
      render += e;
      mask += ma;
      if (d > 0)
        detections++;
    }
  }
  double sum = 0;
  for (auto v : total)
    sum += v;
  std::sort(total.begin(), total.end());
  std::cout << std::fixed << std::setprecision(3) << "{\n  \"resolution\": \"" << c.width << 'x'
            << c.height << "\",\n  \"frames\": " << c.frames << ",\n  \"backend\": \""
            << detector.backend()
            << "\",\n  \"fixture\": \"synthetic texture; one ground-truth tracker/effect ROI; "
               "detector finds no real faces\",\n  \"detector_ms_per_call\": "
            << detect / std::max(1, detections)
            << ",\n  \"detector_amortized_ms\": " << detect / c.frames
            << ",\n  \"tracker_ms\": " << track / c.frames
            << ",\n  \"mask_ms\": " << mask / c.frames
            << ",\n  \"effects_ms\": " << render / c.frames
            << ",\n  \"total_ms\": " << sum / c.frames
            << ",\n  \"p95_ms\": " << total[std::min(total.size() - 1, size_t(total.size() * .95))]
            << ",\n  \"processing_fps\": " << 1000 * c.frames / sum
            << ",\n  \"target_fps\": " << c.fps << ",\n  \"realtime_budget_met\": "
            << (sum / c.frames <= 1000. / c.fps ? "true" : "false")
            << ",\n  \"capture_fps\": null, \"dropped_frames\": null, \"encoder\": null, "
               "\"end_to_end_latency_ms\": null\n}\n";
}
void info() {
  std::cout << "BlurCam 0.1.0\nOpenCV " << CV_VERSION << "\nFFmpeg " << av_version_info()
            << "\nDetector: checksum-pinned YuNet\nInference: CPU; CUDA if OpenCV DNN provides "
               "it\nEffects/tracker/decode: CPU\nEncode: software; explicit NVENC if FFmpeg and "
               "driver support it\nPreview: SDL (Wayland/X11)\nVirtual camera: V4L2/v4l2loopback "
               "YUYV\nTelemetry: none\n";
  auto targets = cv::dnn::getAvailableTargets(cv::dnn::DNN_BACKEND_CUDA);
  std::cout << "CUDA DNN targets: " << targets.size() << '\n';
}
} // namespace blurcam
