# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## Wiser-CPP：现代 C++ 全文检索引擎

本项目是 wiser 全文检索引擎的现代 C++ 重写版，展示 RAII、智能指针与基于 STL 的设计实践。项目源于《How to Develop a Search Engine》（山田浩之、末永匡），原实现为 C 语言。此仓库在原有思想基础上加入了若干现代化改造与实用特性。

### 功能特性
- C++20 / CMake 构建，跨平台
- N-gram 倒排索引全文检索（N 可配置，默认 2）
- SQLite3 持久化
- 多格式导入：XML（Wikipedia）、TSV、JSON（JSONL/NDJSON/数组）
- 短语检索（相邻位置链）可开关（默认关闭）
- 倒排压缩：golomb/none
- 批量缓冲阈值可配置
- Web 服务与前端界面（wiser_web）：多文件上传异步导入、查询接口

### 目录结构
```
wiser-cpp/
├── include/                # 头文件
├── src/                    # 源码
├── demo/                   # 演示程序（可执行输出到 demo/bin）
├── web/                    # 前端静态资源（index.html/script.js/styles.css）
├── bin/ lib/               # 运行时输出目录（构建后产生）
├── CMakeLists.txt          # CMake 配置
└── README.md               # 说明文档
```

### 依赖
- CMake ≥ 3.16
- C++20 编译器（MSVC 17+/Clang 20+/GCC 15+）
- SQLite3
- spdlog、fmt

说明：
- spdlog、fmt 若本机未安装，CMake 会通过 FetchContent 自动拉取并构建；
- SQLite3 优先查找 vcpkg 的 unofficial::sqlite3，其次查找系统包（SQLite::SQLite3）；也支持手工指定 SQLITE3_INCLUDE_DIR/SQLITE3_LIBRARY；
- Windows 下构建后会自动将依赖 DLL 复制到可执行旁边，安装时也会打包这些 DLL（基于 TARGET_RUNTIME_DLLS）。

### 构建

通用构建流程：
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j
```
Windows（cmd.exe）：
```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

产物：
- bin/wiser           —— CLI 工具（索引/检索）
- bin/wiser_web       —— Web 服务（HTTP + 前端）
- demo/bin/*          —— 演示程序（不参与安装）

### 安装（可选）
将核心库与可执行、静态资源安装到指定前缀：
```bash
cmake --install build --config Release --prefix install
```
安装内容：
- <prefix>/bin: wiser、wiser_web 以及其依赖 DLL（Windows）
- <prefix>/lib: wiser_core 等
- <prefix>/web: 前端静态资源（供 wiser_web 使用）
- demo 可执行不安装（始终位于 demo/bin）

### 快速开始（CLI wiser）
```bash
# 1) 生成数据库（按后缀自动选择加载器）
./bin/wiser -x web/data/sample_dataset.tsv data/wiser.db
./bin/wiser -x web/data/sample.jsonl      data/wiser.db
./bin/wiser -x web/data/sample_array.json data/wiser.db
# Wikipedia XML 也支持：./bin/wiser -x enwiki-latest-pages-articles.xml data/wiser.db

# 2) 检索
./bin/wiser -q "information retrieval" data/wiser.db
```
CLI 用法（摘要）：
```
usage: wiser [options] db_file

indexing : -x <data_file> [-m N] [-t N] [-c none|golomb]
search   : -q <query> [-s]
```

### Web 服务（wiser_web）
启动：
```bash
# 默认使用 ./wiser_web.db
./bin/wiser_web
# 指定数据库路径（若不存在则创建并使用默认设置：短语检索=关闭）
./bin/wiser_web my.db
# 覆盖短语检索并持久化（对已存在库同样生效）
./bin/wiser_web my.db --phrase=on
./bin/wiser_web my.db --phrase=off
```
打开浏览器访问：http://localhost:54321

- 前端：
  - 支持多文件选择/拖拽上传；
  - 上传后前端根据任务 ID 轮询，所有任务完成后弹出汇总结果；
  - 搜索结果支持展开正文预览。
- 后端：
  - 静态资源默认从相对路径 `../web` 提供；安装到前缀后对应 <prefix>/web；
  - 上传接口支持 .xml / .tsv / .json / .jsonl / .ndjson，自动选择加载器；导入在后台任务队列并行处理；
  - 写入索引时串行化以保证数据库一致性；
  - 运行时设置通过 WiserEnvironment 的 set 方法即时写数据库（initialize 之后）。

REST API（简要）：
- GET `/api/search?q=关键词`
- POST `/api/import`（multipart/form-data；可多文件，表单字段名均为 `file`）：

  - 响应：`{"accepted": N, "task_ids": ["...", ...]}`
- GET `/api/task?id=<task_id>`：单个任务状态
- GET `/api/tasks`：全部任务快照

示例（多文件上传，curl）：
```bash
curl -F "file=@web/data/sample_dataset.tsv" \
     -F "file=@web/data/sample.jsonl" \
     http://localhost:54321/api/import
```

### 配置与持久化
- WiserEnvironment 会在 `initialize` 后将当前内存中的参数写入设置表（新库时用作默认种子）；
- 之后调用 `setTokenLength`/`setPhraseSearchEnabled`/`setBufferUpdateThreshold`/`setCompressMethod`/`setMaxIndexCount` 均会立刻写入数据库；
- wiser_web 的 `--phrase=on|off` 会即时生效，并在退出时仍会执行 `shutdown()` 进行兜底持久化；
- 新库默认：`TokenLen=2`、`PhraseSearch=off`、`BufferThreshold=2048`、`Compress=none`、`MaxIndex=-1`。

### 架构概览
- WiserEnvironment：统一环境与配置（即时持久化设置）
- Database：SQLite3 封装
- Tokenizer：N-gram 分词
- SearchEngine：查询、短语匹配与 TF-IDF 排序
- Postings/InvertedIndex：索引结构
- Loaders：WikiLoader / TsvLoader / JsonLoader
- Web：cpp-httplib（头文件） + 前端页面

### 命令行参数详解（wiser）
- 位置参数 `db_file`
  - SQLite 数据库文件路径；不存在时会创建。
  - 与 `-x` 同用时：若目标数据库已存在，会报错并退出（避免覆盖已有数据）。
  - 仅与 `-q` 同用时：应指向已存在的数据库。
- `-h`, `--help`
  - 显示帮助并退出（退出码 0）。
- `-x <data_file>`
  - 指定要导入的数据文件路径；根据后缀自动选择加载器：
    - `.xml` -> Wikipedia XML
    - `.tsv` -> TSV（每行 `title[TAB]body`，默认跳过首行表头）
    - `.json` / `.jsonl` / `.ndjson` -> JSON（数组或 JSON Lines 自动识别）
  - 导入完成后会自动触发一次缓冲落库（flush）。
- `-q <search_query>`
  - 执行检索，打印按分数排序的结果与正文片段预览。
  - 可与 `-x` 同时使用：先索引再检索。
- `-c <compress_method>`
  - 设置倒排列表压缩算法：`none`（默认）| `golomb`。
  - `golomb` 压缩率较好、CPU 开销更高；`none` 速度更快、体积更大。
  - 本次运行会立即写入数据库设置，后续启动沿用。
- `-m <max_index_count>`
  - 本次导入的最大文档数；`-1` 表示不限（默认 -1）。
  - 达到上限后会停止导入并落库。
- `-t <buffer_threshold>`
  - 倒排缓冲合并阈值（默认 2048）。值越小越频繁提交（内存更低、导入更慢）。
- `-s`
  - 开启短语检索。wiser CLI 默认“关闭”短语检索；加 `-s` 则本次运行开启。
  - 短语检索开启时，多词查询要求 n-gram 位置相邻。

### 命令行参数详解（wiser_web）
- 位置参数 `db_file`（可选）
  - 指定数据库文件路径；默认 `./wiser_web.db`。
  - 文件不存在时会创建；新库默认：`PhraseSearch=off`、`TokenLen=2`、`BufferThreshold=2048`。
- `--phrase=on|off`
  - 短语检索设置，立即生效并写入数据库（对既有库同样有效）。
- `-h`, `--help`
  - 显示使用方法并退出。

运行特性：wiser_web 固定监听 `0.0.0.0:54321`，静态资源从相对路径 `../web` 提供（安装后为 `<prefix>/web`）。

### 致谢
感谢《How to Develop a Search Engine》作者与 wiser 原项目。原项目结构清晰、实现严谨，为本仓库提供了基础与灵感；本项目在其思想与数据结构基础上进行了现代 C++ 的重写与工程化实践。
