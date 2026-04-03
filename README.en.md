# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## Commercial-Grade C++ Full-Text Search Engine

Wiser-CPP is a high-performance full-text search engine built with modern C++20, featuring BM25 ranking, boolean queries, fuzzy search, spell correction, synonym expansion, autocomplete, and more. Inspired by the book "How to Develop a Search Engine" (H. Yamada, T. Suenaga), this project is a complete modern rewrite with enterprise-grade enhancements.

### Key Features

**Search Capabilities**
- BM25 probabilistic relevance ranking (switchable to TF-IDF)
- Boolean query syntax: `AND`, `OR`, `NOT`, quoted phrases `"exact"`, parentheses grouping
- Fuzzy search: edit-distance tolerance (configurable 0-2)
- Phrase search: position-chain exact phrase matching
- Title boost: automatic score boost for title matches (default 1.5x)
- Spell correction: `did_you_mean` suggestions (index dictionary + edit distance)
- Synonym expansion: configurable synonym dictionary with automatic query expansion
- Autocomplete: prefix-match real-time search suggestions
- Search result snippets with keyword highlighting

**Performance**
- SQLite WAL mode: concurrent read/write support
- LRU query cache: 1024-entry result cache with automatic invalidation on index changes
- Read-write lock separation: `shared_lock` for search, `unique_lock` for indexing
- SQLite tuning: `mmap_size=256MB`, `cache_size=16MB`, `synchronous=NORMAL`
- Golomb-coded postings compression
- Tunable batch buffer threshold

**Data Management**
- Multi-format import: XML (Wikipedia), TSV, JSON (JSONL/NDJSON/array)
- RESTful document CRUD API
- Online hot backup (SQLite Online Backup API)
- Async bulk import task queue

**Operations**
- Health probes: `/health` (liveness), `/ready` (readiness)
- Index statistics API
- JSON config file + environment variable overrides
- Graceful shutdown with signal handling

**Web Frontend**
- Liquid Glass UI with light/dark theme support
- Ocean tide (light) / Starfield (dark) animated backgrounds
- Real-time search suggestion dropdown
- Drag-and-drop file upload with import progress tracking
- Responsive layout

### Screenshots
<p align="center">
  <img src="img/1.png" alt="Screenshot 1" width="800">
</p>
<p align="center">
  <img src="img/2.png" alt="Screenshot 2" width="800">
</p>

### Project Layout
```
wiser-cpp/
├── include/wiser/         # Headers (config.h, search_engine.h, database.h, ...)
├── src/                   # Core sources (search_engine.cpp, database.cpp, ...)
│   └── web/               # Web server (routes.cpp, web_server.cpp, ...)
├── demo/                  # Demo programs (binaries in demo/bin)
├── web/                   # Static frontend assets (index.html, script.js, styles.css)
├── tests/                 # Unit tests
├── bin/ lib/              # Build output directories
├── CMakeLists.txt
└── README.md
```

### Dependencies
- CMake >= 3.16
- C++20 compiler (MSVC 17+ / Clang 20+ / GCC 15+)
- SQLite3
- spdlog, fmt
- cpp-httplib (header-only, bundled)

Notes:
- If spdlog/fmt are not available, CMake falls back to FetchContent.
- SQLite3 is discovered via vcpkg (`unofficial::sqlite3`) first, then system (`SQLite::SQLite3`). Manual `SQLITE3_INCLUDE_DIR`/`SQLITE3_LIBRARY` also supported.
- On Windows, dependent DLLs are automatically copied next to binaries after build.

### Build

```bash
# Linux / macOS
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j

# Windows
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j

# Windows + vcpkg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release -j
```

CMake options:
| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_DEMO` | `ON` | Build demo programs |
| `BUILD_WEB_SERVER` | `ON` | Build web server |
| `BUILD_TESTS` | `ON` | Build unit tests (requires Google Test) |

Outputs:
- `bin/wiser` — CLI tool (index / search)
- `bin/wiser_web` — Web server (HTTP API + frontend)
- `demo/bin/*` — Demo binaries (not installed)

### Install (optional)
```bash
cmake --install build --config Release --prefix install
```
Installs to:
- `<prefix>/bin`: wiser, wiser_web (and runtime DLLs on Windows)
- `<prefix>/lib`: wiser_core static library
- `<prefix>/web`: Frontend static assets

### Quick Start

#### CLI Mode
```bash
# 1) Import data (auto-selects loader by extension)
./bin/wiser -x data/sample.tsv my.db
./bin/wiser -x data/sample.jsonl my.db

# 2) Search
./bin/wiser -q "information retrieval" my.db

# 3) Enable phrase search
./bin/wiser -q "search engine" -s my.db
```

#### Web Server Mode
```bash
# Start with default database
./bin/wiser_web

# Specify database path
./bin/wiser_web my.db

# Enable phrase search
./bin/wiser_web my.db --phrase=on
```
Open http://localhost:54321 in a browser.

### REST API Reference

#### Search

```http
GET /api/search?q=keyword&scoring=bm25&page=1&page_size=20&fuzzy=1&snippet_len=120&phrase=0
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `q` | string | required | Query string (supports boolean syntax) |
| `scoring` | string | `bm25` | Ranking algorithm: `bm25` \| `tfidf` |
| `page` | int | `1` | Page number |
| `page_size` | int | `20` | Results per page |
| `fuzzy` | int | `0` | Fuzzy search edit distance (0-2) |
| `snippet_len` | int | `120` | Snippet length |
| `phrase` | int | `0` | Phrase search toggle |

Response example:
```json
{
  "query": "search engine",
  "total_hits": 42,
  "page": 1,
  "page_size": 20,
  "took_ms": 3.5,
  "did_you_mean": "",
  "results": [
    {
      "doc_id": 5,
      "title": "Search Engine Basics",
      "score": 8.72,
      "snippet": "...how a <em>search</em> <em>engine</em> works..."
    }
  ]
}
```

**Boolean query syntax examples:**
```
search AND engine          # Both terms must appear
search OR retrieval        # Either term matches
"search engine"            # Exact phrase
(search OR find) AND NOT web   # Combined query
```

#### Autocomplete
```http
GET /api/suggest?q=sea&limit=10
```

#### Document Management
```http
GET    /api/documents/{id}       # Get document by ID
POST   /api/documents            # Add document (JSON body)
DELETE /api/documents/{id}       # Delete document
POST   /api/import               # Bulk file import (multipart/form-data)
```

Add document request body:
```json
{
  "title": "Document Title",
  "body": "Document body content"
}
```

#### Index Management
```http
POST /api/index/flush            # Flush index buffer to disk
GET  /api/stats                  # Index statistics
```

#### Operations
```http
POST /api/admin/backup           # Online hot backup
GET  /api/tasks                  # List all tasks
GET  /api/task?id=<task_id>      # Get task status
GET  /health                     # Liveness probe
GET  /ready                      # Readiness probe (checks DB)
```

Bulk import example:
```bash
curl -F "file=@data/sample.tsv" \
     -F "file=@data/articles.jsonl" \
     http://localhost:54321/api/import
```

### Configuration

Wiser supports a three-tier configuration priority: **Environment variables > JSON config file > Defaults**

#### Configuration Parameters

| Parameter | Env Variable | Default | Description |
|-----------|-------------|---------|-------------|
| `db_path` | `WISER_DB_PATH` | required | Database file path |
| `token_len` | `WISER_TOKEN_LEN` | `2` | N-gram token length |
| `compress_method` | — | `none` | Postings compression: `none` \| `golomb` |
| `buffer_update_threshold` | — | `2048` | Buffer flush threshold |
| `max_index_count` | — | `-1` | Max indexed documents (-1=unlimited) |
| `enable_phrase_search` | — | `false` | Phrase search toggle |
| `scoring_method` | — | `bm25` | Ranking algorithm: `bm25` \| `tfidf` |
| `bm25_k1` | — | `1.2` | BM25 k1 parameter (term saturation) |
| `bm25_b` | — | `0.75` | BM25 b parameter (length normalization) |
| `title_boost` | `WISER_TITLE_BOOST` | `1.5` | Title match weight (1.0-10.0) |

#### Synonym Dictionary

Create a `synonyms.txt` file with comma-separated synonym groups per line:
```
# Comment line
computer,PC,laptop
search,find,lookup
fast,quick,rapid
```
Queries are automatically expanded: `fast car` → `(fast OR quick OR rapid) car`

### Architecture Overview

```
┌────────────────────────────────────────────────────┐
│                   Web Frontend                     │
│           (Liquid Glass UI / script.js)            │
├────────────────────────────────────────────────────┤
│                  HTTP Server                       │
│      (cpp-httplib + shared_mutex concurrency)      │
├──────────┬──────────┬──────────┬──────────────────┤
│ Search   │ Document │ Import   │ Admin            │
│ Engine   │ CRUD     │ Queue    │ (backup/health)  │
├──────────┴──────────┴──────────┴──────────────────┤
│              WiserEnvironment                      │
│    (Config / Synonym Dict / Index Coordination)    │
├──────────┬──────────┬──────────┬──────────────────┤
│Tokenizer │ Postings │ Query    │ Synonym          │
│(N-gram)  │(Inverted)│ Parser   │ Dict             │
├──────────┴──────────┴──────────┴──────────────────┤
│              Database (SQLite3 + WAL)              │
│    (mmap 256MB / cache 16MB / synchronous=NORMAL)  │
└────────────────────────────────────────────────────┘
```

Core modules:
- **SearchEngine**: Query pipeline (tokenize → postings lookup → boolean execution → BM25 scoring → title boost → LRU cache)
- **QueryParser**: Recursive descent boolean query parser (AND / OR / NOT / quoted phrases / parentheses)
- **WiserEnvironment**: Unified config management (immediate persistence, synonym dictionary, index buffer coordination)
- **Database**: SQLite3 wrapper (WAL mode, prepared statements, online backup)
- **Tokenizer**: N-gram tokenizer (Unicode-safe)
- **SynonymDict**: Synonym dictionary (CSV loading, query expansion)
- **Postings / InvertedIndex**: Inverted index structures (Golomb-coded compression)
- **Loaders**: WikiLoader / TsvLoader / JsonLoader
- **ConfigLoader**: JSON config file + environment variable loader

### Command-Line Options

#### wiser (CLI tool)
```
usage: wiser [options] db_file

indexing : -x <data_file> [-m N] [-t N] [-c none|golomb]
search   : -q <query> [-s]
```

| Option | Description |
|--------|-------------|
| `db_file` | SQLite database path (created if missing) |
| `-x <file>` | Import file (`.xml` / `.tsv` / `.json` / `.jsonl` / `.ndjson`) |
| `-q <query>` | Execute search (combinable with `-x`: import then search) |
| `-c <method>` | Compression: `none` (default) \| `golomb` |
| `-m <N>` | Max documents to import (`-1` = unlimited) |
| `-t <N>` | Buffer merge threshold (default 2048) |
| `-s` | Enable phrase search |
| `-h` | Show help |

#### wiser_web (web server)
| Option | Description |
|--------|-------------|
| `db_file` | Database path (optional, default `./wiser_web.db`) |
| `--phrase=on\|off` | Phrase search toggle (applied immediately and persisted) |
| `-h` | Show help |

The server listens on `0.0.0.0:54321` and serves static assets from `../web`.

### Acknowledgments

Thanks to the authors of "How to Develop a Search Engine" (H. Yamada, T. Suenaga) and the original wiser project. This repository builds upon their ideas with a complete modern C++ rewrite, adding BM25 ranking, boolean queries, fuzzy search, spell correction, synonym expansion, WAL mode, query caching, and other enterprise-grade features.