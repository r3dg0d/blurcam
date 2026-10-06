#pragma once
#include "vision.hpp"
#include <atomic>
#include <csignal>
#include <memory>
extern "C" {
#include <libavformat/avformat.h>
}
namespace blurcam {
extern volatile sig_atomic_t interrupted;
struct VideoFrame {
  cv::Mat image;
  int64_t pts = 0, duration = 0;
};
class Reader {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  Reader(const std::string &, const Config &, bool camera = false);
  ~Reader();
  bool next(VideoFrame &, const std::function<void(const AVPacket *)> & = {});
  AVFormatContext *format() const;
  int video_index() const;
  AVRational fps() const;
  AVRational timebase() const;
  cv::Size size() const;
};
class Writer {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  Writer(const std::string &, Reader &, const Config &);
  ~Writer();
  void video(const VideoFrame &);
  void audio(const AVPacket *);
  void finish();
  const std::string &codec() const;
};
void process_file(const std::string &, const std::string &, const Config &);
void live(const std::string &device, const std::string &output, const std::string &test_input,
          const Config &, bool preview);
void devices(bool modes);
void benchmark(const Config &);
void info();
void exclusive_write(const std::filesystem::path &, const std::string &);
} // namespace blurcam
