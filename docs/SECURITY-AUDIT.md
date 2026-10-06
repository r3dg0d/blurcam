# Dependency audit — 2026-10-05

Command: `vulnix --closure --json /path/to/blurcam-package` against NVD CVE 2.0 feeds.
The full runtime closure produced name/version matches across **11 packages**. These
are not 11 confirmed exploitable BlurCam defects, and they are not dismissed as harmless.
[Machine-readable findings](dependency-scan.json) retain IDs/scores without private paths.
No automatic whitelist was added. CI preserves the scanner output as an advisory artifact.

| Package | Review evidence and disposition |
| --- | --- |
| coreutils 9.11 | Nix derivation has named patches for both reported CVE-2026-56391/56392. BlurCam does not invoke these commands. |
| libssh2 1.11.1 | Nix derivation contains named patches for all ten reported CVEs. BlurCam model downloads restrict curl to HTTPS and media readers to file/pipe; no SSH/SFTP operation is requested. |
| orc 0.4.44 | Scanner's CVE-2025-47436 describes **Apache ORC**, not GStreamer's **Oil Runtime Compiler** used in this closure. Package-name mismatch. |
| zlib 1.3.2 | CVE-2026-27820 describes Ruby's zlib binding, not this C zlib. CVE-2023-6992 concerns Cloudflare's fork. Package/vendor mismatch; do not broadly whitelist future zlib findings. |
| gcc 16.2 | CVE-2023-4039 is a disputed AArch64 dynamic-stack hardening issue. This validation is x86_64; no variable-length stack arrays/alloca are used by BlurCam. ARM remains untested. |
| cups 2.4.19 | Reported CVE describes macOS privilege escalation; no print operation or CUPS daemon is started by BlurCam. Nix contains additional security backports, but exact reported-CVE status was not independently proven. |
| glibc 2.44 | Reports concern deprecated DNS record printing, explicitly outside resolver execution paths in the advisory. BlurCam does not call these interfaces. One listed range ends at 2.43; patch ancestry for the other was not independently established. |
| avahi 0.8 | Closure includes client/service libraries; BlurCam does not launch an Avahi daemon or use service discovery. Reported daemon/deployment issues remain an upstream/package concern. |
| cJSON 1.7.19 | Reports concern utility patches/comparison or other cJSON paths; BlurCam does not use cJSON APIs. No named backport matching these reported IDs was found; transitive dependency follow-up remains. |
| libsndfile 1.2.2 | Codec/parser findings require upstream/package follow-up; this project uses libav video decoding and packet-copies audio, never calling libsndfile codec APIs. An existing Nix patch does not establish remediation of every listed CVE. |
| FFmpeg 9.0.1 | Two reports describe older git-master memory leaks before abbreviated commit d5873b. The cited short commit could not be resolved for independent ancestry verification, so these entries remain unverified rather than declared fixed. |

Nix patch names and closure metadata were inspected locally. Version/CPE scans cannot
prove reachability or the absence of other vulnerabilities. This audit does not certify
the dependencies safe. Standalone binaries with the whole dependency bundle are not
uploaded; source/Nix recipes make versions and builds inspectable. Use a patched package
set and OS sandbox/resource limits for hostile input. The workflow does not hide scanner
errors as a successful security clearance: it uploads the report and treats the scan as
advisory while build/tests/static analysis remain required.

Primary advisory details: each CVE is available at `https://nvd.nist.gov/vuln/detail/ID`.
Package recipes are pinned through flake.lock. No credentials, camera images or personal
paths are included in this report.
