# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## Wiser-CPP：现代 C++ 全文检索引擎

本项目是 wiser 全文检索引擎的现代 C++ 重写版，展示了 RAII、智能指针与基于 STL 的设计实践。来自《How to Develop a Search
Engine》（作者：山田浩之、末永匡）一书的 wiser 项目，原版使用 C 语言实现。同时，本项目在此基础上添加了少量特性。

### 功能特性
- 现代 C++20，RAII 与智能指针
- 基于 N-gram 的倒排索引全文检索（N 可配置，默认 N=2）
- SQLite3 持久化存储
- 数据加载多样：按后缀自动选择 XML/TSV/JSON 加载器
- 短语检索（相邻位置链）开关
- 倒排列表压缩：golomb/none 可切换
- 缓冲区阈值可配置的增量合并
- 模块化组件与 CMake 构建

### 目录结构
```
wiser-cpp/
├── include/wiser/           # 头文件
├── src/                     # 源码
├── build/                   # 构建产物（生成）
├── demo/                    # 演示代码与产物（可执行输出到 demo/bin）
├── CMakeLists.txt           # CMake 配置
└── README.md                # 说明文档
```

### 依赖
- C++20 编译器（推荐：GCC 14.2+）
- CMake 3.16+
- SQLite3 开发库

依赖安装方法：
- Ubuntu/Debian：
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake pkg-config libsqlite3-dev
  ```
- CentOS/RHEL（yum）或 Fedora（dnf）：
  ```bash
  # CentOS/RHEL
  sudo yum install -y gcc-c++ cmake sqlite-devel
  # Fedora
  sudo dnf install -y gcc-c++ cmake sqlite-devel
  ```
- macOS（Homebrew）：
  ```bash
  brew update
  brew install cmake sqlite
  ```

### 构建

通用构建流程：
```bash
mkdir -p build && cd build
cmake ..
cmake --build .
# 主程序 wiser 位于 build/bin/
# 演示程序 wiser_demo、loader_demo 位于 源码目录 demo/bin/
```

脚本构建（Unix/macOS）：
```bash
bash build.sh
```

Windows：使用上面的通用 CMake 步骤（建议在 x64 Native Tools 命令行或 VS 开发者命令提示符中执行）。

### 安装（可选）
支持使用 CMake 安装（或配合 CPack 打包）。安装规则会把以下目标复制到安装前缀：
- wiser 可执行文件 -> <prefix>/bin
- wiser_core 静态库 -> <prefix>/lib
- 公共头文件 -> <prefix>/include

示例：安装到上级目录 install/
```bash
# 在构建目录执行安装
cmake --install . --prefix ../install
# 完成后可在 install/bin、install/lib、install/include 中看到安装产物
```

说明：
- 未对 demo 程序定义安装规则；demo 可执行文件始终编译输出到源码目录 demo/bin。
- 若不指定 --prefix，则使用 CMake 配置时的 CMAKE_INSTALL_PREFIX（系统默认前缀，如 /usr/local 或 Windows 的 Program Files）。

### 快速开始
```bash
# 1) 生成数据库（按后缀自动选择加载器）
./wiser -x enwiki-latest-pages-articles.xml data/wiser.db   # Wikipedia XML
./wiser -x sample_dataset.tsv data/wiser.db                 # TSV: title[TAB]body
./wiser -x sample.jsonl data/wiser.db                       # JSON Lines
./wiser -x sample_array.json data/wiser.db                  # JSON 数组

# 2) 执行检索
./wiser -q "information retrieval" data/wiser.db
```
> 提示：少量文档导入后已自动刷新缓冲（落库），可直接检索。

### 使用说明
```
usage: wiser [options] db_file

modes:
  Indexing : -x <data_file> [-m N] [-t N] [-c METHOD]
             data_file 支持：.xml（Wikipedia XML）、.tsv、.json、.jsonl、.ndjson
  Searching: -q <query> [-s]
  可同时提供 -x 与 -q，在一次运行中先建索引再搜索。

options:
  -h, --help                   : 显示帮助并退出
  -c <compress_method>         : 倒排列表压缩方式 [默认: golomb]
                                 可选值: none | golomb
  -x <data_file>               : 数据文件路径；根据后缀选择加载器
                                 .xml -> Wikipedia XML, .tsv -> TSV (title[TAB]body), .json/.jsonl/.ndjson -> JSON
  -q <search_query>            : 搜索查询（UTF-8）
  -m <max_index_count>         : 最大索引文档数 [-1 表示不限，默认 -1]
  -t <buffer_threshold>        : 倒排缓冲合并阈值 [默认 2048]
  -s                           : 关闭短语搜索（默认开启）
```

#### 已实现特性与参数映射
- -x <data_file>：按后缀自动选择加载器并索引（.xml/.tsv/.json/.jsonl/.ndjson）。
- -m <N>：限制本次索引的最大文档数；到达上限立即合并缓冲并停止导入。
- -c <none|golomb>：设置倒排列表压缩方式；非法值自动回退为 golomb；启动时打印最终生效值。
- -t <阈值>：控制倒排缓冲批量落库的阈值；越小越频繁提交（峰值内存更低、索引更慢）。
- -q <query>：执行检索；使用 TF（对数缩放）× IDF（平滑）评分避免同分；Top 输出附带 Score。
- -s：关闭短语检索。默认开启时，多词查询要求 n-gram 位置相邻。

#### 示例
```bash
# 1) 构建索引（创建数据库）- Wikipedia XML
./wiser -x enwiki-latest-pages-articles.xml -m 10000 -c golomb data/wiser.db

# 2) 构建索引（创建数据库）- TSV
./wiser -x sample_dataset.tsv data/wiser.db

# 3) 构建索引（创建数据库）- JSON Lines / JSON 数组
./wiser -x sample.jsonl data/wiser.db
./wiser -x sample_array.json data/wiser.db

# 4) 在现有数据库中搜索（短语开启，默认）
./wiser -q "information retrieval" data/wiser.db

# 5) 关闭短语检索
./wiser -q "information retrieval" -s data/wiser.db

# 6) 调整缓冲阈值进行索引
./wiser -x wiki.xml -t 1000 data/wiser.db
```

---

## 数据加载（除 Wiki 外）

除了通过 CLI 的 -x 选项加载 Wikipedia XML，本项目还提供了从本地数据文件直接导入的加载器：TSV 与 JSON。

### 1) TSV 加载（TsvLoader）
- 格式：每行 `title[TAB]body`，可选首行表头；
- 用法：
  ```cpp
  wiser::WiserEnvironment env;
  env.initialize("data.db");
  wiser::TsvLoader tsv(&env);
  tsv.loadFromFile("dataset.tsv", /*has_header=*/true);
  env.flushIndexBuffer(); // 导入少量文档时，确保落库
  ```

### 2) JSON 加载（JsonLoader）
- 支持两种格式：
  - JSON Lines（NDJSON）：每行一个对象 `{"title":"...","body":"..."}`；
  - JSON 数组：`[{"title":"...","body":"..."}, ...]`；
- 自动识别：读取首个非空字符，若为 `[` 则按数组解析，否则按行解析；
- 字段要求：对象内需包含 `title` 与 `body` 两个字符串字段；
- 用法：
  ```cpp
  wiser::WiserEnvironment env;
  env.initialize("data.db");
  wiser::JsonLoader jl(&env);
  jl.loadFromFile("data.jsonl");       // JSON Lines
  jl.loadFromFile("data_array.json");  // JSON 数组
  env.flushIndexBuffer();
  ```

### 3) 快速上手（loader_demo）
工程包含一个示例程序 `loader_demo`，演示 TSV 与 JSON 的导入与检索：
```bash
# 构建
mkdir -p build && cd build
cmake .. && cmake --build .

# 运行
cd ..
./demo/bin/loader_demo            # Linux/macOS
# 或
./demo/bin/wiser_demo             # 第二个 demo
# Windows 下直接双击或在终端执行 demo/bin/*.exe
```
示例会从 `sample_dataset.tsv`、`sample.jsonl`、`sample_array.json` 导入，随后执行简单检索并打印正文预览（UTF-8 安全换行与截断）。
loader_demo 输出示例：

```$ ./demo/bin/loader_demo
=== Loader Demo (TSV + JSON) ===
[INFO] Wiser environment initialized successfully.
[INFO] Indexing up to: 30 documents
[INFO] Loading TSV from: ../data/sample_dataset.tsv
[INFO] [##################################################] 100% (3/3)
[INFO] TSV loader done. Lines imported: 3
[INFO] Loading JSON Lines from: ../data/sample.jsonl
[INFO] [##################################################] 100% (2/2)
[INFO] JSONL done. Imported: 2
[INFO] Loading JSON from: ../data/sample_array.json
[INFO] [##################################################] 100% (2/2)
[INFO] JSON array done. Objects imported: 2
[INFO] Loading JSON from: ../data/sample_array_test.json
[INFO] [##########----------------------------------------] 20% (6/30)
[INFO] Flushing index buffer with 527 token(s).
[INFO] Index buffer flushed successfully
[INFO] [#########################-------------------------] 50% (15/30)
[INFO] Flushing index buffer with 544 token(s).
[INFO] Index buffer flushed successfully
[INFO] [######################################------------] 76% (23/30)
[INFO] [######################################------------] 76% (23/30)
[INFO] JSON array done. Objects imported: 23
[INFO] Found 3 matching documents:
============================================================
1. Document ID: 3, Title: 信息检索, Score: 5.16019
2. Document ID: 5, Title: JSON 示例二, Score: 3.04769
3. Document ID: 7, Title: 数组示例二, Score: 3.04769
============================================================
[INFO] Found 3 matching documents (bodies):
============================================================
1) Document ID: 3  |  Title: 信息检索  |  Score: 5.16019
Body: 信息检索研究如何从大量非结构化数据中高效找到相关信息。 
------------------------------------------------------------
2) Document ID: 5  |  Title: JSON 示例二  |  Score: 3.04769
Body: 第二条 JSONL 文档，内容关于信息检索。
------------------------------------------------------------
3) Document ID: 7  |  Title: 数组示例二  |  Score: 3.04769
Body: 第二条数组文档，讨论倒排索引与信息检索。
============================================================
[INFO] Inverted index for query tokens:
  - Token: "信息" (id=49), docs(disk)=3
      [disk] doc 3 positions: 0, 24
      [disk] doc 5 positions: 11
      [disk] doc 7 positions: 13
      <no postings in mem>

[INFO] Flushing index buffer with 429 token(s).
[INFO] Index buffer flushed successfully
[INFO] Wiser environment shut down successfully.
Done. DB: loader_demo.db
```

### 数据格式要求
- 通用
  - 编码：UTF-8（建议无 BOM）。
  - 字段：必须包含非空的 title 与 body。
  - 唯一性：title 作为唯一键（数据库对 title 建有唯一索引）。重复的 title 会更新对应文档的正文。

- TSV（制表符分隔）
  - 每行一条记录：`title[TAB]body`。仅“第一个”TAB 作为分隔；后续 TAB 原样保留在 body 中。
  - 单行记录：不支持跨行正文（换行会被视为新纪录）。
  - 表头：CLI 下的 `-x <file.tsv>` 默认“跳过首行”作为表头；若你的 TSV 无表头，请通过代码调用 `TsvLoader::loadFromFile(path, /*has_header=*/false)` 或使用自定义导入流程。
  - 示例：
    ```tsv
    title	body
    快速排序	快速排序是一种分治法的排序算法，平均时间复杂度为 O(n log n)。
    倒排索引	倒排索引用于全文检索系统，记录词项出现在哪些文档中以及位置。
    信息检索	信息检索研究如何从大量非结构化数据中高效找到相关信息。
    ```

- JSON Lines（NDJSON）
  - 每行一个 JSON 对象，且必须包含字符串字段 `"title"` 与 `"body"`。
  - 解析：支持常见转义（\n/\t/\\/\"），不展开 `\uXXXX`，建议文件直接用 UTF-8 存放中文。
  - 示例：
    ```json
    {"title":"JSON 示例一","body":"这是第一条 JSON Lines 文档。"}
    {"title":"JSON 示例二","body":"第二条文档，包含关键词：信息检索。"}
    ```

- JSON 数组
  - 顶层是数组，元素为对象；每个对象必须包含字符串字段 `"title"` 与 `"body"`。
  - 示例：
    ```json
    [
      {"title": "数组示例一", "body": "这是第一条数组文档。"},
      {"title": "数组示例二", "body": "第二条数组文档，讨论倒排索引。"}
    ]
    ```

### 注意事项
- 控制台编码：Windows 下使用 UTF-8 终端（Windows Terminal/PowerShell 7 更佳），避免乱码。
- 字段规范：TSV 需严格按 `title[TAB]body`，JSON 需具有 `title`/`body` 字段；空标题或空正文会被跳过。

### 架构概览
- WiserEnvironment：统一环境与配置
- Database：SQLite3 封装
- Tokenizer：N-gram 分词（仅索引长度≥N 的 n-gram，位置连续）
- SearchEngine：查询处理、短语匹配（位置链）与 TF-IDF 排序
- Postings/InvertedIndex：索引结构
- WikiLoader：Wikipedia XML 解析与进度显示
- Utils：字符编码与工具函数

### 致谢

首先感谢《How to Develop a Search Engine》一书的作者——山田浩之、末永匡，以及书中配套的 wiser
项目。原项目以清晰的结构和严谨的实现为本仓库提供了坚实的出发点与灵感来源，本项目在其思想与数据结构的基础上进行了现代 C++
的重写与实践。
