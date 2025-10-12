# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## Wiser-CPP: Modern C++ Full-Text Search Engine

This project is a modern C++ rewrite of the wiser full-text search engine, showcasing RAII, smart pointers, and
STL-based design practices. It is derived from the wiser project in the book “How to Develop a Search Engine” (
authors: Hiroyuki Yamada and Tadashi Suenaga); the original implementation was in C. Additionally, building on that
foundation, this project adds a small number of features.

### Features
- Modern C++20, RAII, smart pointers
- N-gram full-text search with inverted index (configurable N, default N=2)
- SQLite3 persistent storage
- Data loading: auto-select loader by file extension (XML/TSV/JSON)
- Phrase search (adjacent position-chain) with a switch
- Postings compression: golomb/none
- Tunable buffer threshold for batched merges
- Modular components and CMake build

### Project Structure
```
wiser-cpp/
├── include/wiser/           # Headers
├── src/                     # Sources
├── build/                   # Build output (generated)
├── demo/                    # Demo sources and outputs (binaries to demo/bin)
├── CMakeLists.txt           # CMake config
└── README.md                # This file
```

### Dependencies
- C++20 compiler (recommended: GCC 14.2+)
- CMake 3.16+
- SQLite3 dev libraries

How to install dependencies:
- Ubuntu/Debian:
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake pkg-config libsqlite3-dev
  ```
- CentOS/RHEL (yum) or Fedora (dnf):
  ```bash
  # CentOS/RHEL
  sudo yum install -y gcc-c++ cmake sqlite-devel
  # Fedora
  sudo dnf install -y gcc-c++ cmake sqlite-devel
  ```
- macOS (Homebrew):
  ```bash
  brew update
  brew install cmake sqlite
  ```
  
### Build

Configure & build (generic):
```bash
mkdir -p build && cd build
cmake ..
cmake --build .
# Main binary wiser at build/bin/
# Demo binaries wiser_demo and loader_demo at source demo/bin/
```

Script build (Unix/macOS):
```bash
bash build.sh
```

Windows: use the generic CMake steps above (recommended in "x64 Native Tools" or VS Developer Command Prompt).

### Install (optional)
You can install artifacts using CMake install rules (also used by CPack). The rules copy:
- wiser executable -> <prefix>/bin
- wiser_core static library -> <prefix>/lib
- public headers -> <prefix>/include

Example: install into the ../install directory
```bash
# Run installation from the build directory
cmake --install . --prefix ../install
# After install, check install/bin, install/lib, install/include
```
Notes:
- Demo executables are not installed by default; they are built into source demo/bin.
- If --prefix is omitted, the default CMAKE_INSTALL_PREFIX is used (e.g., /usr/local on Unix or Program Files on Windows).

### Quickstart
```bash
# 1) Create a database (auto-select loader by extension)
./wiser -x enwiki-latest-pages-articles.xml data/wiser.db   # Wikipedia XML
./wiser -x sample_dataset.tsv data/wiser.db                 # TSV: title[TAB]body
./wiser -x sample.jsonl data/wiser.db                       # JSON Lines
./wiser -x sample_array.json data/wiser.db                  # JSON array

# 2) Search
./wiser -q "information retrieval" data/wiser.db
```
> Tip: after import the index buffer is flushed automatically, so you can search right away.

### Usage
```
usage: wiser [options] db_file

modes:
  Indexing : -x <data_file> [-m N] [-t N] [-c METHOD]
             data_file supports: .xml (Wikipedia XML), .tsv, .json, .jsonl, .ndjson
  Searching: -q <query> [-s]
  You can provide both -x and -q to index then search in one run.

options:
  -h, --help                   : show this help and exit
  -c <compress_method>         : postings list compression [default: golomb]
                                 values: none | golomb
  -x <data_file>               : path to data file; loader is chosen by extension
                                 .xml -> Wikipedia XML, .tsv -> TSV (title[TAB]body), .json/.jsonl/.ndjson -> JSON
  -q <search_query>            : query string (UTF-8) for search
  -m <max_index_count>         : max docs to index [-1 = no limit, default: -1]
  -t <buffer_threshold>        : inverted index buffer merge threshold [default: 2048]
  -s                           : disable phrase search (by default it's enabled)
```

#### Implemented features and CLI flags
- -x <data_file>: auto-select loader by extension and index (.xml/.tsv/.json/.jsonl/.ndjson).
- -m <N>: limit the number of documents to index; upon reaching the limit, the buffer is flushed and the loader stops.
- -c <none|golomb>: set postings compression; invalid values fallback to golomb; the effective value is printed.
- -t <N>: control the inverted index buffer merge threshold; smaller values flush more frequently (lower peak memory, slower indexing).
- -q <query>: run a search; scoring uses TF (log-scaled) × IDF (smoothed). Top results print Score.
- -s: disable phrase search. By default, multi-term queries require adjacent n-grams.

#### Examples
```bash
# 1) Index (create database) - Wikipedia XML
./wiser -x enwiki-latest-pages-articles.xml -m 10000 -c golomb data/wiser.db

# 2) Index (create database) - TSV
./wiser -x sample_dataset.tsv data/wiser.db

# 3) Index (create database) - JSON Lines / JSON array
./wiser -x sample.jsonl data/wiser.db
./wiser -x sample_array.json data/wiser.db

# 4) Search in an existing database (phrase enabled by default)
./wiser -q "information retrieval" data/wiser.db

# 5) Disable phrase search
./wiser -q "information retrieval" -s data/wiser.db

# 6) Tune buffer threshold while indexing
./wiser -x wiki.xml -t 1000 data/wiser.db
```

---

## Data loading (besides Wiki)

Beyond the CLI `-x` importer, this project provides local file loaders for TSV and JSON.

### 1) TSV loader (TsvLoader)
- Format: each line as `title[TAB]body`, optional header line;
- Usage:
  ```cpp
  wiser::WiserEnvironment env;
  env.initialize("data.db");
  wiser::TsvLoader tsv(&env);
  tsv.loadFromFile("dataset.tsv", /*has_header=*/true);
  env.flushIndexBuffer(); // ensure flush when importing a small set
  ```

### 2) JSON loader (JsonLoader)
- Two formats supported:
  - JSON Lines (NDJSON): one object per line `{ "title":"...", "body":"..." }`;
  - JSON array: `[{ "title":"...", "body":"..." }, ...]`;
- Auto-detect: the first non-space char `'['` indicates an array, otherwise JSON Lines;
- Field requirement: each object must have string fields `title` and `body`;
- Usage:
  ```cpp
  wiser::WiserEnvironment env;
  env.initialize("data.db");
  wiser::JsonLoader jl(&env);
  jl.loadFromFile("data.jsonl");       // JSON Lines
  jl.loadFromFile("data_array.json");  // JSON array
  env.flushIndexBuffer();
  ```

> Note: JsonLoader is a lightweight parser for flat objects; it doesn't expand `\uXXXX`. Prefer UTF-8 source files with direct Unicode.

### 3) Quickstart (loader_demo)
A demo program `loader_demo` shows how to import TSV and JSON and then search:
```bash
# Build
mkdir -p build && cd build
cmake .. && cmake --build .

# Run
cd ..
./demo/bin/loader_demo            # Linux/macOS
# or
./demo/bin/wiser_demo             # the other demo
# On Windows, run demo/bin/*.exe from a terminal or File Explorer
```
The demo imports from `sample_dataset.tsv`, `sample.jsonl`, and `sample_array.json`, then runs simple queries and prints body previews (UTF-8 safe wrapping and truncation).

### Data format requirements
- General
  - Encoding: UTF-8 (BOM-less preferred).
  - Fields: each record must provide non-empty `title` and `body`.
  - Uniqueness: `title` is the unique key (there is a unique index on `title`). A duplicate title updates the document body.

- TSV (tab-separated)
  - One record per line: `title[TAB]body`. Only the “first” TAB splits the two fields; remaining TABs are preserved in `body`.
  - Single-line record: multiline bodies are not supported in TSV; a newline starts a new record.
  - Header: with CLI `-x <file.tsv>` the first line is treated as a header and skipped by default. If your TSV has no header, call `TsvLoader::loadFromFile(path, /*has_header=*/false)` in code or use a custom import flow.
  - Example:
    ```tsv
    title	body
    Quick Sort	Quicksort is a divide-and-conquer sorting algorithm with average time complexity O(n log n).
    Inverted Index	An inverted index records which documents contain a term and the positions.
    IR	Information retrieval studies how to efficiently find relevant info in large unstructured data.
    ```

- JSON Lines (NDJSON)
  - Each line is a JSON object with string fields `"title"` and `"body"`.
  - Parsing: supports common escapes (\n/\t/\\/\"), does not expand `\uXXXX`. Prefer UTF-8 files with direct Unicode.
  - Example:
    ```json
    {"title":"JSON example 1","body":"This is the first JSON Lines document."}
    {"title":"JSON example 2","body":"The second document, containing the keyword: information retrieval."}
    ```

- JSON array
  - Top-level array; each element is an object with string fields `"title"` and `"body"`.
  - Example:
    ```json
    [
      {"title": "Array example 1", "body": "This is the first array document."},
      {"title": "Array example 2", "body": "The second array document about inverted index."}
    ]
    ```

### Notes
- Flush the index buffer: for a small batch, call `env.flushIndexBuffer()` (or `env.addDocument("", "")`) before searching, otherwise you may observe empty results.
- Console encoding: on Windows use a UTF-8 terminal (Windows Terminal/PowerShell 7 recommended). The project tries to set console UTF-8 during initialization.
- Field discipline: TSV must be `title[TAB]body`; JSON objects must have `title`/`body`. Empty title/body lines are skipped.

### Architecture (overview)
- WiserEnvironment: environment and configuration
- Database: SQLite3 wrapper
- Tokenizer: N-gram tokenization (only index n-grams with length ≥ N; positions are consecutive)
- SearchEngine: query processing, phrase matching (position chain), TF-IDF ranking
- Postings/InvertedIndex: index structures
- WikiLoader: Wikipedia XML processing with progress
- Utils: conversions and helpers

### Acknowledgments

I would like to express my sincere gratitude to Hiroyuki Yamada and Tadashi Suenaga, authors of “How to Develop a Search
Engineer,” and to the original wiser project. Their clear structure and robust implementation provided the foundation
and inspiration for this repository, which rewrites wiser in modern C++.