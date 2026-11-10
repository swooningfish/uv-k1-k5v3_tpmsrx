#!/usr/bin/env bash
# Build build/TPMSRX.app in the repo image. Skips docker build when the
# image is already loaded (CI builds it with layer cache first).
set -euo pipefail

cd "$(dirname "$0")"
export MSYS_NO_PATHCONV=1

IMAGE=${IMAGE:-tpmsrx}
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" .
fi

docker run --rm -v "$PWD":/work -w /work "$IMAGE" \
    bash -lc 'python3 tools/fetch_sdk.py && ./build.sh && ./test/run_tests.sh'
