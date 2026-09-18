#!/usr/bin/env bash
#
# Configure, build, and run every MiniHLS test suite. This is the one command
# referenced by milestone 0.
#
# Environment overrides:
#   BUILD_DIR   where to put the build tree (default: ~/.cache/minihls/build)
#   BUILD_TYPE  CMake build type (default: Release)
#   JOBS        parallel build jobs (default: nproc)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The repository normally lives on a Windows drive that WSL reaches over 9p,
# where compiling is several times slower than on the Linux filesystem. Default
# the build tree off the mount and let the caller put it back if they want it
# visible from Windows.
build_dir="${BUILD_DIR:-$HOME/.cache/minihls/build}"
build_type="${BUILD_TYPE:-Release}"
jobs="${JOBS:-$(nproc)}"

echo "==> source ${repo_root}"
echo "==> build  ${build_dir} (${build_type}, ${jobs} jobs)"

cmake -S "${repo_root}" -B "${build_dir}" -DCMAKE_BUILD_TYPE="${build_type}"
cmake --build "${build_dir}" -j "${jobs}"

echo
echo "==> test"
ctest --test-dir "${build_dir}" --output-on-failure

echo
# Invoked through bash rather than directly: the repository is checked out on a
# Windows filesystem, so the executable bit is not reliably present.
bash "${repo_root}/scripts/area_report.sh"
