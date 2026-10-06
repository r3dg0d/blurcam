# Contributing

Use `nix develop` or install the development dependencies listed in README.
Run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, build, then CTest.
Run `clang-format -i src/*.cpp include/blurcam/*.hpp tests/core.cpp` before committing.
Model-free tests are offline. To include detector/file/live tests, install the pinned model
and export `BLURCAM_TEST_MODEL` to its absolute path. Never download a model implicitly
from a processing command. Never commit private face photos, recordings, tokens or builds.

For safety-sensitive changes add a failure-path test, particularly detector exceptions,
tracker loss and fail-safe recovery. Keep output audio/timestamp regression tests.
Use ASan/UBSan via `-DBLURCAM_SANITIZE=ON` for memory changes. Document measured performance
and fixture limitations rather than presenting microbenchmarks as camera FPS.

Effects currently implement catalog entries and ROI render functions in effects.cpp;
keep parameter validation in config.cpp and output outside the ROI untouched. Preserve
ordering and seeded reproducibility. Backend additions should implement the detector
interface or separate media/sink APIs with ownership and timestamps explicit.
