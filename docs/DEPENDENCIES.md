# Dependencies and distribution

| Dependency | Requirement | Upstream license | Use |
| --- | --- | --- | --- |
| OpenCV | ≥4.8 | Apache-2.0 | detector, tracking, effects |
| FFmpeg libav | ≥6 | LGPL-2.1-or-later, GPL when built with GPL components | media |
| CLI11 | ≥2.4 | BSD-3-Clause | arguments |
| toml++ | ≥3.3 | MIT | configuration |
| SDL2 | ≥2 | zlib | preview |
| libcurl | HTTPS support | curl license | explicit model download |
| OpenSSL | modern | Apache-2.0 | checksum, TLS dependency |
| YuNet 2023mar | exact SHA-256 | MIT | separately installed weights |
| Python | ≥3.11, tests only | PSF | integration/benchmark tooling |
| v4l2loopback | host kernel-compatible | GPL-2.0 | optional host virtual camera module |

BlurCam source is MIT. Source licensing does not erase the obligations of linked libraries.
Distribution of binaries linked to GPL-enabled FFmpeg (including libx264/libx265) must
meet applicable GPL conditions, including corresponding source/build information for the
complete distributed work. Do not label such a binary bundle as MIT-only. The release distributes source, not an MIT-only binary bundle.
Nix users build packages with their pinned dependency source recipes; model installation
is separate. libcurl/libav link to system/Nix libraries, not vendored unreviewed blobs.

Versions are constrained in CMake; the complete Nix package set is pinned in flake.lock.
GitHub CI builds through the flake and scans the built closure using vulnix. A closure
scan is advisory vulnerability evidence, not proof of safety, and needs current advisory
data. Runtime parsers must be kept patched. Model weights are data, not executables;
checksum verification prevents unreviewed graphs replacing the supported model.
