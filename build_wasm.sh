#!/usr/bin/env bash
# build_wasm.sh — Compile Panda3DS to WebAssembly and copy output to web/public/
#
# Prerequisites:
#   1. Emscripten SDK installed and activated:
#        git clone https://github.com/emscripten-core/emsdk
#        ./emsdk install latest && ./emsdk activate latest
#        source ./emsdk_env.sh
#      OR set EMSDK environment variable to the emsdk directory.
#
#   2. Git submodules initialised:
#        git submodule update --init --recursive
#
# Usage:
#   ./build_wasm.sh              # Release build
#   ./build_wasm.sh --debug      # Debug build (slower, better error messages)
#   ./build_wasm.sh --clean      # Delete build directory first
#
# NOTE: Dynarmic, the ARM JIT used by Panda3DS, generates native machine code
# at runtime which cannot run inside a WASM sandbox.  The build will attempt
# to compile dynarmic in interpreter-only mode (no JIT codegen).  If the build
# fails at the dynarmic step you may need to patch dynarmic to expose a pure
# interpreter backend, or replace it with an ARM interpreter library.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
WEB_PUBLIC="$SCRIPT_DIR/web/public"
BUILD_TYPE="Release"
CLEAN=false

# ─── argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --debug)  BUILD_TYPE="Debug" ;;
        --clean)  CLEAN=true ;;
        --help|-h)
            echo "Usage: $0 [--debug] [--clean]"
            exit 0
            ;;
    esac
done

# ─── locate emsdk ─────────────────────────────────────────────────────────────
if [[ -z "${EMSDK:-}" ]]; then
    # Try common locations
    for candidate in "$HOME/emsdk" "$HOME/.emsdk" "/opt/emsdk" "$SCRIPT_DIR/../emsdk"; do
        if [[ -f "$candidate/emsdk_env.sh" ]]; then
            EMSDK="$candidate"
            break
        fi
    done
fi

if [[ -z "${EMSDK:-}" ]]; then
    echo "ERROR: Could not find emsdk. Set the EMSDK environment variable to the"
    echo "       emsdk directory, or install it next to this repository."
    echo "  git clone https://github.com/emscripten-core/emsdk"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

echo "Using emsdk at: $EMSDK"
# shellcheck source=/dev/null
source "$EMSDK/emsdk_env.sh"

# ─── ensure submodules are present ────────────────────────────────────────────
echo "Checking submodules..."
git -C "$SCRIPT_DIR" submodule update --init --recursive

# ─── clean if requested ───────────────────────────────────────────────────────
if $CLEAN && [[ -d "$BUILD_DIR" ]]; then
    echo "Removing $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# ─── configure ────────────────────────────────────────────────────────────────
echo ""
echo "=== Configuring (CMake + Emscripten) ==="
cd "$BUILD_DIR"

emcmake cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    \
    -DENABLE_OPENGL=ON \
    -DENABLE_VULKAN=OFF \
    -DENABLE_METAL=OFF \
    -DENABLE_QT_GUI=OFF \
    -DENABLE_DISCORD_RPC=OFF \
    -DENABLE_LUAJIT=OFF \
    -DENABLE_FASTMEM=OFF \
    -DENABLE_HTTP_SERVER=OFF \
    -DENABLE_RENDERDOC_API=OFF \
    -DENABLE_GIT_VERSIONING=OFF \
    -DENABLE_TESTS=OFF \
    -DBUILD_HYDRA_CORE=OFF \
    -DBUILD_LIBRETRO_CORE=OFF \
    \
    -DOPENGL_PROFILE=OpenGLES \
    \
    -DDYNARMIC_ENABLE_CPU_FEATURE_DETECTION=OFF \
    -DDYNARMIC_TESTS=OFF \
    \
    -DCRYPTOPP_OPT_DISABLE_ASM=ON \
    \
    -DCMAKE_EXE_LINKER_FLAGS="-sUSE_SDL=2"

# ─── build ────────────────────────────────────────────────────────────────────
echo ""
echo "=== Building (this will take a while) ==="
NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
emmake make -j"$NPROC" Alber

# ─── copy output to web/public ────────────────────────────────────────────────
echo ""
echo "=== Copying output to $WEB_PUBLIC ==="
mkdir -p "$WEB_PUBLIC"

cp -v "$BUILD_DIR/panda3ds.js"   "$WEB_PUBLIC/panda3ds.js"
cp -v "$BUILD_DIR/panda3ds.wasm" "$WEB_PUBLIC/panda3ds.wasm"

echo ""
echo "================================================================"
echo "  Build complete!"
echo "  panda3ds.js   → $WEB_PUBLIC/panda3ds.js"
echo "  panda3ds.wasm → $WEB_PUBLIC/panda3ds.wasm"
echo ""
echo "  To run the web app:"
echo "    cd web && npm install && npm run dev"
echo "================================================================"
