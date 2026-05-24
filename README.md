# Wiser-CPP

<p align="right">
  <b>Language:</b>
  <a href="README.en.md">English</a> | <a href="README.md">中文</a>
</p>

## 商业级 C++20 全文搜索引擎

Wiser-CPP 是一个使用现代 C++20 构建的高性能全文搜索引擎，具备 BM25 排序、布尔查询、模糊搜索、拼写纠错、同义词扩展、自动补全、JWT 身份验证、API Key 鉴权、RBAC 角色权限、IP 速率限制等企业级特性。项目源于《How to Develop a Search Engine》（山田浩之、末永匡），在原有思想基础上进行了全面的现代化重写与商业化升级。

### ✨ 核心特性

<table>
<tr>
<td width="50%">

**🔍 搜索能力**
- BM25 概率相关性排序（可切换 TF-IDF）
- 布尔查询：`AND`、`OR`、`NOT`、`"短语"`、括号分组
- 模糊搜索：编辑距离容错匹配（0-2）
- 短语搜索：基于位置链的精确短语匹配
- 标题权重提升（默认 1.5x）
- 拼写纠错：`did_you_mean` 建议
- 同义词扩展：可配置词典，查询时自动扩展
- 自动补全：前缀匹配实时建议
- 搜索结果摘要（Snippet）与关键词高亮

</td>
<td width="50%">

**🔐 安全特性**
- JWT 身份验证（HS256 签名，可配置过期时间）
- API Key 无状态鉴权（`wsk_` 前缀密钥）
- RBAC 三级角色权限：Admin / Editor / Viewer
- 基于 IP 的令牌桶速率限制
- CORS 跨域保护
- 所有输入经过长度校验与 JSON 安全解析
- 密码 SHA-256 迭代哈希（10000 次迭代 + 随机盐）

</td>
</tr>
<tr>
<td>

**⚡ 性能优化**
- SQLite WAL 模式：并发读写
- LRU 查询缓存（1024 条，索引变更自动失效）
- 读写锁分离：搜索 `shared_lock`，索引 `unique_lock`
- SQLite 调优：mmap 256MB、缓存 16MB
- Golomb 编码倒排列表压缩
- 可调批量缓冲阈值

</td>
<td>

**📊 数据与运维**
- 多格式导入：XML（Wikipedia）、TSV、JSON/JSONL
- RESTful 文档 CRUD API
- 在线热备份（SQLite Online Backup API）
- 异步批量导入任务队列
- 健康检查探针（`/health`、`/ready`）
- JSON 配置文件 + 环境变量覆盖
- 优雅停机与信号处理

</td>
</tr>
</table>

**🎨 前端界面**
- Liquid Glass 风格 UI，明暗主题自适应
- 大海潮汐（浅色）/ 星空（暗色）动态 Canvas 背景
- 实时搜索建议下拉
- 拖拽文件上传与导入进度跟踪
- 登录 / 注册 / 用户菜单 / API Key 生成
- 响应式布局，移动端适配

### 📸 界面预览

<p align="center">
  <img src="img/1.png" alt="搜索界面" width="800">
</p>
<p align="center">
  <img src="img/2.png" alt="暗色主题" width="800">
</p>

---

## 📦 安装教程

### 前置要求

| 工具 | 最低版本 | 说明 |
|------|---------|------|
| CMake | 3.16+ | 构建系统 |
| C++ 编译器 | C++20 | MSVC 17+、Clang 20+、GCC 15+ |
| vcpkg | 最新 | 包管理器（推荐） |

### 第一步：安装 vcpkg（如未安装）

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

### 第二步：安装依赖

```bash
# 使用 vcpkg 安装所有依赖
vcpkg install spdlog fmt sqlite3 nlohmann-json jwt-cpp openssl
```

> **说明**：`jwt-cpp` 依赖 `openssl`，vcpkg 会自动安装。如果不需要身份验证功能，可以只安装 `spdlog fmt sqlite3 nlohmann-json`。

### 第三步：克隆项目

```bash
git clone https://github.com/YourUsername/wiser-cpp.git
cd wiser-cpp
```

### 第四步：构建

**Windows（推荐方式）**

```powershell
# 打开 "x64 Native Tools Command Prompt" 或 VS Developer PowerShell
# 确保使用 x64 架构

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

**CMake 构建选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_DEMO` | `ON` | 构建演示程序 |
| `BUILD_WEB_SERVER` | `ON` | 构建 Web 服务器 |
| `BUILD_TESTS` | `ON` | 构建单元测试（需要 Google Test） |

**构建产物：**
- `bin/wiser` — CLI 工具（索引 / 检索）
- `bin/wiser_web` — Web 服务器（HTTP API + 前端界面）
- `demo/bin/*` — 演示程序

### 安装（可选）
```bash
cmake --install build --config Release --prefix /usr/local
```

---

## 🚀 快速开始

### CLI 模式

```bash
# 导入数据（自动识别格式：TSV / JSON / JSONL / XML）
./bin/wiser -x data/sample.tsv my.db
./bin/wiser -x data/articles.jsonl my.db

# 搜索
./bin/wiser -q "information retrieval" my.db

# 启用短语搜索
./bin/wiser -q "search engine" -s my.db
```

### Web 服务模式

**基本启动：**
```bash
# 使用默认数据库启动
cd bin
./wiser_web

# 指定数据库
./wiser_web --db my.db

# 启用短语搜索
./wiser_web --db my.db --phrase=on
```

**使用配置文件启动（推荐）：**

创建 `config.json`：
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

**启动成功输出示例：**
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

打开浏览器访问：**http://localhost:8604**

---

## 🔐 身份验证系统

身份验证是**可选功能**，默认关闭。启用后提供完整的用户管理与权限控制。

### 启用身份验证

在配置文件中设置：
```json
{
  "auth_enabled": true,
  "jwt_secret": "your-secret-key-change-this-in-production",
  "token_expiry_hours": 24,
  "allow_registration": true
}
```

或使用环境变量：
```bash
export WISER_AUTH_ENABLED=true
export WISER_JWT_SECRET=your-secret-key
```

### 用户注册

```bash
# 注册 viewer（默认角色，只读）
curl -X POST http://localhost:8604/api/auth/register \
     -H "Content-Type: application/json" \
     -d '{"username":"alice","password":"MyPassword123"}'
# 响应: {"registered":true,"username":"alice"}

# 注册 editor（可添加/导入文档）
curl -X POST http://localhost:8604/api/auth/register \
     -H "Content-Type: application/json" \
     -d '{"username":"bob","password":"BobPass456","role":"editor"}'
# 响应: {"registered":true,"username":"bob"}
```

> **注意**：用户名长度 3-64 字符，密码长度 6-128 字符。Admin 角色不能通过公开注册接口创建。

### 用户登录

```bash
curl -X POST http://localhost:8604/api/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"bob","password":"BobPass456"}'
```

响应：
```json
{
  "token": "eyJhbGciOiJIUzI1NiJ9.eyJleHAi...",
  "expires_in_hours": 24
}
```

### 使用 JWT Token 访问受保护接口

```bash
# 将 token 放入 Authorization 头
TOKEN="eyJhbGciOiJIUzI1NiJ9..."

# 查看当前用户信息
curl http://localhost:8604/api/auth/me \
     -H "Authorization: Bearer $TOKEN"
# 响应: {"username":"bob","role":"editor"}

# 添加文档（需要 editor 权限）
curl -X POST http://localhost:8604/api/documents \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"title":"My Document","body":"Document content here"}'
# 响应: {"id":1,"title":"My Document"}
```

### 生成 API Key

API Key 适用于自动化脚本和 CI/CD 集成，无需频繁登录：

```bash
# 生成 API Key（需要先登录获取 token）
curl -X POST http://localhost:8604/api/auth/api-key \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" \
     -d '{}'
# 响应: {"api_key":"wsk_a5bf8833acb554f518e0eeb15637a528..."}
```

使用 API Key：
```bash
# 用 X-API-Key 头代替 Bearer Token
curl -X POST http://localhost:8604/api/documents \
     -H "X-API-Key: wsk_a5bf8833acb554f518e0eeb15637a528..." \
     -H "Content-Type: application/json" \
     -d '{"title":"Automated Import","body":"Created via API key"}'
```

### RBAC 角色权限

| 角色 | 权限 | 说明 |
|------|------|------|
| **Viewer** | 搜索、查看文档、查看统计 | 默认角色 |
| **Editor** | Viewer + 添加文档、批量导入 | 内容管理 |
| **Admin** | Editor + 删除文档、刷新索引、重建索引、备份 | 系统管理 |

未授权操作的响应示例：
```bash
# Viewer 尝试添加文档 → 403
curl -X POST http://localhost:8604/api/documents \
     -H "Authorization: Bearer $VIEWER_TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"title":"Test","body":"Content"}'
# 响应: {"error":"Editor role required"}  (HTTP 403)

# 无 token 访问受保护接口 → 401
curl -X POST http://localhost:8604/api/documents \
     -H "Content-Type: application/json" \
     -d '{"title":"Test","body":"Content"}'
# 响应: {"error":"Unauthorized"}  (HTTP 401)
```

---

## ⚡ 速率限制

基于 IP 的令牌桶限流，保护服务器免受滥用。

### 配置

```json
{
  "rate_limit_enabled": true,
  "rate_limit_max_tokens": 100,
  "rate_limit_refill_rate": 10
}
```

| 参数 | 说明 |
|------|------|
| `rate_limit_max_tokens` | 令牌桶最大容量（突发请求上限） |
| `rate_limit_refill_rate` | 每秒补充令牌数（持续 QPS 上限） |

超限时返回 HTTP 429：
```json
{"error": "Rate limit exceeded"}
```

---

## 📡 REST API 参考

### 公开接口（无需认证）

#### 搜索

```http
GET /api/search?q=关键词&scoring=bm25&page=1&page_size=20&fuzzy=1&snippet_len=120&phrase=0
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `q` | string | **必填** | 查询字符串，支持布尔语法 |
| `scoring` | string | `bm25` | 排序算法：`bm25` \| `tfidf` |
| `page` | int | `1` | 页码 |
| `page_size` | int | `20` | 每页条数（1-100） |
| `fuzzy` | int | `0` | 模糊搜索编辑距离（0-2） |
| `snippet_len` | int | `120` | 摘要长度 |
| `phrase` | int | `0` | 短语搜索开关 |

**示例：**
```bash
# 基本搜索
curl "http://localhost:8604/api/search?q=search+engine"

# 模糊搜索
curl "http://localhost:8604/api/search?q=serch&fuzzy=1"

# 分页
curl "http://localhost:8604/api/search?q=API&page=2&page_size=10"
```

**响应示例：**
```json
{
  "total_hits": 3,
  "page": 1,
  "page_size": 20,
  "took_ms": 0.95,
  "results": [
    {
      "id": 1,
      "title": "Wiser Search Engine",
      "author": "",
      "body": "Wiser is a modern full-text search engine...",
      "snippet": "Wiser is a modern full-text search engine...",
      "score": 1.71,
      "matched_tokens": ["se", "ea", "ar", "rc", "ch"]
    }
  ]
}
```

**布尔查询语法：**
```
search AND engine              # 两词都必须出现
search OR retrieval            # 任一词出现即可
"search engine"                # 精确短语
(search OR find) AND NOT web   # 组合查询
```

#### 自动补全

```bash
curl "http://localhost:8604/api/suggest?q=se&limit=10"
```

响应：
```json
{
  "suggestions": [
    {"type": "title", "text": "Wiser Search Engine", "doc_id": 1},
    {"type": "token", "text": "se", "docs_count": 3}
  ]
}
```

#### 获取文档

```bash
curl http://localhost:8604/api/documents/1
```

响应：
```json
{
  "id": 1,
  "title": "Wiser Search Engine",
  "body": "Wiser is a modern full-text search engine...",
  "author": "",
  "token_count": 90
}
```

#### 索引统计

```bash
curl http://localhost:8604/api/stats
```

响应：
```json
{
  "document_count": 6,
  "total_tokens": 472,
  "avg_document_length": 78.7,
  "token_length": 2,
  "compress_method": "none",
  "phrase_search": false,
  "scoring_method": "bm25"
}
```

#### 健康检查

```bash
# 存活探针
curl http://localhost:8604/health
# {"status":"ok"}

# 就绪探针
curl http://localhost:8604/ready
# {"ready":true,"document_count":6}
```

### 受保护接口（需要认证）

> 以下接口在启用 `auth_enabled` 时需要 `Authorization: Bearer <token>` 或 `X-API-Key: <key>` 头。

#### 添加文档 <sup>Editor+</sup>

```bash
curl -X POST http://localhost:8604/api/documents \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"title":"文档标题","body":"文档正文内容","author":"作者（可选）"}'
```

响应：`{"id":1,"title":"文档标题"}`

#### 删除文档 <sup>Admin</sup>

```bash
curl -X DELETE http://localhost:8604/api/documents/1 \
     -H "Authorization: Bearer $ADMIN_TOKEN"
```

#### 批量导入 <sup>Editor+</sup>

支持 multipart/form-data 上传文件（TSV / JSON / JSONL / XML）：

```bash
curl -X POST http://localhost:8604/api/import \
     -H "Authorization: Bearer $TOKEN" \
     -F "file=@data/articles.jsonl"
```

响应：
```json
{"accepted": 1, "task_ids": ["0000000000000001"]}
```

#### 索引管理 <sup>Admin</sup>

```bash
# 刷新索引缓冲区
curl -X POST http://localhost:8604/api/index/flush \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'

# 重建索引
curl -X POST http://localhost:8604/api/index/rebuild \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'
```

#### 在线备份 <sup>Admin</sup>

```bash
curl -X POST http://localhost:8604/api/admin/backup \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -H "Content-Type: application/json" -d '{}'
```

#### 任务管理

```bash
# 查看所有导入任务
curl http://localhost:8604/api/tasks

# 查看特定任务
curl "http://localhost:8604/api/task?id=0000000000000001"
```

### 身份验证接口

```bash
# 注册
curl -X POST http://localhost:8604/api/auth/register \
     -H "Content-Type: application/json" \
     -d '{"username":"user","password":"pass123","role":"viewer"}'

# 登录
curl -X POST http://localhost:8604/api/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"user","password":"pass123"}'

# 获取当前用户
curl http://localhost:8604/api/auth/me \
     -H "Authorization: Bearer $TOKEN"

# 生成 API Key
curl -X POST http://localhost:8604/api/auth/api-key \
     -H "Authorization: Bearer $TOKEN" \
     -H "Content-Type: application/json" -d '{}'
```

---

## ⚙️ 配置参考

Wiser 支持三级配置优先级：**环境变量 > JSON 配置文件 > 默认值**

### 完整配置文件示例

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

### 参数说明

| 参数 | 环境变量 | 默认值 | 说明 |
|------|---------|--------|------|
| `port` | `WISER_PORT` | `54321` | 监听端口 |
| `db_path` | `WISER_DB_PATH` | `wiser.db` | 数据库文件路径 |
| `log_level` | `WISER_LOG_LEVEL` | `info` | 日志级别：`trace/debug/info/warn/error` |
| `token_len` | `WISER_TOKEN_LEN` | `2` | N-gram 分词长度 |
| `compress_method` | — | `none` | 倒排压缩：`none` \| `golomb` |
| `buffer_update_threshold` | — | `2048` | 缓冲区刷新阈值 |
| `max_index_count` | — | `-1` | 最大索引文档数（-1=无限） |
| `enable_phrase_search` | — | `false` | 短语搜索开关 |
| `scoring_method` | — | `bm25` | 排序算法：`bm25` \| `tfidf` |
| `bm25_k1` | — | `1.2` | BM25 k1 参数（词频饱和度） |
| `bm25_b` | — | `0.75` | BM25 b 参数（文档长度归一化） |
| `title_boost` | `WISER_TITLE_BOOST` | `1.5` | 标题匹配权重系数（1.0-10.0） |
| `auth_enabled` | `WISER_AUTH_ENABLED` | `false` | 启用 JWT 身份验证 |
| `jwt_secret` | `WISER_JWT_SECRET` | — | JWT 签名密钥（启用认证时必填） |
| `token_expiry_hours` | — | `24` | JWT Token 有效时间（小时） |
| `allow_registration` | — | `true` | 允许用户自行注册 |
| `rate_limit_enabled` | `WISER_RATE_LIMIT` | `false` | 启用速率限制 |
| `rate_limit_max_tokens` | — | `100` | 令牌桶最大容量 |
| `rate_limit_refill_rate` | — | `10` | 每秒令牌补充速率 |

### 同义词词典

创建 `synonyms.txt` 文件，每行一组同义词，逗号分隔：
```
# 注释行
电脑,计算机,computer
搜索,检索,查找
fast,quick,rapid
```
查询时自动扩展：`搜索引擎` → `(搜索 OR 检索 OR 查找)引擎`

---

## 🎨 前端功能

前端基于原生 HTML/CSS/JavaScript 构建，采用 Liquid Glass 设计语言。

### 搜索功能
- 输入框实时搜索建议（自动补全）
- 布尔查询语法支持
- 搜索结果分页浏览
- 关键词高亮显示
- 文档详情模态框（含鼠标跟随光晕效果）

### 文档管理
- 点击「+」按钮可在线添加文档（需 Editor 权限）
- 拖拽文件上传导入（支持 TSV / JSON / JSONL）
- 导入进度实时追踪

### 身份验证 UI
- 导航栏登录 / 注册按钮（仅在启用 auth 时显示）
- 登录 / 注册模态框（支持角色选择：Viewer / Editor）
- 登录后显示用户头像菜单
- 用户下拉菜单：角色显示、API Key 生成、退出登录

### 主题与视觉
- 自动跟随系统的明暗主题切换
- 浅色主题：大海潮汐动态背景
- 暗色主题：星空粒子动态背景
- Liquid Glass 毛玻璃效果卡片
- 鼠标跟随光晕效果

---

## 🏗️ 目录结构

```
wiser-cpp/
├── include/wiser/            # 公共头文件
│   ├── web/                  #   Web 服务头文件
│   │   ├── auth.h            #     身份验证模块
│   │   ├── rate_limiter.h    #     速率限制器
│   │   ├── routes.h          #     路由声明
│   │   └── ...
│   ├── config.h              #   编译期常量
│   ├── config_loader.h       #   配置加载器
│   ├── search_engine.h       #   搜索引擎核心
│   ├── database.h            #   数据库层
│   └── ...
├── src/                      # 核心源码
│   ├── web/                  #   Web 服务实现
│   │   ├── auth.cpp          #     JWT/API Key/RBAC 实现
│   │   ├── routes.cpp        #     HTTP API 路由
│   │   └── ...
│   ├── search_engine.cpp     #   搜索引擎核心
│   ├── database.cpp          #   SQLite 数据库层
│   ├── config_loader.cpp     #   JSON 配置加载
│   └── ...
├── web/                      # 前端静态资源
│   ├── index.html            #   单页应用
│   ├── script.js             #   前端逻辑（搜索/认证/动画）
│   └── styles.css            #   Liquid Glass 样式
├── demo/                     # 演示程序
├── tests/                    # 单元测试
├── bin/                      # 构建产物（可执行文件）
├── lib/                      # 构建产物（静态库）
├── CMakeLists.txt            # CMake 构建配置
└── README.md
```

---

## 🏛️ 架构概览

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

核心模块：
- **SearchEngine**：查询处理流水线（分词 → 倒排检索 → 布尔执行 → BM25 评分 → 标题加权 → LRU 缓存）
- **QueryParser**：递归下降布尔查询解析器（AND / OR / NOT / 引号短语 / 括号分组）
- **WiserEnvironment**：统一环境管理（配置持久化、同义词词典、索引缓冲协调）
- **AuthManager**：JWT 身份验证、API Key 管理、RBAC 角色权限
- **RateLimiter**：基于 IP 的令牌桶速率限制（线程安全）
- **Database**：SQLite3 封装（WAL 模式、预编译语句、在线备份）
- **Tokenizer**：N-gram 分词器（Unicode 安全）
- **SynonymDict**：同义词词典（CSV 加载、查询扩展）
- **Postings / InvertedIndex**：倒排索引结构（Golomb 编码压缩）
- **Loaders**：WikiLoader / TsvLoader / JsonLoader
- **ConfigLoader**：nlohmann/json 配置文件 + 环境变量加载器

---

## 🖥️ 命令行参考

### wiser（CLI 工具）

```
usage: wiser [options] db_file

indexing : -x <data_file> [-m N] [-t N] [-c none|golomb]
search   : -q <query> [-s]
```

| 参数 | 说明 |
|------|------|
| `db_file` | SQLite 数据库路径（不存在时创建） |
| `-x <file>` | 导入文件（`.xml` / `.tsv` / `.json` / `.jsonl` / `.ndjson`） |
| `-q <query>` | 执行搜索（可与 `-x` 组合：先导入再搜索） |
| `-c <method>` | 压缩算法：`none`（默认）\| `golomb` |
| `-m <N>` | 最大导入文档数（`-1` = 无限） |
| `-t <N>` | 缓冲区合并阈值（默认 2048） |
| `-s` | 开启短语搜索 |
| `-h` | 显示帮助 |

### wiser_web（Web 服务器）

| 参数 | 说明 |
|------|------|
| `--config <file>` | JSON 配置文件路径 |
| `--db <file>` | 数据库路径（默认 `wiser_web.db`） |
| `--phrase=on\|off` | 短语搜索开关 |
| `-h` | 显示帮助 |

> **提示**：`wiser_web` 从 `../web` 目录提供前端静态资源，推荐从 `bin/` 目录启动。

---

## 🔧 依赖说明

| 库 | 用途 | 获取方式 |
|----|------|---------|
| SQLite3 | 数据存储 | vcpkg 或系统包 |
| spdlog + fmt | 日志输出 | vcpkg 或 FetchContent 自动拉取 |
| nlohmann/json | JSON 解析 | vcpkg |
| jwt-cpp | JWT 令牌 | vcpkg（依赖 OpenSSL） |
| OpenSSL | 加密 | vcpkg（jwt-cpp 的依赖） |
| cpp-httplib | HTTP 服务 | 已内置（header-only） |

> **说明**：spdlog 和 fmt 若本机未安装，CMake 会通过 FetchContent 自动拉取。SQLite3 优先查找 vcpkg（`unofficial::sqlite3`），也支持手工指定 `SQLITE3_INCLUDE_DIR` / `SQLITE3_LIBRARY`。Windows 下构建后会自动将依赖 DLL 复制到可执行文件旁。

---

## 🙏 致谢

感谢《How to Develop a Search Engine》（山田浩之、末永匡）作者与 wiser 原项目。本项目在其思想与数据结构基础上进行了全面的现代 C++20 重写，加入了 BM25 排序、布尔查询、模糊搜索、拼写纠错、同义词扩展、JWT 身份验证、RBAC 权限控制、速率限制、WAL 模式、查询缓存等企业级特性。

---

## 📄 许可证

MIT License