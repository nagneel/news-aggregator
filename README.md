# News Aggregator

A minimalistic news aggregator that searches 92+ sources, then clusters and ranks results using a C/WebAssembly module — all client-side, no backend required.

## How it works

1. Enter a search query
2. The app fetches articles in parallel from **Hacker News** (Algolia API), **Google News** (RSS), and **~90 categorized RSS feeds** across tech, business, science, world news, and more
3. Articles are deduplicated and passed to a **C/WASM module** that tokenizes text, clusters articles by Jaccard similarity, and relevance-ranks them
4. Results render as collapsible cluster cards with source pills, favicons, and relative timestamps
5. Filter by time range, category, or sort by relevance, recency, or coverage — all instant, client-side

## Prerequisites

- [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (`brew install emscripten` or via emsdk)
- Python 3 (for the local HTTP server)

## Build & run

```bash
./build.sh
python3 -m http.server 8080 --directory web
# open http://localhost:8080
```

`build.sh` downloads [cJSON](https://github.com/DaveGamble/cJSON) and compiles the C source to WebAssembly. WASM requires an HTTP server — `file://` URLs won't work.

## Project structure

```
src/
  cluster.c/h   — tokenization, Jaccard similarity, greedy clustering
  ranker.c/h    — relevance scoring (title + description), cluster sorting
  api.c         — WASM entry point: process_articles(json, query) → json
web/
  index.html    — all UI and app logic (single file)
build.sh        — downloads cJSON, compiles C → news.js + news.wasm
```

## License

MIT
