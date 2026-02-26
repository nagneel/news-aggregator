# CLAUDE.md — News Aggregator

## What this project is

A minimalistic news aggregator webpage. The user enters a search query; the page fetches articles from ~92 sources (HN Algolia + Google News RSS + ~90 categorized RSS feeds), passes them through a C/WebAssembly module that tokenizes, clusters, and relevance-ranks them, then renders the results as collapsible cluster cards with filters and sort controls.

## Build & run

```bash
# Requires Emscripten (brew install emscripten or via emsdk)
./build.sh
# → downloads vendor/cjson/{cJSON.h,cJSON.c} then compiles → web/news.js + web/news.wasm

python3 -m http.server 8080 --directory web
# open http://localhost:8080
```

WASM requires an HTTP server — opening `web/index.html` as a `file://` URL will not work.

## Project structure

```
news-aggregator/
├── src/
│   ├── cluster.h / cluster.c   # tokenization, Jaccard similarity, greedy clustering
│   ├── ranker.h / ranker.c     # relevance scoring (title×0.7 + desc×0.3), cluster sort
│   └── api.c                   # WASM entry point: process_articles(json, query) → json
├── vendor/cjson/               # downloaded by build.sh at compile time
│   ├── cJSON.h
│   └── cJSON.c
├── web/
│   ├── index.html              # all UI + app logic (inline <script>)
│   ├── news.js                 # Emscripten glue (generated — do not edit)
│   └── news.wasm               # compiled C module (generated — do not edit)
└── build.sh                    # curl cJSON + emcc invocation
```

## Architecture

```
Browser
  ├── fetchHN(query)          HN Algolia search + search_by_date (both, deduped)
  ├── fetchGoogleNews(query)  news.google.com/rss/search via allorigins.win proxy
  ├── fetchRSS × 90           CATEGORIZED_FEEDS via allorigins.win, 8 s timeout each
  │
  ├── dedup by URL/title
  ├── runWASM(articles, query)
  │     └── C: parse JSON → score_articles → cluster_articles → rank_clusters → JSON
  │
  ├── applyFiltersAndSort(rawClusters)   JS-side, instant (no re-fetch)
  └── renderClusters(display)
```

The WASM function signature (called via `ccall`):
```c
EMSCRIPTEN_KEEPALIVE
const char *process_articles(const char *articles_json, const char *query);
```
Returns a pointer to a static internal buffer — JS must copy immediately via `UTF8ToString`.

## C module details

### cluster.c
- `tokenize(text, words, max)` — lowercase, strip punct, filter stopwords + ≤2-char words. Uses `strtok_r`. Arrays declared `static` in callers to avoid large stack frames in WASM.
- `headline_similarity(a, b)` — Jaccard on word sets. Deduplicates both word arrays via `dedup_words()` before computing intersection so duplicate words in a title don't inflate the score. Uses `static` local arrays (avoids 16 KB stack per call).
- `cluster_articles(list)` — greedy O(n·k): for each article find the cluster whose representative has Jaccard similarity > `CLUSTER_THRESHOLD` (0.25); if none, create new cluster. Returns heap-allocated `ClusterList*`.

### ranker.c
- `compute_relevance(title, desc, query)` — tokenize all three, count query-term hits in title (weight 0.7) and description (weight 0.3). Returns [0.0, 1.0]. Uses `static` local arrays (avoids 24 KB stack per call — important for WASM).
- `ranker_set_article_list(list)` — sets file-static `g_list` used by the `qsort` comparator (stdlib comparators cannot take extra parameters).
- `rank_clusters(cl, query)` — sorts articles within each cluster by relevance, computes `avg_relevance`, updates representative to the top article, then sorts clusters by `avg_relevance` desc then `count` desc.

### api.c
- Static `result_buf` / `result_cap` — reused across calls; grows via `realloc`, never shrinks. JS copies the string immediately so the single-buffer approach is safe in the single-threaded WASM context.
- `serialize_clusters` bounds-checks `article_indices[j]` against `articles->count` before dereferencing.
- `ArticleList` is `static` (~2.2 MB in BSS, not stack). `ClusterList` is heap-allocated (~900 KB per call, freed after serialization).

## JavaScript (web/index.html)

All app logic is inline in `<script>`. Key globals:

| Symbol | Purpose |
|---|---|
| `CATEGORIZED_FEEDS` | Array of `{name, url, cat, domain}` — 90 RSS feeds across 7 categories |
| `SOURCE_CAT` | `name → category` lookup (built from CATEGORIZED_FEEDS + HN/Google News) |
| `CAT_COLORS` | Category → `{bg, fg, border}` for source pills |
| `SOURCE_DOMAINS` | `name → domain URL` for favicons (Google s2 service) |
| `rawClusters` | Stored after WASM call; filters re-use without re-fetching |
| `filterState` | `{time, cat, sort}` — current filter selections |
| `wasmReady` / `wasmModule` | WASM bootstrap state |

### WASM bootstrap (race-condition safe)
```js
if (typeof Module !== 'undefined' && Module.calledRun) {
  wasmReady = true; wasmModule = Module;   // already initialized
}
if (typeof Module !== 'undefined') {
  Module.onRuntimeInitialized = () => { wasmReady = true; wasmModule = Module; };
}
```

### Fetch flow
1. `fetchHN(query)` — fires `search` + `search_by_date` in parallel, deduplicates by `objectID`
2. `fetchGoogleNews(query)` — Google News RSS via allorigins.win proxy, `parseRSS`
3. `fetchRSS(feed)` — uses `fetchWithTimeout(8000)` (AbortController); each feed fails gracefully
4. All 92 fetches run with `Promise.all`; progress bar shows `X/92 sources · N articles`
5. Articles deduplicated by URL then passed to WASM

### Filter & sort (client-side, instant)
`applyFiltersAndSort(clusters)` applies in order:
1. **Date filter** — drop articles older than N days (undated articles pass through)
2. **Category filter** — keep articles whose `SOURCE_CAT[source]` matches
3. Drop clusters that become empty
4. **Zero-relevance pruning** — in relevance-sort mode, drop zero-relevance clusters when at least one cluster has a match
5. **Sort** — `relevance` (avg_relevance desc), `newest` (newest article date desc), `coverage` (count desc)

### CORS proxy
All RSS feeds use `https://api.allorigins.win/get?url=<encoded>` which returns `{contents: "<xml>"}`.
HN Algolia and Google News RSS are direct fetches (CORS-enabled).

## Known design constraints

- **Single CORS proxy** — allorigins.win may rate-limit under heavy concurrent load. If feeds start returning empty, consider rotating proxies.
- **Memory** — `ArticleList` (~2.2 MB BSS) + `ClusterList` (~900 KB heap) per call. `-s ALLOW_MEMORY_GROWTH=1` handles this.
- **Stack** — set to 256 KB (`-s STACK_SIZE=262144`) to safely accommodate nested C calls.
- **Thread safety** — not applicable; Emscripten runs single-threaded. The `static` arrays in `compute_relevance` and `headline_similarity` are safe.
- **result_buf** — returned pointer is valid only until the next `process_articles` call. JS copies it immediately via `UTF8ToString`.

## What's been built (session history)

| Phase | What was done |
|---|---|
| Initial build | C module (cluster/ranker/api), Emscripten build.sh, minimal HTML+JS |
| Search fix | Added Google News RSS (query-specific), HN `search_by_date`, zero-relevance cluster filter |
| C expert review | Fixed Jaccard set semantics (`dedup_words`), made large arrays `static`, fixed `cmp_clusters` comparator, added bounds check in `serialize_clusters`, raised WASM stack to 256 KB |
| Frontend agent | WASM race fix, relative timestamps, source color pills + favicons, skeleton loader, per-source progress chips (redesigned for 100 sources), description previews, accessibility |
| Filters + sources | 90 RSS feeds in 7 categories, filter bar (Time/Sort/Category), `applyFiltersAndSort`, `fetchWithTimeout`, count-based progress bar |

## Do not edit

- `web/news.js` and `web/news.wasm` — generated by Emscripten, recreated by `./build.sh`
- `vendor/cjson/` — downloaded by `build.sh`

## Rebuild after any C change

```bash
./build.sh
```
