# Security and privacy

Report exploitable vulnerabilities privately using GitHub private vulnerability reporting
if enabled, or contact the maintainer privately before opening an exploit-bearing issue.
Do not attach sensitive video to public issues. Use synthetic/public-domain reproductions.

BlurCam performs no telemetry or cloud inference. Only `models install` contacts a fixed
HTTPS upstream. Normal media readers allow local file/pipe protocols, not network streams.
Paths are passed to native APIs, never interpolated into shell commands. Model installation
is bounded and checksum-verified. Config writes are exclusive. File outputs use a private
mkstemp file and no-clobber atomic commit by default. Ctrl+C removes incomplete file output.

Malformed video, images and models still reach native parsers. Keep dependencies current;
process untrusted media with OS-level resource/sandbox limits. Pixel budgets are enforced
but are not a complete sandbox against decoder exploits or resource exhaustion.

Detection failures and frames skipped between refreshes can expose faces. Full-frame
fail-safe protects observable uncertainty, not unobserved false negatives. Decorative
filters/transparent masks are not anonymization guarantees. Review all sensitive output.
Debug overlays contain positions, IDs and confidence; enable them intentionally.
Abrupt SIGKILL/power loss cannot send a final black frame. Configure and test loopback
timeout behavior for critical sessions. Live output stalls terminate rather than buffer.
