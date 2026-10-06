# Architecture and decisions

C++20 provides direct, established interfaces to OpenCV, libav and V4L2. Rust was considered,
but adding binding/version layers for both multimedia and CV would complicate the first
working pipeline. Python remains test/benchmark tooling only; no per-frame subprocesses.

* `config.cpp`: XDG paths, TOML, presets, validation, exclusive config writes.
* `detection.cpp`: `Detector` interface and checksum-verified YuNet backend.
* `tracking.cpp`: forward/backward Lucas–Kanade flow, texture quality checks, IoU identity
  association, EMA coordinates, conservative coverage union.
* `effects.cpp`: catalog, ordered stack, bounded ROI transforms, box/ellipse masks, RGBA.
* `pipeline.cpp`: safety state machine, reacquisition, selection, stage measurements.
* `media.cpp`: RAII libav demux/decode/mux/encode, timestamp rescaling and audio packet copy,
  pixel budgets, local-protocol allowlist, transactional private output.
* `live.cpp`: replaceable frame slot, capture thread, SDL preview, V4L2 loopback output,
  shutdown protection, device/mode enumeration, benchmark.
* `models.cpp`: fixed HTTPS origin/revision, bounded download, TLS checks, SHA-256,
  atomic model installation and startup verification.
* `main.cpp`: CLI11 frontend; config bootstrap before final parser implements precedence.

## Backend assessment

| Candidate | Decision |
| --- | --- |
| YuNet/OpenCV DNN | Default: small MIT weights, CPU support, five landmarks, optional CUDA, existing native library |
| MediaPipe BlazeFace/landmarks | Useful fast model/tracking task graph; extra runtime and model integration deferred |
| RetinaFace/SCRFD | Strong accuracy candidates; InsightFace pretrained weights are noncommercial by default; do not bundle |
| YOLO face variants | Per-repository/model licensing and often AGPL implications; no universal license assumption |
| ONNX Runtime CUDA | Useful separate inference backend; adds CUDA/cuDNN version compatibility and deployment requirements |
| TensorRT | High-throughput future backend; engine compilation/version management and available hardware validation needed |
| NCNN/Vulkan | Interesting portable GPU inference; another integration not needed for initial CPU workflows |
| Native libav | Chosen for frame PTS/durations, audio packet copying and direct NVENC access |
| FFmpeg CLI raw pipes | Good interoperability but lose per-frame timestamp metadata unless a second framing protocol is added |
| GStreamer/CUDA | Good future graph/surface-sharing alternative; not implemented alongside libav |
| PipeWire source | Desktop-native possibility but portal/app camera discovery and interoperability need testing |
| v4l2loopback | Chosen: established Linux camera interface, OBS/WebRTC support with exclusive_caps |

The first pipeline has CPU image buffers; CUDA DNN and NVENC involve transfers. We do
not advertise NVDEC or zero-copy GPU effects. A future surface abstraction must retain
frame timing, stride, ownership and GPU synchronization across decode/inference/effects.
Async capture is implemented; inference runs synchronously, which avoids stale detection
results being applied to unrelated current frames. A future async detector must associate
results with frame IDs and track their regions forward before rendering.

Five sparse facial landmarks do not define full forehead/chin/head coverage. Box is the
privacy default; segmentation/hull masks need a separate validated model. Optical flow
tracks translation; detections update scale and pose. Identity association is not face
recognition and can change during crossings.

## References (primary upstream sources consulted)

* https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet
* https://docs.opencv.org/4.x/df/d20/classcv_1_1FaceDetectorYN.html
* https://docs.opencv.org/4.x/dc/d6b/group__video__track.html
* https://developers.google.com/edge/mediapipe/solutions/vision/face_detector
* https://github.com/deepinsight/insightface
* https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html
* https://docs.nvidia.com/deeplearning/tensorrt/latest/inference-library/python-api-docs.html
* https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html
* https://gstreamer.freedesktop.org/documentation/cuda/index.html
* https://docs.pipewire.org/page_portal.html
* https://github.com/v4l2loopback/v4l2loopback/blob/main/README.md

Model snapshot is fixed at opencv_zoo revision 47534e27c9851bb1128ccc0102f1145e27f23f98;
YuNet 2023mar is selected for the OpenCV 4.x API. Claims about accuracy are upstream
results, not local accuracy evaluation; no accuracy numbers are claimed by BlurCam.
