# Validation — 2026-10-05

The local system used NixOS x86_64, Intel Core i9-14900K, NVIDIA RTX 4090,
OpenCV 4.13, FFmpeg 9.0.1, SDL2 compatibility over SDL3 and GCC 16.
These are measured results from this host, not cross-machine guarantees.

## Tests

* 68 C++ checks passed, including real YuNet one-face/two-face detection on a
  public-domain NASA fixture, optical-flow motion, stable association, conservative
  smoothing coverage, masks, independent pixelation visual golden, deterministic effects,
  ROI isolation, overlay and TOML roundtrip.
* 50 integration commands passed with the pinned model: help/CLI validation,
  configuration precedence, all compatible audio packet hash identity, dimensions,
  10/10 output video frames and CFR rate, opaque fail-safe pixels, VFR PTS equality,
  overwrite protection, stdin/stdout, SDL dummy preview, raw live output and errors.
* Privacy checks cover missing faces, detector exceptions, tracking/flow failure,
  uniform occlusion, low confidence, startup recovery, max-face overflow, resolution
  changes and retained detector coverage while tracking smoothed boxes.
* AddressSanitizer + UndefinedBehaviorSanitizer and LeakSanitizer passed the model-enabled
  suites outside the sandbox. The sandbox restricts LeakSanitizer thread inspection;
  SDL2-over-SDL3 needs the SDL3 search path under ASan's intercepted dlopen. The Nix
  development shell supplies that path. No code sanitizer errors were suppressed.
* clang-format verification and cppcheck warning/performance/portability checks passed.
* Pure Nix package build/install and CTest passed. Package installs the executable,
  man page and three shell completions. Model is a checksum-pinned test-only input.
* Real Wayland webcam preview passed: 15 processed frames, 23 captured, 8 dropped by
  the bounded slot. Last measured capture-to-output time was 16.0 ms. Opaque max-privacy
  preset was used; no webcam image or recording was saved. Capture included camera
  startup and this short run is not a steady-state camera benchmark.
* Real v4l2loopback producer/consumer passed on public two-face sample replay:
  30 processed frames, 31 captured, 0 slot drops; last measured capture-to-output
  1.65 ms. Separate FFmpeg consumer captured five 768×432 processed frames. Output
  negotiated YUYV/frame rate and the idle endpoint's original format was restored.
* Actual MP4/H.264/AAC processing, MKV/FFV1/PCM and WebM/VP9/Opus processing passed.
* Model HTTPS installation and SHA-256 verification passed; malformed/wrong weights
  are rejected. Processing commands do not initiate model downloads.

These tests do not establish detection accuracy across profile faces, demographics,
glasses, masks, low light or high motion. Occlusion tests exercise the safety state
machine with synthetic inputs; a detector false negative while another face remains
tracked can still leak. Multiple-face crossing does not guarantee persistent identity.

## Synthetic processing benchmarks

Two OpenCV/codec threads; 60 measured frames after ten warmup frames; YuNet on CPU with
640-pixel max input and detection interval 5. Synthetic random texture, one ground-truth
tracker/effect ROI with padding 0.3. The detector runs on the texture and does not detect
a real face. Tracker images are capped at 640 pixels; effects remain at output resolution.
Results include the benchmark frame copy, tracking and effect work. They exclude media
capture, decode, encode, rendering/window display and real-world detector accuracy.

| Effect | Resolution | Target fps | Mean ms | p95 ms | Processing fps |
| --- | --- | ---: | ---: | ---: | ---: |
| blur | 1280x720 | 30 | 3.730 | 8.764 | 268.1 |
| blur | 1920x1080 | 30 | 5.523 | 9.438 | 181.1 |
| blur | 1920x1080 | 60 | 4.998 | 8.939 | 200.1 |
| blur | 2560x1440 | 60 | 7.133 | 11.017 | 140.2 |
| blur | 3840x2160 | 30 | 13.338 | 17.508 | 75.0 |
| blur | 3840x2160 | 60 | 13.057 | 17.258 | 76.6 |
| glitch | 1280x720 | 30 | 2.720 | 7.464 | 367.6 |
| glitch | 1920x1080 | 30 | 3.499 | 7.174 | 285.8 |
| glitch | 1920x1080 | 60 | 3.595 | 7.275 | 278.2 |
| glitch | 2560x1440 | 60 | 5.134 | 9.182 | 194.8 |
| glitch | 3840x2160 | 30 | 10.040 | 15.560 | 99.6 |
| glitch | 3840x2160 | 60 | 10.096 | 14.049 | 99.0 |
| pixelate | 1280x720 | 30 | 2.210 | 6.709 | 452.5 |
| pixelate | 1920x1080 | 30 | 2.668 | 6.356 | 374.8 |
| pixelate | 1920x1080 | 60 | 2.746 | 6.990 | 364.2 |
| pixelate | 2560x1440 | 60 | 3.530 | 7.553 | 283.3 |
| pixelate | 3840x2160 | 30 | 6.265 | 10.642 | 159.6 |
| pixelate | 3840x2160 | 60 | 6.250 | 10.204 | 160.0 |

Raw stage measurements: [benchmark JSON files](benchmarks/).
Repeat with `python3 scripts/benchmark.py --binary build/blurcam --frames 60` after
installing the pinned model. `--model PATH` can select the same checksum-verified model.

The initial full-resolution tracker dominated 4K pixelation at 122.0 ms/frame;
overall 135.2 ms/frame. Downsampling tracking preparation, sharing optical-flow pyramids
across faces and limiting effect copies to ROIs reduced the same microbenchmark to
6.25 ms/frame (about 160 processing fps). This is not a claim of 4K webcam/encoder fps.

## File pipeline throughput

Public NASA-based two-face sample, 1920×1080 at 60 fps, 120 frames, pixelation effect,
CPU YuNet/tracking/effects/decode and two workers:

| Encoder | Measured complete file processing throughput |
| --- | ---: |
| libx264, fast, CRF 18 | 111.045 fps |
| h264_nvenc, p4, CQ 19, RTX 4090 | 185.113 fps |

These short, easily compressed samples omit audio. Audio retention is tested separately.
Timing includes native decode/process/encode/mux and finalization after stream setup,
not process startup or input metadata probing. NVENC uses system-memory frames, not a
zero-copy GPU surface pipeline. No GPU-inference or GPU-effect timings are claimed.

## Dependency check

The runtime-closure vulnix/NVD scan completed with advisory matches across 11 transitive
packages. This is **not a clean dependency-security result**. The scan is name/version
based and does not account for all Nix backports or distinguish similarly named projects.
[Recorded findings](dependency-scan.json) and [triage](SECURITY-AUDIT.md) preserve the
remaining uncertainties. Coreutils and all reported libssh2 issues have explicit named
Nix patches; Apache ORC and Ruby zlib entries are package-name mismatches. Other entries
need upstream/package follow-up; BlurCam does not invoke the reported cJSON utility,
printing/server, deprecated DNS-printing or libsndfile codec interfaces. FFmpeg advisory
fix ancestry could not be verified from its abbreviated upstream reference. No finding
was silently suppressed. Keep dependencies patched and sandbox hostile media.

## Scope and release gates

File processing, real webcam preview, actual loopback output, core effects, multiple
faces, tracking, padding, conservative fail-safe, CPU fallback, NVIDIA NVENC encoding,
Nix installation and documentation were exercised. This release contains source and
Nix packaging; no standalone GPL-linked binary bundle is uploaded.

Known limits: CPU decode/tracking/effects, optional unvalidated CUDA DNN build path,
no NVDEC/zero-copy or TensorRT/ONNX Runtime backend, no PipeWire-native source/screen
portal, no face/head segmentation, no reconnect, no subtitles/attachments/chapters,
8-bit SDR conversion, rotated input rejection, no network streaming, no real-world
accuracy evaluation. Architecture/docs deliberately distinguish these from implemented
features. x86_64 is tested; aarch64 is a flake target but not tested locally.

Next improvements: detector accuracy/occlusion corpus, GPU surface abstraction and
CUDA effects, validated CUDA detector packages, shared optical-flow pyramids with
landmark/scale fitting, crash-safe loopback timeout verification, camera reconnect,
color/HDR/rotation handling, deeper codec/container coverage and ARM hardware testing.
