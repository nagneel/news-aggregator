#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VENDOR="$ROOT/vendor/cjson"
WEB="$ROOT/web"

echo "==> Downloading cJSON…"
mkdir -p "$VENDOR"
curl -sSfL -o "$VENDOR/cJSON.h" \
  "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h"
curl -sSfL -o "$VENDOR/cJSON.c" \
  "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c"

echo "==> Compiling with Emscripten…"
emcc \
  "$ROOT/src/api.c" \
  "$ROOT/src/cluster.c" \
  "$ROOT/src/ranker.c" \
  "$VENDOR/cJSON.c" \
  -I"$VENDOR" \
  -I"$ROOT/src" \
  -O2 \
  -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_process_articles"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s STACK_SIZE=262144 \
  -s ENVIRONMENT=web \
  -o "$WEB/news.js"

echo ""
echo "Build complete!"
echo "  web/news.js   — Emscripten glue"
echo "  web/news.wasm — compiled C module"
echo ""
echo "To serve:"
echo "  python3 -m http.server 8080 --directory web"
echo "  open http://localhost:8080"
