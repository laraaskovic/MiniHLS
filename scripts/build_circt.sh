#!/usr/bin/env bash
#
# Build and install LLVM, MLIR, and CIRCT from source for MiniHLS.
#
# CIRCT publishes prebuilt development packages only for x86-64 Linux. On an
# ARM64 machine (such as Windows on ARM running WSL) there is no prebuilt that
# can run, so everything is built here. On x86-64 Linux, the prebuilt
# `circt-full-shared-linux-x64.tar.gz` from the CIRCT releases page is a much
# faster alternative: extract it and point CIRCT_PREFIX at it.
#
# The build is a single "unified" one: LLVM's build system builds MLIR as an
# LLVM project and CIRCT as an external project, from the LLVM sources that
# the CIRCT release bundles. That guarantees the LLVM version is the one CIRCT
# was tested against.
#
# Choices that keep the build inside a machine with ~8 GB of RAM:
#   - shared libraries, so linking each tool is cheap;
#   - a limited number of parallel compile jobs and a single link job;
#   - only the host CPU as a code generation target;
#   - no LLVM tools, examples, or tests beyond what MLIR and CIRCT need.
# Assertions stay on: when our code misuses an MLIR API, an assertion names the
# problem instead of producing silently broken IR.
#
# Environment overrides:
#   CIRCT_VERSION   release to build (default: 1.159.0)
#   CIRCT_PREFIX    install location (default: ~/.local/circt)
#   SRC_DIR         where the sources are unpacked (default: ~/.local/src/circt-src)
#   BUILD_DIR       build tree (default: ~/.cache/circt-build)
#   COMPILE_JOBS    parallel compile jobs (default: 5)
#   NINJA           ninja binary (default: ninja on PATH, else ~/.local/venvs/build/bin/ninja)

set -euo pipefail

version="${CIRCT_VERSION:-1.159.0}"
prefix="${CIRCT_PREFIX:-$HOME/.local/circt}"
src_dir="${SRC_DIR:-$HOME/.local/src/circt-src}"
build_dir="${BUILD_DIR:-$HOME/.cache/circt-build}"
compile_jobs="${COMPILE_JOBS:-5}"

if [[ -n "${NINJA:-}" ]]; then
  ninja_bin="${NINJA}"
elif command -v ninja >/dev/null 2>&1; then
  ninja_bin="$(command -v ninja)"
else
  # No root is needed: Ninja installs from PyPI into a private venv.
  if [[ ! -x "$HOME/.local/venvs/build/bin/ninja" ]]; then
    python3 -m venv "$HOME/.local/venvs/build"
    "$HOME/.local/venvs/build/bin/pip" install -q ninja
  fi
  ninja_bin="$HOME/.local/venvs/build/bin/ninja"
fi

if [[ ! -f "${src_dir}/llvm/llvm/CMakeLists.txt" ]]; then
  echo "==> fetching CIRCT ${version} sources"
  mkdir -p "${src_dir}"
  tarball="$(dirname "${src_dir}")/circt-full-sources-${version}.tar.gz"
  base="https://github.com/llvm/circt/releases/download/firtool-${version}"
  curl -sSL -o "${tarball}" "${base}/circt-full-sources.tar.gz"
  expected="$(curl -sSL "${base}/circt-full-sources.tar.gz.sha256" | awk '{print $1}')"
  echo "${expected}  ${tarball}" | sha256sum -c
  tar xzf "${tarball}" -C "${src_dir}"
fi

# gold links noticeably faster and with less memory than the default GNU ld.
linker_flag=()
if command -v ld.lld >/dev/null 2>&1; then
  linker_flag=(-DLLVM_USE_LINKER=lld)
elif command -v ld.gold >/dev/null 2>&1; then
  linker_flag=(-DLLVM_USE_LINKER=gold)
fi

echo "==> configuring (source ${src_dir}, build ${build_dir}, prefix ${prefix})"
cmake -G Ninja -S "${src_dir}/llvm/llvm" -B "${build_dir}" \
  -DCMAKE_MAKE_PROGRAM="${ninja_bin}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_EXTERNAL_PROJECTS=circt \
  -DLLVM_EXTERNAL_CIRCT_SOURCE_DIR="${src_dir}" \
  -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_PARALLEL_COMPILE_JOBS="${compile_jobs}" \
  -DLLVM_PARALLEL_LINK_JOBS=1 \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INSTALL_UTILS=ON \
  -DMLIR_INCLUDE_TESTS=OFF \
  -DCIRCT_INCLUDE_TESTS=OFF \
  "${linker_flag[@]}"

echo "==> building (this takes a few hours on a laptop)"
"${ninja_bin}" -C "${build_dir}" install

echo "==> installed to ${prefix}"
"${prefix}/bin/circt-opt" --version | head -3
