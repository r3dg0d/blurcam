#include "blurcam/media.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <unistd.h>
namespace blurcam {
volatile sig_atomic_t interrupted = 0;
static std::string error(int n) {
  char s[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(n, s, sizeof(s));
  return s;
}
static void check(int n, const char *action) {
  if (n < 0)
    throw std::runtime_error(std::string(action) + ": " + error(n));
}
struct Reader::Impl {
  AVFormatContext *input = nullptr;
  AVCodecContext *decoder = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();
  SwsContext *sws = nullptr;
  int video = -1;
  bool draining = false;
  int64_t fallback = 0;
  std::atomic<int64_t> deadline{0};
  ~Impl() {
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&input);
  }
  static int interrupt(void *opaque) {
    auto *self = static_cast<Impl *>(opaque);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    return interrupted || (self->deadline.load() > 0 && now > self->deadline.load());
  }
  void timeout() {
    deadline = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count() +
               3000;
  }
};
Reader::Reader(const std::string &path, const Config &c, bool camera)
    : impl_(std::make_unique<Impl>()) {
  auto &s = *impl_;
  s.input = avformat_alloc_context();
  if (!s.input || !s.frame || !s.packet)
    throw std::bad_alloc();
  s.input->interrupt_callback = {Impl::interrupt, &s};
  AVDictionary *options = nullptr;
  av_dict_set(&options, "protocol_whitelist", "file,pipe", 0);
  const AVInputFormat *format = nullptr;
  if (camera) {
    avdevice_register_all();
    format = av_find_input_format("v4l2");
    std::string resolution = std::to_string(c.width) + "x" + std::to_string(c.height);
    av_dict_set(&options, "video_size", resolution.c_str(), 0);
    av_dict_set(&options, "framerate", std::to_string(c.fps).c_str(), 0);
    s.timeout();
  }
  int r = avformat_open_input(&s.input, path == "-" ? "pipe:0" : path.c_str(), format, &options);
  av_dict_free(&options);
  check(r, "Open input (check path/device permission)");
  check(avformat_find_stream_info(s.input, nullptr), "Read stream metadata");
  s.video = av_find_best_stream(s.input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  check(s.video, "Find video stream");
  auto *params = s.input->streams[s.video]->codecpar;
  if (params->width <= 0 || params->height <= 0 ||
      int64_t(params->width) * params->height > 7680LL * 4320)
    throw std::runtime_error("Input resolution exceeds 7680x4320 pixel budget");
  // Preserve only inputs whose orientation is explicit in the pixels.
  if (av_dict_get(s.input->streams[s.video]->metadata, "rotate", nullptr, 0))
    throw std::runtime_error(
        "Rotation metadata not supported; normalize orientation with FFmpeg first");
#if LIBAVCODEC_VERSION_MAJOR >= 61
  for (int i = 0; i < params->nb_coded_side_data; i++)
    if (params->coded_side_data[i].type == AV_PKT_DATA_DISPLAYMATRIX)
      throw std::runtime_error(
          "Display rotation metadata not supported; normalize with FFmpeg first");
#else
  size_t matrix_size = 0;
  if (av_stream_get_side_data(s.input->streams[s.video], AV_PKT_DATA_DISPLAYMATRIX, &matrix_size))
    throw std::runtime_error(
        "Display rotation metadata not supported; normalize with FFmpeg first");
#endif
  const auto *dec = avcodec_find_decoder(params->codec_id);
  if (!dec)
    throw std::runtime_error("Video decoder unavailable");
  s.decoder = avcodec_alloc_context3(dec);
  if (!s.decoder)
    throw std::bad_alloc();
  check(avcodec_parameters_to_context(s.decoder, params), "Initialize decoder");
  s.decoder->thread_count = c.threads;
  s.decoder->max_pixels = 7680LL * 4320;
  check(avcodec_open2(s.decoder, dec, nullptr), "Open decoder");
  s.deadline = 0;
}
Reader::~Reader() = default;
AVFormatContext *Reader::format() const { return impl_->input; }
int Reader::video_index() const { return impl_->video; }
AVRational Reader::fps() const {
  auto r = av_guess_frame_rate(impl_->input, impl_->input->streams[impl_->video], nullptr);
  return r.num && r.den ? r : AVRational{30, 1};
}
AVRational Reader::timebase() const { return impl_->input->streams[impl_->video]->time_base; }
cv::Size Reader::size() const { return {impl_->decoder->width, impl_->decoder->height}; }
bool Reader::next(VideoFrame &out, const std::function<void(const AVPacket *)> &audio) {
  auto &s = *impl_;
  while (!interrupted) {
    int r = avcodec_receive_frame(s.decoder, s.frame);
    if (r == 0) {
      auto *f = s.frame;
      if (f->width <= 0 || f->height <= 0 || int64_t(f->width) * f->height > 7680LL * 4320)
        throw std::runtime_error("Decoded frame exceeds pixel budget");
      s.sws = sws_getCachedContext(s.sws, f->width, f->height, AVPixelFormat(f->format), f->width,
                                   f->height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr,
                                   nullptr);
      if (!s.sws)
        throw std::runtime_error("Cannot allocate color conversion");
      out.image.create(f->height, f->width, CV_8UC3);
      uint8_t *data[4] = {out.image.data, nullptr, nullptr, nullptr};
      int stride[4] = {int(out.image.step), 0, 0, 0};
      check(sws_scale(s.sws, f->data, f->linesize, 0, f->height, data, stride), "Convert frame");
      out.pts = f->best_effort_timestamp == AV_NOPTS_VALUE ? s.fallback : f->best_effort_timestamp;
      out.duration = f->duration > 0 ? f->duration : av_rescale_q(1, av_inv_q(fps()), timebase());
      s.fallback = out.pts + out.duration;
      av_frame_unref(f);
      return true;
    }
    if (r == AVERROR_EOF)
      return false;
    if (r != AVERROR(EAGAIN))
      check(r, "Decode video");
    if (s.draining)
      throw std::runtime_error("Decoder failed to drain");
    while (true) {
      av_packet_unref(s.packet);
      s.timeout();
      r = av_read_frame(s.input, s.packet);
      s.deadline = 0;
      if (r == AVERROR_EOF) {
        check(avcodec_send_packet(s.decoder, nullptr), "Drain decoder");
        s.draining = true;
        break;
      }
      check(r, "Read input (camera disconnected or stalled)");
      if (s.packet->stream_index == s.video) {
        check(avcodec_send_packet(s.decoder, s.packet), "Submit compressed frame");
        break;
      }
      if (audio)
        audio(s.packet);
    }
  }
  return false;
}
struct Writer::Impl {
  AVFormatContext *output = nullptr;
  AVCodecContext *encoder = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();
  SwsContext *sws = nullptr;
  AVIOContext *io = nullptr;
  Reader *reader = nullptr;
  std::map<int, int> mapping;
  std::map<int64_t, int64_t> durations;
  std::string name;
  std::filesystem::path destination, temp;
  int fd = -1;
  bool finished = false, overwrite = false, stdout_output = false;
  int64_t last_pts = AV_NOPTS_VALUE;
  ~Impl() {
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&encoder);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
    if (output) {
      output->pb = nullptr;
      avformat_free_context(output);
    }
    if (fd >= 0)
      close(fd);
    if (!temp.empty())
      unlink(temp.c_str());
  }
#if LIBAVFORMAT_VERSION_MAJOR >= 61
  static int write_packet(void *o, const uint8_t *data, int n){
#else
  static int write_packet(void *o, uint8_t *data, int n) {
#endif
      auto &self = *static_cast<Impl *>(o);
  int done = 0;
  while (done < n) {
    auto r = write(self.fd, data + done, size_t(n - done));
    if (r < 0 && errno == EINTR && !interrupted)
      continue;
    if (r <= 0)
      return AVERROR(errno ? errno : EIO);
    done += int(r);
  }
  return done;
} static int64_t seek(void *o, int64_t offset, int whence) {
  auto &self = *static_cast<Impl *>(o);
  if (whence == AVSEEK_SIZE) {
    auto cur = lseek(self.fd, 0, SEEK_CUR);
    auto end = lseek(self.fd, 0, SEEK_END);
    lseek(self.fd, cur, SEEK_SET);
    return end;
  }
  auto r = lseek(self.fd, offset, whence & ~AVSEEK_FORCE);
  return r < 0 ? AVERROR(errno) : r;
}
void drain() {
  while (true) {
    int r = avcodec_receive_packet(encoder, packet);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
      break;
    check(r, "Encode video");
    if (auto it = durations.find(packet->pts); it != durations.end()) {
      packet->duration = it->second;
      durations.erase(it);
    }
    av_packet_rescale_ts(packet, encoder->time_base, output->streams[0]->time_base);
    packet->stream_index = 0;
    check(av_interleaved_write_frame(output, packet), "Mux encoded video");
    av_packet_unref(packet);
  }
}
}; // namespace blurcam
Writer::Writer(const std::string &path, Reader &reader, const Config &c)
    : impl_(std::make_unique<Impl>()) {
  auto &s = *impl_;
  s.reader = &reader;
  s.overwrite = c.overwrite;
  s.stdout_output = path == "-";
  s.destination = path;
  if (!s.stdout_output && !c.overwrite && std::filesystem::exists(path))
    throw std::runtime_error("Output exists; use --overwrite explicitly");
  check(avformat_alloc_output_context2(&s.output, nullptr, s.stdout_output ? "matroska" : nullptr,
                                       s.stdout_output ? nullptr : path.c_str()),
        "Choose output container");
  if (!s.output)
    throw std::runtime_error("Unknown output extension (use .mp4, .mkv, .webm)");
  std::string name = c.codec;
  if (name == "auto")
    name = std::string(s.output->oformat->name) == "webm" ? "vp9" : "h264";
  std::map<std::string, std::string> codecs{
      {"h264", "libx264"}, {"h265", "libx265"}, {"vp9", "libvpx-vp9"}, {"av1", "libaom-av1"}};
  s.name = codecs.contains(name) ? codecs[name] : name;
  const auto *enc = avcodec_find_encoder_by_name(s.name.c_str());
  if (!enc)
    throw std::runtime_error("Encoder not built into FFmpeg: " + s.name);
  if (!avformat_query_codec(s.output->oformat, enc->id, FF_COMPLIANCE_NORMAL))
    throw std::runtime_error("Codec is incompatible with output container");
  s.encoder = avcodec_alloc_context3(enc);
  if (!s.encoder || !s.frame || !s.packet)
    throw std::bad_alloc();
  auto size = reader.size();
  s.encoder->width = size.width;
  s.encoder->height = size.height;
  s.encoder->pix_fmt = AV_PIX_FMT_YUV420P;
  if ((size.width % 2 || size.height % 2) && name != "ffv1")
    throw std::runtime_error(
        "4:2:0 encoding requires even dimensions; use --codec ffv1 with MKV for odd sizes");
  if (name == "ffv1")
    s.encoder->pix_fmt = AV_PIX_FMT_YUV444P;
  s.encoder->time_base = reader.timebase();
  s.encoder->framerate = reader.fps();
  s.encoder->sample_aspect_ratio =
      reader.format()->streams[reader.video_index()]->codecpar->sample_aspect_ratio;
  s.encoder->thread_count = c.threads;
  s.encoder->gop_size = 60;
  s.encoder->max_b_frames = 0;
  if (s.output->oformat->flags & AVFMT_GLOBALHEADER)
    s.encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  AVDictionary *options = nullptr;
  av_dict_set(&options, "crf", "18", 0);
  if (name == "h264" || name == "h265")
    av_dict_set(&options, "preset", "fast", 0);
  if (name == "vp9") {
    av_dict_set(&options, "deadline", "good", 0);
    av_dict_set(&options, "cpu-used", "4", 0);
    s.encoder->bit_rate = 0;
  }
  if (name.find("nvenc") != std::string::npos) {
    av_dict_set(&options, "preset", "p4", 0);
    av_dict_set(&options, "cq", "19", 0);
  }
  int r = avcodec_open2(s.encoder, enc, &options);
  av_dict_free(&options);
  check(r, "Open encoder (NVENC requires a working NVIDIA driver)");
  auto *video = avformat_new_stream(s.output, nullptr);
  if (!video)
    throw std::bad_alloc();
  check(avcodec_parameters_from_context(video->codecpar, s.encoder), "Create video stream");
  video->time_base = s.encoder->time_base;
  video->avg_frame_rate = reader.fps();
  for (unsigned i = 0; i < reader.format()->nb_streams; i++) {
    auto *in = reader.format()->streams[i];
    if (in->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
      continue;
    if (!avformat_query_codec(s.output->oformat, in->codecpar->codec_id, FF_COMPLIANCE_NORMAL))
      throw std::runtime_error("Audio codec cannot be copied into selected container. Choose MKV "
                               "or transcode audio with FFmpeg first");
    auto *audio = avformat_new_stream(s.output, nullptr);
    if (!audio)
      throw std::bad_alloc();
    check(avcodec_parameters_copy(audio->codecpar, in->codecpar), "Copy audio parameters");
    audio->codecpar->codec_tag = 0;
    audio->time_base = in->time_base;
    av_dict_copy(&audio->metadata, in->metadata, 0);
    s.mapping[int(i)] = audio->index;
  }
  av_dict_copy(&s.output->metadata, reader.format()->metadata, 0);
  if (s.stdout_output) {
    s.fd = dup(STDOUT_FILENO);
  } else {
    auto parent = s.destination.parent_path();
    if (parent.empty())
      parent = ".";
    std::string pattern = (parent / ".blurcam-XXXXXX").string();
    std::vector<char> p(pattern.begin(), pattern.end());
    p.push_back(0);
    s.fd = mkstemp(p.data());
    s.temp = p.data();
  }
  if (s.fd < 0)
    throw std::runtime_error("Cannot create output: " + std::string(std::strerror(errno)));
  auto *buffer = static_cast<unsigned char *>(av_malloc(65536));
  if (!buffer)
    throw std::bad_alloc();
  s.io = avio_alloc_context(buffer, 65536, 1, &s, nullptr, Impl::write_packet,
                            s.stdout_output ? nullptr : Impl::seek);
  if (!s.io) {
    av_free(buffer);
    throw std::bad_alloc();
  }
  s.output->pb = s.io;
  s.output->flags |= AVFMT_FLAG_CUSTOM_IO;
  check(avformat_write_header(s.output, nullptr), "Write container header");
  s.frame->format = s.encoder->pix_fmt;
  s.frame->width = size.width;
  s.frame->height = size.height;
  check(av_frame_get_buffer(s.frame, 32), "Allocate encoder frame");
  s.sws = sws_getContext(size.width, size.height, AV_PIX_FMT_BGR24, size.width, size.height,
                         s.encoder->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!s.sws)
    throw std::bad_alloc();
}
Writer::~Writer() = default;
const std::string &Writer::codec() const { return impl_->name; }
void Writer::video(const VideoFrame &f) {
  auto &s = *impl_;
  if (f.image.size() != cv::Size(s.encoder->width, s.encoder->height))
    throw std::runtime_error("Midstream resolution change; output cancelled safely");
  if (s.last_pts != AV_NOPTS_VALUE && f.pts <= s.last_pts)
    throw std::runtime_error("Non-monotonic video timestamps; refusing to desynchronize output");
  s.last_pts = f.pts;
  check(av_frame_make_writable(s.frame), "Reuse encoder buffer");
  const uint8_t *data[4] = {f.image.data, nullptr, nullptr, nullptr};
  int stride[4] = {int(f.image.step), 0, 0, 0};
  check(sws_scale(s.sws, data, stride, 0, f.image.rows, s.frame->data, s.frame->linesize),
        "Convert encoder colors");
  s.frame->pts = f.pts;
  s.frame->duration = f.duration;
  s.durations[f.pts] = f.duration;
  check(avcodec_send_frame(s.encoder, s.frame), "Submit encoder frame");
  s.drain();
}
void Writer::audio(const AVPacket *packet) {
  auto &s = *impl_;
  auto it = s.mapping.find(packet->stream_index);
  if (it == s.mapping.end())
    return;
  auto *p = av_packet_clone(packet);
  if (!p)
    throw std::bad_alloc();
  auto in = s.reader->format()->streams[packet->stream_index]->time_base;
  auto out = s.output->streams[it->second]->time_base;
  av_packet_rescale_ts(p, in, out);
  p->stream_index = it->second;
  p->pos = -1;
  int r = av_interleaved_write_frame(s.output, p);
  av_packet_free(&p);
  check(r, "Copy audio packet");
}
void Writer::finish() {
  auto &s = *impl_;
  check(avcodec_send_frame(s.encoder, nullptr), "Flush encoder");
  s.drain();
  check(av_write_trailer(s.output), "Finalize output");
  avio_flush(s.io);
  check(s.io->error, "Flush output");
  if (!s.stdout_output) {
    if (fsync(s.fd) != 0)
      throw std::runtime_error("Cannot sync output");
    if (s.overwrite) {
      if (rename(s.temp.c_str(), s.destination.c_str()) != 0)
        throw std::runtime_error("Cannot commit output");
    } else {
      if (link(s.temp.c_str(), s.destination.c_str()) != 0)
        throw std::runtime_error("Output appeared during processing; refusing overwrite");
      unlink(s.temp.c_str());
    }
    s.temp.clear();
  }
  s.finished = true;
}
void process_file(const std::string &input, const std::string &output, const Config &c) {
  if (input != "-" && output != "-" && std::filesystem::exists(output) &&
      std::filesystem::equivalent(input, output))
    throw std::runtime_error("Input and output must be different files");
  Pipeline pipeline(c);
  Reader reader(input, c);
  Writer writer(output, reader, c);
  if (c.verbose)
    std::cerr << "Detector: YuNet / " << pipeline.backend()
              << "; decode: CPU; effects: CPU; encoder: " << writer.codec() << '\n';
  VideoFrame f;
  uint64_t frames = 0;
  auto start = std::chrono::steady_clock::now();
  while (reader.next(f, [&](auto *p) { writer.audio(p); })) {
    pipeline.process(f.image);
    writer.video(f);
    frames++;
  }
  if (interrupted)
    throw std::runtime_error("Interrupted; partial file removed");
  if (frames == 0)
    throw std::runtime_error("No decodable frames; output cancelled");
  writer.finish();
  if (!c.quiet) {
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cerr << "Processed " << frames << " frames, " << frames / seconds << " fps\n";
  }
}
} // namespace blurcam
