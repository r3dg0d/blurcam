# Changelog

## 0.2.0 — 2026-11-18

* Added a Quickshell desktop UI for camera preview, v4l2loopback output, video export,
  and face model installation. Full-frame fail-safe is enabled by default in the UI.
* Added grayscale, sepia, duotone, threshold, emboss, and mirror, for 17 standalone
  filters in the UI.
* Added nine assignable filter slots. The UI saves assignments across launches, and
  the SDL preview uses keys 1–9 to switch filters.
* Fixed upper-body fail-safe coverage for known faces extending below the top 75%
  of the frame.

## 0.1.0 — 2026-10-05

* Native local file, preview, webcam and loopback pipelines.
* YuNet model installation/checksum verification and multi-face detection.
* Optical-flow association/smoothing with conservative padding and fail-safe censoring.
* Blur, mosaic, glitch and practical composable decorative effects; RGBA overlay.
* Native FFmpeg timestamp retention, audio copy and explicit NVENC encoding.
* TOML, profiles, CLI precedence, shell completions, man page, Nix flake and CI.
* Synthetic privacy/integration tests and published measured validation notes.

See docs/VALIDATION.md for tests, hardware verification, measurements and limitations.
