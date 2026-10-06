#!/usr/bin/env bash
# Isolated Release products: no plugin installation or user AU-cache changes.
set -euo pipefail
PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO="$(cd -- "$PACKAGE/../../.." && pwd)"
TUNER_BUILD="${TUNER_BUILD:-/private/tmp/nam-tuner-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
mkdir -p "$TUNER_BUILD/products" "$TUNER_BUILD/noop-scripts"
printf '#!/bin/sh\nexit 0\n' > "$TUNER_BUILD/noop-scripts/clear_audiounit_caches.command"
chmod +x "$TUNER_BUILD/noop-scripts/clear_audiounit_caches.command"
cd "$REPO/NeuralAmpModeler"
for format in APP VST3 AU; do
  format_root="$TUNER_BUILD/build-$format"
  mkdir -p "$format_root"
  export CLANG_MODULE_CACHE_PATH="$format_root/module-cache"
  xcodebuild -quiet -project ./projects/NeuralAmpModeler-macOS.xcodeproj \
    -target "$format" -configuration Release DEMO_VERSION=0 \
    MACOSX_DEPLOYMENT_TARGET=12.0 ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
    BUILD_DIR="$TUNER_BUILD/products" CONFIGURATION_BUILD_DIR="$TUNER_BUILD/products" \
    SYMROOT="$format_root/sym" OBJROOT="$format_root/obj" \
    MODULE_CACHE_DIR="$format_root/module-cache" CLANG_MODULE_CACHE_PATH="$format_root/module-cache" \
    SHARED_PRECOMPS_DIR="$format_root/precompiled" \
    DEPLOYMENT_LOCATION=NO SKIP_INSTALL=YES \
    APP_PATH="$format_root/not-installed-app" VST3_PATH="$format_root/not-installed-vst3" \
    AU_PATH="$format_root/not-installed-au" SCRIPTS_PATH="$TUNER_BUILD/noop-scripts" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= \
    > "$format_root/build.log" 2>&1
  case "$format" in APP) extension=app;; VST3) extension=vst3;; AU) extension=component;; esac
  product="$TUNER_BUILD/products/NeuralAmpModeler.$extension"
  test ! -L "$product"
  test -s "$product/Contents/MacOS/NeuralAmpModeler"
  test -f "$product/Contents/Info.plist"
  codesign --force --deep --sign - "$product"
  codesign --verify --deep --strict --verbose=2 "$product"
  plutil -lint "$product/Contents/Info.plist"
  file "$product/Contents/MacOS/NeuralAmpModeler"
  printf 'PASS Release %s: %s\n' "$format" "$product"
done
