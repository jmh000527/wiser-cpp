cmake_minimum_required(VERSION 3.16)

project(wiser-cpp
        VERSION 1.0.0
        DESCRIPTION "Modern C++ full-text search engine based on wiser"
        LANGUAGES CXX
)

# C++20标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可配置选项：是否构建 demo 与 web_server
option(BUILD_DEMOS "Build demo executables (wiser_demo, loader_demo)" ON)
option(BUILD_WEB_SERVER "Build web server executable" ON)

# 编译选项
if (MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else ()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

# 尝试查找SQLite3
find_package(SQLite3 QUIET)
if (SQLite3_FOUND)
    set(HAVE_SQLITE 1)
else ()
    # 回退到手动查找头文件与库
    find_library(SQLITE3_LIBRARY sqlite3)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
    if (SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
        set(HAVE_SQLITE 1)
    endif ()
endif ()

# 源文件
set(WISER_SOURCES
        src/wiser_environment.cpp
        src/database.cpp
        src/search_engine.cpp
        src/tokenizer.cpp
        src/postings.cpp
        src/utils.cpp
        src/wiki_loader.cpp
        # 新增加载器
        # src/directory_loader.cpp  (已移除)
        src/tsv_loader.cpp
        src/json_loader.cpp
)

# 创建静态库
add_library(wiser_core STATIC ${WISER_SOURCES})
# 要求C++20
target_compile_features(wiser_core PUBLIC cxx_std_20)
# 使库位置无关（方便生成共享库或在不同目录链接）
set_target_properties(wiser_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 目标头文件目录（工程自身，安装后可见）
target_include_directories(wiser_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接外部依赖（gflags, glog, sqlite3）
# 先尝试通过 CONFIG 包（例如 vcpkg 提供的导出目标）
find_package(gflags CONFIG QUIET)

# 如果 gflags 的导出目标使用了不同命名空间，为 glog 的配置文件兼容性创建别名
if (NOT TARGET gflags::gflags)
    if (TARGET GFlags::gflags)
        add_library(gflags::gflags ALIAS GFlags::gflags)
    elseif (TARGET gflags)
        add_library(gflags::gflags ALIAS gflags)
    endif()
endif()

find_package(glog CONFIG QUIET)
find_package(unofficial-sqlite3 CONFIG QUIET)

# 选择 gflags 目标名
if (TARGET gflags::gflags)
    set(GFLAGS_TARGET gflags::gflags)
elseif (TARGET GFlags::gflags)
    set(GFLAGS_TARGET GFlags::gflags)
elseif (TARGET gflags)
    set(GFLAGS_TARGET gflags)
endif()

# 如果还没找到，尝试非-CONFIG 的 find_package
if (NOT GFLAGS_TARGET)
    find_package(gflags QUIET)
    if (TARGET gflags::gflags)
        set(GFLAGS_TARGET gflags::gflags)
    elseif (TARGET GFlags::gflags)
        set(GFLAGS_TARGET GFlags::gflags)
    elseif (TARGET gflags)
        set(GFLAGS_TARGET gflags)
    endif()
endif()

if (NOT GFLAGS_TARGET)
    message(FATAL_ERROR "未找到 gflags (目标 gflags::gflags)，请通过 vcpkg 或系统包管理器安装并提供 CMake 导出目标。")
endif()

# glog 目标名
if (TARGET glog::glog)
    set(GLOG_TARGET glog::glog)
elseif (TARGET glog)
    set(GLOG_TARGET glog)
endif()

if (NOT GLOG_TARGET)
    find_package(glog QUIET)
    if (TARGET glog::glog)
        set(GLOG_TARGET glog::glog)
    elseif (TARGET glog)
        set(GLOG_TARGET glog)
    endif()
endif()

if (NOT GLOG_TARGET)
    message(FATAL_ERROR "未找到 glog (目标 glog::glog)，请通过 vcpkg 或系统包管理器安装并提供 CMake 导出目标。")
endif()

# SQLite3 处理（沿用原逻辑）
# 注意：顶部已调用过 find_package(SQLite3) 并可能通过手工查找设置 HAVE_SQLITE，
# 这里仅使用已知的变量或 vcpkg 的 unofficial-sqlite3 导出目标，不再重复调用 find_package。
if (unofficial-sqlite3_FOUND)
    message(STATUS "SQLite3 found via vcpkg unofficial-sqlite3")
    set(SQLITE_TARGET unofficial::sqlite3::sqlite3)
elseif (SQLite3_FOUND)
    message(STATUS "SQLite3 found via find_package: using target SQLite::SQLite3")
    set(SQLITE_TARGET SQLite::SQLite3)
elseif (HAVE_SQLITE)
    message(STATUS "SQLite3 found manually: lib='${SQLITE3_LIBRARY}', include='${SQLITE3_INCLUDE_DIR}'")
    set(SQLITE_TARGET "")
else()
    message(FATAL_ERROR "未找到 SQLite3，请安装开发包或通过 vcpkg/conan 配置，或手工设置 SQLITE3_LIBRARY/SQLITE3_INCLUDE_DIR。")
endif()

# 链接库（如果是手动查找 sqlite，单独处理 include/library）
if (SQLITE_TARGET)
    target_link_libraries(wiser_core PRIVATE ${SQLITE_TARGET} ${GFLAGS_TARGET} ${GLOG_TARGET})
else()
    target_include_directories(wiser_core PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(wiser_core PRIVATE ${SQLITE3_LIBRARY} ${GFLAGS_TARGET} ${GLOG_TARGET})
endif()

# 主程序
add_executable(wiser src/main.cpp)
target_link_libraries(wiser wiser_core)

# 演示程序（源自 demo/ 目录）
if (BUILD_DEMOS)
    add_executable(wiser_demo demo/demo.cpp)
    target_link_libraries(wiser_demo wiser_core)

    # 新增：加载器演示程序（源自 demo/ 目录）
    add_executable(loader_demo demo/loader_demo.cpp)
    target_link_libraries(loader_demo wiser_core)
endif()

# 新增：Web服务器
if (BUILD_WEB_SERVER)
    add_executable(web_server src/web_server.cpp)
    target_link_libraries(web_server wiser_core)
endif()

# 设置输出目录
# - 核心程序与库仍输出到构建目录
# - demo 可执行文件输出到源码目录下 demo/bin
set_target_properties(wiser PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
set_target_properties(wiser_core PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
)

# demo 可执行文件输出到 demo/bin（单配置生成器）
set_target_properties(wiser_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/demo/bin
)
set_target_properties(loader_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/demo/bin
)
set_target_properties(web_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# 多配置生成器（如 Visual Studio）每配置目录
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    set_target_properties(wiser PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/bin)
    set_target_properties(wiser_core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/lib)

    # demo 可执行文件输出到源码目录 demo/bin
    set_target_properties(wiser_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/demo/bin)
    set_target_properties(loader_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/demo/bin)
    set_target_properties(web_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/bin)
endforeach()

# 安装规则
install(TARGETS wiser DESTINATION bin)
install(TARGETS wiser_core DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# 安装其他可执行文件（如果构建）
if (BUILD_DEMOS)
    install(TARGETS wiser_demo loader_demo RUNTIME DESTINATION demo/bin)
endif()
if (BUILD_WEB_SERVER)
    install(TARGETS web_server RUNTIME DESTINATION bin)
endif()
