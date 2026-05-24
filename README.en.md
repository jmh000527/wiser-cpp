# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## Commercial-Grade C++20 Full-Text Search Engine

Wiser-CPP is a high-performance full-text search engine built with modern C++20, featuring BM25 ranking, boolean queries, fuzzy search, spell correction, synonym expansion, autocomplete, JWT authentication, API Key auth, RBAC role-based access control, and IP-based rate limiting. Inspired by "How to Develop a Search Engine" (H. Yamada, T. Suenaga), this is a complete modern rewrite with commercial-grade enhancements.

### ✨ Key Features

<table>
<tr>
<td width="50%">

**🔍 Search Capabilities**
- BM25 probabilistic ranking (switchable to TF-IDF)
- Boolean queries: `AND`, `OR`, `NOT`, `"phrase"`, parentheses
- Fuzzy search: edit distance tolerance (0-2)
- Phrase search: position-based exact phrase matching
- Title boosting (default 1.5x)
- Spell correction: `did_you_mean` suggestions
- Synonym expansion: configurable dictionary
- Autocomplete: real-time prefix-match suggestions
- Search snippets with keyword highlighting

</td>
<td width="50%">

**🔐 Security Features**
- JWT authentication (HS256, configurable expiry)
- API Key stateless auth (`wsk_` prefix keys)
- RBAC: Admin / Editor / Viewer roles
- IP-based token bucket rate limiting
- CORS cross-origin protection
- Input length validation & safe JSON parsing
- Password SHA-256 iterated hashing (10000 iterations + random salt)

</td>
</tr>
<tr>
<td>

**⚡ Performance**
- SQLite WAL mode: concurrent reads/writes
- LRU query cache (1024 entries, auto-invalidated)
- Read/write lock separation: `shared_lock` / `unique_lock`
- SQLite tuning: mmap 256MB, cache 16MB
- Golomb-coded inverted list compression
- Configurable batch buffer threshold

</td>
<td>

**📊 Data & Operations**
- Multi-format import: XML (Wikipedia), TSV, JSON/JSONL
- RESTful document CRUD API
- Online hot backup (SQLite Online Backup API)
- Async bulk import task queue
- Health probes (`/health`, `/ready`)
- JSON config + environment variable overrides
- Graceful shutdown with signal handling

</td>
</tr>
</table>

**🎨 Frontend**
- Liquid Glass UI with light/dark theme auto-detection
- Ocean tide (light) / Starfield (dark) animated Canvas backgrounds
- Real-time search suggestions dropdown
- Drag-and-drop file upload with progress tracking
- Login / Register / User menu / API Key generation
- Responsive layout with mobile support

### 📸 Screenshots

<p align="center">
  <img src="img/1.png" alt="Search Interface" width="800">
</p>
<p align="center">
  <img src="img/2.png" alt="Dark Theme" width="800">
</p>

---

## 📦 Installation Guide

### Prerequisites

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| CMake | 3.16+ | Build system |
| C++ Compiler | C++20 | MSVC 17+, Clang 20+, GCC 15+ |
| vcpkg | Latest | Package manager (recommended) |

### Step 1: Install vcpkg (if not installed)

```bash
# Windows (PowerShell)
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
cd D:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install

# Linux / macOS
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg
./bootstrap-vcpkg.sh
```

### Step 2: Install Dependencies

```bash
vcpkg install spdlog fmt sqlite3 nlohmann-json jwt-cpp openssl
```

> **Note**: `jwt-cpp` depends on `openssl`; vcpkg will install it automatically. If you don't need authentication, you only need `spdlog fmt sqlite3 nlohmann-json`.

### Step 3: Clone the Repository

```bash
git clone https://github.com/YourUsername/wiser-cpp.git
cd wiser-cpp
```

### Step 4: Build

**Windows (Recommended)**

```powershell
# Open "x64 Native Tools Command Prompt" or VS Developer PowerShell
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
      -DBUILD_TESTS=OFF
cmake --build cmake-build-release --config Release
```

**Linux / macOS**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DBUILD_TESTS=OFF
cmake --build build --config Release -j$(nproc)
```

**CMake Build Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_DEMO` | `ON` | Build demo programs |
| `BUILD_WEB_SERVER` | `ON` | Build web server |
| `BUILD_TESTS` | `ON` | Build unit tests (requires Google Test) |

**Build Outputs:**
- `bin/wiser` — CLI tool (indexing / searching)
- `bin/wiser_web` — Web server (HTTP API + frontend)
- `demo/bin/*` — Demo programs

### Install (Optional)
```bash
cmake --install build --config Release --prefix /usr/local
```

---

## 🚀 Quick Start

### CLI Mode

```bash
# Import data (auto-detects format: TSV / JSON / JSONL / XML)
./bin/wiser -x data/sample.tsv my.db
./bin/wiser -x data/articles.jsonl my.db

# Search
./bin/wiser -q "information retrieval" my.db

# Enable phrase search
./bin/wiser -q "search engine" -s my.db
```

### Web Server Mode

**Basic start:**
```bash
cd bin
./wiser_web

# Specify database
./wiser_web --db my.db

# Enable phrase search
./wiser_web --db my.db --phrase=on
```

**Start with config file (recommended):**

Create `config.json`:
```json
{
  "port": 8604,
  "db_path": "search.db",
  "log_level": "info",
  "auth_enabled": true,
  "jwt_secret": "your-secret-key-at-least-32-chars-long",
  "token_expiry_hours": 24,
  "allow_registration": true,
  "rate_limit_enabled": true,
  "rate_limit_max_tokens": 100,
  "rate_limit_refill_rate": 10
}
```

```bash
./wiser_web --config config.json
```

**Example startup output:**
```
  ╦ ╦╦╔═╗╔═╗╦═╗  Web Server
  ║║║║╚═╗║╣ ╠╦╝
  ╚╩╝╩╚═╝╚═╝╩╚═  v1.0

  Database   search.db (new)
  Documents  0
  Scoring    BM25
  Phrase     off
  Workers    16 threads
  Auth       enabled
  Rate Limit enabled

  ✓ Listening on http://localhost:8604
```

Open browser: **http://localhost:8604**

---

## 🔐 Authentication System

Authentication is **optional** and disabled by default. When enabled, it provides full user management and access control.

### Enable Authentication

In config file:
```json
{
  "auth_enabled": true,
  "jwt_secret": "your-secret-key-change-this-in-production",
  "token_expiry_hours": 24,
  "allow_registration": true
}
```

Or via environment variables:
```bash
export WISER_AUTH_ENABLED=true
export WISER_JWT_SECRET=your-secret-key
```

### User Registration

```bash
# Register as viewer (default role, read-only)
curl -X POST http://localhost:8604/api/auth/register \
     -H "Content-Type: application/json" \
     -d '{"username":"alice","password":"MyPassword123"}'
# Response: {"registered":true,"username":"alice"}

# Register as editor (can add/import documents)
curl -X POST http://localhost:8604/api/auth/register \
     -H "Content-Type: application/json" \
     -d '{"username":"bob","password":"BobPass456","role":"editor"}'
```

> **Note**: Username 3-64 chars, password 6-128 chars. Admin role cannot be created via public registration.

### User Login

```bash
curl -X POST http://localhost:8604/api/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"bob","password":"BobPass456"}'
```

Response:
```json
{
  "token": "eyJhbGciOiJIUzI1NiJ9.eyJleHAi...",
  "expires_in_hours": 24
}
```

### Using JWT Token

```bash
TOKEN="eyJhbGciOiJIUzI1NiJ9..."

# Get current user info
curl http://localhost:8604/api/auth/me \
     -H "Authorization: Bearer $TOKEN"
# Response: {"username":"bob","role":"editor"}

# Add document (requires editor role)
curl -X POST http://localhost:8604/api/documents \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"title":"My Document","body":"Document content here"}'
```

### API Key Generation

API Keys are ideal for automation scripts and CI/CD pipelines:

```bash
# Generate API Key (requires login token first)
curl -X POST http://localhost:8604/api/auth/api-key \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" -d '{}'
# Response: {"api_key":"wsk_a5bf8833acb554f518e0eeb15637a528..."}

# Use API Key instead of Bearer Token
curl -X POST http://localhost:8604/api/documents \
     -H "X-API-Key: wsk_a5bf8833acb554f518e0eeb15637a528..." \
     -H "Content-Type: application/json" \
     -d '{"title":"Automated Import","body":"Created via API key"}'
```

### RBAC Roles

| Role | Permissions | Description |
|------|-------------|-------------|
| **Viewer** | Search, view documents, view stats | Default role |
| **Editor** | Viewer + add documents, bulk import | Content management |
| **Admin** | Editor + delete, flush index, rebuild, backup | System administration |

---

## ⚡ Rate Limiting

IP-based token bucket rate limiting protects the server from abuse.

```json
{
  "rate_limit_enabled": true,
  "rate_limit_max_tokens": 100,
  "rate_limit_refill_rate": 10
}
```

| Parameter | Description |
|-----------|-------------|
| `rate_limit_max_tokens` | Maximum bucket capacity (burst limit) |
| `rate_limit_refill_rate` | Tokens refilled per second (sustained QPS limit) |

Returns HTTP 429 when exceeded: `{"error": "Rate limit exceeded"}`

---

## 📡 REST API Reference

### Public Endpoints (No Auth Required)

#### Search

```bash
curl "http://localhost:8604/api/search?q=search+engine"
curl "http://localhost:8604/api/search?q=serch&fuzzy=1"
curl "http://localhost:8604/api/search?q=API&page=2&page_size=10"
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `q` | string | **required** | Query string (supports boolean syntax) |
| `scoring` | string | `bm25` | Ranking: `bm25` \| `tfidf` |
| `page` | int | `1` | Page number |
| `page_size` | int | `20` | Results per page (1-100) |
| `fuzzy` | int | `0` | Fuzzy search edit distance (0-2) |
| `snippet_len` | int | `120` | Snippet length |
| `phrase` | int | `0` | Phrase search toggle |

**Boolean query syntax:**
```
search AND engine              # Both terms required
search OR retrieval            # Either term matches
"search engine"                # Exact phrase
(search OR find) AND NOT web   # Combined query
```

#### Autocomplete
```bash
curl "http://localhost:8604/api/suggest?q=se&limit=10"
```

#### Get Document
```bash
curl http://localhost:8604/api/documents/1
```

#### Stats
```bash
curl http://localhost:8604/api/stats
```

#### Health Checks
```bash
curl http://localhost:8604/health      # {"status":"ok"}
curl http://localhost:8604/ready       # {"ready":true,"document_count":6}
```

### Protected Endpoints (Auth Required)

#### Add Document <sup>Editor+</sup>
```bash
curl -X POST http://localhost:8604/api/documents \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"title":"Title","body":"Content","author":"Author (optional)"}'
```

#### Delete Document <sup>Admin</sup>
```bash
curl -X DELETE http://localhost:8604/api/documents/1 \
     -H "Authorization: Bearer $ADMIN_TOKEN"
```

#### Bulk Import <sup>Editor+</sup>
```bash
curl -X POST http://localhost:8604/api/import \
     -H "Authorization: Bearer $TOKEN" \
     -F "file=@data/articles.jsonl"
```

#### Index Management <sup>Admin</sup>
```bash
curl -X POST http://localhost:8604/api/index/flush \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'
curl -X POST http://localhost:8604/api/index/rebuild \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'
```

#### Online Backup <sup>Admin</sup>
```bash
curl -X POST http://localhost:8604/api/admin/backup \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'
```

#### Task Management
```bash
curl http://localhost:8604/api/tasks
curl "http://localhost:8604/api/task?id=0000000000000001"
```

---

## ⚙️ Configuration Reference

Configuration priority: **Environment Variables > JSON Config File > Defaults**

### Full Config Example

```json
{
  "port": 8604,
  "db_path": "search.db",
  "log_level": "info",
  "token_len": 2,
  "compress_method": "none",
  "buffer_update_threshold": 2048,
  "max_index_count": -1,
  "enable_phrase_search": false,
  "scoring_method": "bm25",
  "bm25_k1": 1.2,
  "bm25_b": 0.75,
  "title_boost": 1.5,
  "auth_enabled": false,
  "jwt_secret": "change-this-to-a-long-random-string",
  "token_expiry_hours": 24,
  "allow_registration": true,
  "rate_limit_enabled": false,
  "rate_limit_max_tokens": 100,
  "rate_limit_refill_rate": 10
}
```

### Parameters

| Parameter | Env Variable | Default | Description |
|-----------|-------------|---------|-------------|
| `port` | `WISER_PORT` | `54321` | Listen port |
| `db_path` | `WISER_DB_PATH` | `wiser.db` | Database file path |
| `log_level` | `WISER_LOG_LEVEL` | `info` | `trace/debug/info/warn/error` |
| `token_len` | `WISER_TOKEN_LEN` | `2` | N-gram token length |
| `compress_method` | — | `none` | Compression: `none` \| `golomb` |
| `buffer_update_threshold` | — | `2048` | Buffer flush threshold |
| `max_index_count` | — | `-1` | Max indexed docs (-1=unlimited) |
| `enable_phrase_search` | — | `false` | Phrase search toggle |
| `scoring_method` | — | `bm25` | Ranking: `bm25` \| `tfidf` |
| `bm25_k1` | — | `1.2` | BM25 k1 (term frequency saturation) |
| `bm25_b` | — | `0.75` | BM25 b (document length normalization) |
| `title_boost` | `WISER_TITLE_BOOST` | `1.5` | Title match weight (1.0-10.0) |
| `auth_enabled` | `WISER_AUTH_ENABLED` | `false` | Enable JWT authentication |
| `jwt_secret` | `WISER_JWT_SECRET` | — | JWT signing key (required when auth enabled) |
| `token_expiry_hours` | — | `24` | JWT token validity (hours) |
| `allow_registration` | — | `true` | Allow user self-registration |
| `rate_limit_enabled` | `WISER_RATE_LIMIT` | `false` | Enable rate limiting |
| `rate_limit_max_tokens` | — | `100` | Token bucket capacity |
| `rate_limit_refill_rate` | — | `10` | Tokens refilled per second |

### Synonym Dictionary

Create `synonyms.txt` with comma-separated synonym groups:
```
# Comments start with #
computer,PC,laptop
search,find,lookup
fast,quick,rapid
```
Queries auto-expand: `search engine` → `(search OR find OR lookup) engine`

---

## 🏛️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Web Frontend                           │
│          (Liquid Glass UI / Auth UI / Canvas BG)            │
├─────────────────────────────────────────────────────────────┤
│                      HTTP Server                            │
│   (cpp-httplib + CORS + Rate Limiter + Auth Middleware)     │
├───────────┬───────────┬───────────┬───────────┬────────────┤
│  Search   │ Document  │  Import   │   Admin   │    Auth    │
│  Engine   │   CRUD    │  Queue    │ (backup)  │ (JWT/Key) │
├───────────┴───────────┴───────────┴───────────┴────────────┤
│                    WiserEnvironment                         │
│        (Buffer Manager + Index + Document Cache)           │
├────────────────────────────────────────────────────────────┤
│                    Search Engine                            │
│      (BM25 + Boolean + Fuzzy + Phrase + Synonyms)         │
├────────────────────────────────────────────────────────────┤
│                   Database Layer                            │
│         (SQLite WAL + Prepared Statements + LRU)           │
└────────────────────────────────────────────────────────────┘
```

Core Modules:
- **SearchEngine**: Query pipeline (tokenize → inverted index → boolean execution → BM25 scoring → title boost → LRU cache)
- **QueryParser**: Recursive descent boolean parser (AND/OR/NOT/phrase/grouping)
- **AuthManager**: JWT auth, API Key management, RBAC role permissions
- **RateLimiter**: IP-based token bucket rate limiting (thread-safe)
- **Database**: SQLite3 wrapper (WAL mode, prepared statements, online backup)
- **Tokenizer**: N-gram tokenizer (Unicode-safe)
- **ConfigLoader**: nlohmann/json config + environment variable loader

---

## 🔧 Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| SQLite3 | Data storage | vcpkg or system |
| spdlog + fmt | Logging | vcpkg or CMake FetchContent |
| nlohmann/json | JSON parsing | vcpkg |
| jwt-cpp | JWT tokens | vcpkg (depends on OpenSSL) |
| OpenSSL | Cryptography | vcpkg (jwt-cpp dependency) |
| cpp-httplib | HTTP server | Bundled (header-only) |

---

## 🙏 Acknowledgments

Thanks to "How to Develop a Search Engine" (H. Yamada, T. Suenaga) and the original wiser project. This project is a complete modern C++20 rewrite with BM25 ranking, boolean queries, fuzzy search, spell correction, synonym expansion, JWT authentication, RBAC access control, rate limiting, WAL mode, query caching, and more enterprise features.

---

## 📄 License

MIT License

