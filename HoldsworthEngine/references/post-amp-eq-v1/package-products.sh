#!/usr/bin/env bash
# Reuse the accepted isolated product builder; do not install plugins or clear caches.
set -euo pipefail
EQ_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
export TUNER_BUILD="${EQ_BUILD:-/private/tmp/nam-post-amp-eq-v1}"
bash "$EQ_PACKAGE/../tuner-v1/package-products.sh"
