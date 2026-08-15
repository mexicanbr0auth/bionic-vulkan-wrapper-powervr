#!/usr/bin/env bash
# Cross-build libvulkan_wrapper.so (leegao bionic-vulkan-wrapper, PowerVR fork)
# for Android arm64-v8a using the Android NDK directly (no Termux required).
#
# Produces a Winlator-compatible wrapper.tzst in ./dist/
#
# Required environment:
#   ANDROID_NDK  - path to the Android NDK root
#   WORKDIR      - scratch dir (defaults to ./build-ndk)
#   PREFIX       - staging install dir (defaults to $WORKDIR/prefix)
set -euo pipefail

NDK="${ANDROID_NDK:?set ANDROID_NDK to the NDK root}"
WORKDIR="${WORKDIR:-$(pwd)/build-ndk}"
PREFIX="${PREFIX:-$WORKDIR/prefix}"
SRC="$(pwd)"
DIST="$(pwd)/dist"
API=26
TRIPLE="aarch64-linux-android"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
CC="$TC/$TRIPLE$API-clang"
CXX="$TC/$TRIPLE$API-clang++"
AR="$TC/llvm-ar"
RANLIB="$TC/llvm-ranlib"
NM="$TC/llvm-nm"
STRIP="$TC/llvm-strip"
PKGCFG="$(command -v pkg-config)"

DL_DIR="$WORKDIR/downloads"
SRC_DIR="$WORKDIR/src"

JOBS="$(nproc)"
export PATH="$TC:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export CPPFLAGS="-D__USE_GNU -I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib -Wl,-rpath-link,$PREFIX/lib"
export LIBS=""

log()  { echo -e "\n\e[1;34m==> $*\e[0m" >&2; }
fail() { echo -e "\e[1;31mERROR: $*\e[0m" >&2; exit 1; }

mkdir -p "$DL_DIR" "$SRC_DIR" "$PREFIX" "$DIST" \
         "$PREFIX/lib/pkgconfig" "$PREFIX/share/pkgconfig" \
         "$PREFIX/include"

fetch() { # fetch <name> <url> [tarflags]
  local name="$1" url="$2" flags="${3:-}"
  local arc="$DL_DIR/$name"
  if [ ! -f "$arc" ]; then
    log "Downloading $name"
    curl -fsSL -o "$arc" "$url"
  fi
  local dir="$SRC_DIR/${name%.tar.*}"
  [ -d "$dir" ] || { mkdir -p "$dir"; tar -xf "$arc" -C "$dir" --strip-components=1 $flags; }
  echo "$dir"
}

cross_configure() { # cross_configure <srcdir> <extra...>
  local dir="$1"; shift
  local builddir="$dir/.build"
  mkdir -p "$builddir"
  ( cd "$builddir"
    PKG_CONFIG="$PKGCFG" \
    CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" NM="$NM" STRIP="$STRIP" \
    CPPFLAGS="$CPPFLAGS" LDFLAGS="$LDFLAGS" LIBS="$LIBS" \
    "$dir/configure" --host="$TRIPLE" --prefix="$PREFIX" \
      PKG_CONFIG="$PKGCFG" \
      ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes \
      xorg_cv_malloc0_returns_null=no \
      "$@"
    make -j"$JOBS" && make install )
}

# ---------------------------------------------------------------------------
# 0. Runtime libs that are already committed upstream (built for bionic/arm64):
#    libadrenotools.so + libandroid-sysvshm.so + companion hook libs + ICD json
# ---------------------------------------------------------------------------
RPK="https://raw.githubusercontent.com/leegao/vulkan_wrapper_termux-packages/dev/wrapper/wrapper"
log "Fetching prebuilt runtime libs (adrenotools, hooks) from leegao fork"
mkdir -p "$PREFIX/lib" "$PREFIX/share/vulkan/icd.d"
curl -fsSL -o "$PREFIX/lib/libadrenotools.so" "$RPK/usr/lib/libadrenotools.so"
for f in libfile_redirect_hook.so libgsl_alloc_hook.so \
         libhook_impl.so libmain_hook.so; do
  [ -f "$PREFIX/lib/$f" ] || \
    curl -fsSL -o "$PREFIX/lib/$f" "$RPK/termux/data/data/com.termux/files/usr/lib/$f"
done
[ -f "$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" ] || \
  curl -fsSL -o "$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" \
    "$RPK/termux/data/data/com.termux/files/usr/share/vulkan/icd.d/wrapper_icd.aarch64.json"

# bionic has no libpthread.so/librt.so; create stubs so configure checks pass
printf '' > "$PREFIX/lib/libpthread.so"
printf '' > "$PREFIX/lib/librt.so"

# bionic lacks glibc's values.h (libxshmfence uses MAXINT from it)
cat > "$PREFIX/include/values.h" <<EOF
#ifndef _ANDROID_VALUES_H_
#define _ANDROID_VALUES_H_
#include <limits.h>
#define MAXINT    INT_MAX
#define MAXLONG   LONG_MAX
#define MAXSHORT  SHRT_MAX
#define MAXUINT   UINT_MAX
#define MAXULONG  ULONG_MAX
#define MAXUSHORT USHRT_MAX
#endif
EOF

# ---------------------------------------------------------------------------
# 1. zlib
# ---------------------------------------------------------------------------
log "Building zlib"
zdir=$(fetch zlib-1.3.1.tar.gz \
  "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz")
( cd "$zdir"
  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
  cmake --build build -j"$JOBS" && cmake --install build )
cat > "$PREFIX/lib/pkgconfig/zlib.pc" <<EOF
prefix=$PREFIX
includedir=\${prefix}/include
libdir=\${prefix}/lib
Name: zlib
Description: zlib compression library
Version: 1.3.1
Libs: -L\${libdir} -lz
Cflags: -I\${includedir}
EOF

# ---------------------------------------------------------------------------
# 2. zstd
# ---------------------------------------------------------------------------
log "Building zstd"
zstdir=$(fetch zstd-1.5.6.tar.gz \
  "https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz")
( cmake -S "$zstdir/build/cmake" -B "$zstdir/build/cmake/build" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
    -DZSTD_BUILD_SHARED=ON -DZSTD_BUILD_STATIC=OFF \
    -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF \
    -DZSTD_BUILD_BENCHMARKS=OFF -DZSTD_BUILD_FUZZERS=OFF
  cmake --build "$zstdir/build/cmake/build" -j"$JOBS" && cmake --install "$zstdir/build/cmake/build" )

# ---------------------------------------------------------------------------
# 3. libdrm
# ---------------------------------------------------------------------------
log "Building libdrm"
drmdir=$(fetch libdrm-2.4.123.tar.xz \
  "https://dri.freedesktop.org/libdrm/libdrm-2.4.123.tar.xz" "--no-same-owner")
cat > "$WORKDIR/meson-drm.txt" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
ranlib = '$RANLIB'
strip = '$STRIP'
pkgconfig = '$PKGCFG'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
( cd "$drmdir"
  PKG_CONFIG="$PKGCFG" \
  meson setup build --cross-file="$WORKDIR/meson-drm.txt" \
    --prefix="$PREFIX" --buildtype=release \
    -Dtests=false -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled \
    -Dnouveau=disabled -Dvmwgfx=disabled -Dvc4=disabled -Dfreedreno=disabled \
    -Detnaviv=disabled -Dexynos=disabled -Dtegra=disabled \
    -Dman-pages=disabled -Dvalgrind=disabled -Dcairo-tests=disabled \
    -Dinstall-test-programs=false
  meson compile -C build -j"$JOBS" && meson install -C build )

# ---------------------------------------------------------------------------
# 4. X11 stack (xcb-proto, xau, xdmcp, xcb, xshmfence, xorgproto, xtrans, libX11)
# ---------------------------------------------------------------------------
xorgurl="https://xorg.freedesktop.org/archive/individual"

log "Building xcb-proto (host)"
xcp=$(fetch xcb-proto-1.17.0.tar.xz "$xorgurl/proto/xcb-proto-1.17.0.tar.xz")
( cd "$xcp"
  ./configure --prefix="$PREFIX" PYTHON=python3
  make -j"$JOBS" install )

log "Building xorgproto"
xproto=$(fetch xorgproto-2024.1.tar.xz "$xorgurl/proto/xorgproto-2024.1.tar.xz")
cross_configure "$xproto"

log "Building xtrans"
xtr=$(fetch xtrans-1.5.1.tar.xz "$xorgurl/lib/xtrans-1.5.1.tar.xz")
cross_configure "$xtr"

log "Building libXau"
xau=$(fetch libXau-1.0.11.tar.xz "$xorgurl/lib/libXau-1.0.11.tar.xz")
cross_configure "$xau"

log "Building libXdmcp"
xdmcp=$(fetch libXdmcp-1.1.5.tar.xz "$xorgurl/lib/libXdmcp-1.1.5.tar.xz")
cross_configure "$xdmcp"

log "Building libxshmfence"
xshm=$(fetch libxshmfence-1.3.2.tar.xz "$xorgurl/lib/libxshmfence-1.3.2.tar.xz")
cross_configure "$xshm"

log "Building libxcb"
xcb=$(fetch libxcb-1.17.0.tar.xz "$xorgurl/lib/libxcb-1.17.0.tar.xz")
cross_configure "$xcb" \
  --disable-static \
  --disable-pthread-stubs \
  --disable-xinput --disable-xkb --disable-xinerama \
  --disable-xv --disable-xtest --disable-xprint --disable-util \
  --enable-dri3 --enable-present --enable-sync --enable-randr --enable-shm \
  --enable-xfixes

log "Building libX11"
x11=$(fetch libX11-1.8.10.tar.xz "$xorgurl/lib/libX11-1.8.10.tar.xz")
cross_configure "$x11" --disable-static --enable-xcb

log "Building libXext / libXrender / libXrandr (WSI x11)"
xext=$(fetch libXext-1.3.6.tar.xz "$xorgurl/lib/libXext-1.3.6.tar.xz")
cross_configure "$xext" --disable-static
xrender=$(fetch libXrender-0.9.11.tar.xz "$xorgurl/lib/libXrender-0.9.11.tar.xz")
cross_configure "$xrender" --disable-static
xrandr=$(fetch libXrandr-1.5.4.tar.xz "$xorgurl/lib/libXrandr-1.5.4.tar.xz")
cross_configure "$xrandr" --disable-static

# ---------------------------------------------------------------------------
# 5. SPIRV-Tools (static) -- leegao fork (v2025.3 = vulkan-sdk-1.4.304.0) with
#    the Mali passes the wrapper needs (FixMaliSpecConstantComposite /
#    MaliOptimizationBarrier / RemoveClipCullDist), pinned by commit.
# ---------------------------------------------------------------------------
log "Building SPIRV-Tools (leegao fork)"
spvsha="9113deed32ba366b765a148f474ca86c3890db6a"
spvhsha="a863779"
spvh=$(fetch "SPIRV-Headers-$spvhsha.tar.gz" \
  "https://github.com/KhronosGroup/SPIRV-Headers/archive/$spvhsha.tar.gz")
spvt=$(fetch "SPIRV-Tools-leegao-$spvsha.tar.gz" \
  "https://github.com/leegao/SPIRV-Tools/archive/$spvsha.tar.gz")
( cd "$spvt"
  cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSPIRV-Headers_SOURCE_DIR="$spvh" \
    -DSPIRV_SKIP_TESTS=ON -DSPIRV_WERROR=OFF -DBUILD_SHARED_LIBS=OFF
  cmake --build build -j"$JOBS" --target SPIRV-Tools-static SPIRV-Tools-opt )
mkdir -p "$SRC/src/vulkan/wrapper/lib"
cp "$spvt/build/source/libSPIRV-Tools.a" "$SRC/src/vulkan/wrapper/lib/"
cp "$spvt/build/source/opt/libSPIRV-Tools-opt.a" "$SRC/src/vulkan/wrapper/lib/"

# ---------------------------------------------------------------------------
# 6. Build libvulkan_wrapper.so
# ---------------------------------------------------------------------------
log "Building libvulkan_wrapper.so"
cat > "$WORKDIR/meson-wrapper.txt" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
ranlib = '$RANLIB'
strip = '$STRIP'
pkgconfig = '$PKGCFG'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

( cd "$SRC"
  CC="$CC" CXX="$CXX" PKG_CONFIG="$PKGCFG" \
  CFLAGS="-g" CXXFLAGS="-g" \
  CPPFLAGS="$CPPFLAGS -D__TERMUX__" \
  LDFLAGS="$LDFLAGS -Wl,--as-needed -L$PREFIX/lib -ladrenotools" \
  meson setup "$WORKDIR/wrapper-build" \
    --cross-file="$WORKDIR/meson-wrapper.txt" \
    -Dvulkan-drivers=wrapper \
    -Dplatforms=x11 \
    -Dgbm=disabled -Dopengl=false -Dllvm=disabled -Dshared-llvm=disabled \
    -Dgallium-drivers= -Dxmlconfig=disabled -Dcpp_rtti=false -Db_ndebug=true \
    -Dzstd=disabled \
    --buildtype=release --prefix="$PREFIX" \
  || { echo "MESON SETUP FAILED"; tail -60 "$WORKDIR/wrapper-build/meson-logs/meson-log.txt"; exit 1; }
  meson compile -C "$WORKDIR/wrapper-build" -j"$JOBS" )

# Keep an unstripped, -g build for offline symbolization (addr2line) of the
# crash handler's pc=/proc/self/maps dump.
cp "$WORKDIR/wrapper-build/src/vulkan/wrapper/libvulkan_wrapper.so" \
   "$DIST/libvulkan_wrapper.so.debug"

# ---------------------------------------------------------------------------
# 7. Package -> dist/wrapper.tzst (Winlator graphics_driver layout)
# ---------------------------------------------------------------------------
log "Packaging dist/wrapper.tzst"
rm -rf "$WORKDIR/package"
mkdir -p "$WORKDIR/package/usr/lib" "$WORKDIR/package/usr/share/vulkan/icd.d"
"$STRIP" "$WORKDIR/wrapper-build/src/vulkan/wrapper/libvulkan_wrapper.so"
cp "$WORKDIR/wrapper-build/src/vulkan/wrapper/libvulkan_wrapper.so" \
   "$WORKDIR/package/usr/lib/"
for f in libadrenotools.so \
         libfile_redirect_hook.so libgsl_alloc_hook.so \
         libhook_impl.so libmain_hook.so; do
  cp "$PREFIX/lib/$f" "$WORKDIR/package/usr/lib/"
done
cp "$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" \
   "$WORKDIR/package/usr/share/vulkan/icd.d/"
cp "$WORKDIR/package/usr/lib/libvulkan_wrapper.so" "$DIST/libvulkan_wrapper.so"

( cd "$WORKDIR/package" && tar -c -I 'zstd -19' -f "$DIST/wrapper.tzst" usr )

log "Done."
echo "  artifacts:"
ls -la "$DIST"
