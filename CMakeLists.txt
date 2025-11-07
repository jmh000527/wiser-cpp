cmake_minimum_required(VERSION 3.16)

project(wiser-cpp
        VERSION 1.0.0
        DESCRIPTION "Modern C++ full-text search engine based on wiser"
        LANGUAGES CXX
)

message(STATUS "Configuring wiser-cpp ${PROJECT_VERSION}")

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

# Options
option(BUILD_DEMO "构建 demo 程序" ON)
option(BUILD_WEB_SERVER "构建 web_server 程序" ON)
message(STATUS "Build options: BUILD_DEMO=${BUILD_DEMO}, BUILD_WEB_SERVER=${BUILD_WEB_SERVER}")

# Compile options
if (MSVC)
    message(STATUS "Using MSVC compiler flags")
    add_compile_options(/W4 /permissive- /EHsc /Zc:__cplusplus /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else ()
    message(STATUS "Using GNU/Clang compiler flags")
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Linux 特定选项
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Adding Linux-specific pthread options")
        add_compile_options(-pthread)
        add_link_options(-pthread)
    endif ()
endif ()

# 依赖查找
message(STATUS "Looking for dependencies...")

find_package(spdlog CONFIG REQUIRED)
message(STATUS "spdlog: ${spdlog_DIR}")

find_package(fmt CONFIG REQUIRED)
message(STATUS "fmt: ${fmt_DIR}")

# SQLite3 依赖 - 根据平台使用不同的包
message(STATUS "Detected platform: ${CMAKE_SYSTEM_NAME}")
if (WIN32)
    message(STATUS "Windows platform detected, using unofficial-sqlite3 package from vcpkg")
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    set(SQLITE3_TARGET unofficial::sqlite3::sqlite3)
    set(SQLITE3_FOUND ${unofficial-sqlite3_FOUND})
    set(SQLITE3_SOURCE "vcpkg unofficial package")

    # 打印 vcpkg 相关信息
    if (unofficial-sqlite3_FOUND)
        # 尝试获取包版本信息
        if (DEFINED unofficial-sqlite3_VERSION)
            message(STATUS "SQLite3: using unofficial-sqlite3 v${unofficial-sqlite3_VERSION} from vcpkg")
        else ()
            message(STATUS "SQLite3: using unofficial-sqlite3 package from vcpkg")
        endif ()

        # 打印包路径信息（通常包含vcpkg）
        get_target_property(SQLITE3_INCLUDE_DIR unofficial::sqlite3::sqlite3 INTERFACE_INCLUDE_DIRECTORIES)
        if (SQLITE3_INCLUDE_DIR)
            message(STATUS "SQLite3 include path: ${SQLITE3_INCLUDE_DIR}")
            if (SQLITE3_INCLUDE_DIR MATCHES "vcpkg")
                message(STATUS "  -> Found vcpkg installation")
            endif ()
        endif ()
    endif ()

else ()
    message(STATUS "Unix-like platform detected, using system SQLite3")
    find_package(SQLite3 REQUIRED)
    set(SQLITE3_TARGET SQLite::SQLite3)
    set(SQLITE3_FOUND ${SQLite3_FOUND})
    set(SQLITE3_SOURCE "system package")
    message(STATUS "SQLite3: using system package, version ${SQLite3_VERSION}")
endif ()

# 检查是否通过vcpkg管理依赖
set(VCPKG_INFO "")
if (DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_INFO "vcpkg: $ENV{VCPKG_ROOT}")
elseif (DEFINED ENV{VCPKG_INSTALLATION_ROOT})
    set(VCPKG_INFO "vcpkg: $ENV{VCPKG_INSTALLATION_ROOT}")
endif ()

# Sources (core library only, 不含 main/web_server)
set(WISER_CORE_SOURCES
        src/database.cpp
        src/json_loader.cpp
        src/postings.cpp
        src/search_engine.cpp
        src/tokenizer.cpp
        src/tsv_loader.cpp
        src/utils.cpp
        src/wiki_loader.cpp
        src/wiser_environment.cpp
)

message(STATUS "Creating core library with ${WISER_CORE_SOURCES} source files")

add_library(wiser_core STATIC ${WISER_CORE_SOURCES})

# Public headers
target_include_directories(wiser_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
message(STATUS "Added include directory: ${CMAKE_SOURCE_DIR}/include")

# 链接依赖
message(STATUS "Linking dependencies to wiser_core")
target_link_libraries(wiser_core PUBLIC spdlog::spdlog)
target_link_libraries(wiser_core PUBLIC fmt::fmt)
target_link_libraries(wiser_core PRIVATE ${SQLITE3_TARGET})
message(STATUS "Linked SQLite3 target: ${SQLITE3_TARGET}")

# Executables
message(STATUS "Creating executables...")

# main 程序
add_executable(wiser src/main.cpp)
target_link_libraries(wiser PRIVATE wiser_core)
message(STATUS "Added main executable: wiser")

# demo 程序
set(DEMO_TARGETS_CREATED FALSE)
if (BUILD_DEMO)
    message(STATUS "Building demo programs")
    if (EXISTS ${CMAKE_SOURCE_DIR}/demo/demo.cpp)
        add_executable(wiser_demo demo/demo.cpp)
        target_link_libraries(wiser_demo PRIVATE wiser_core)
        message(STATUS "Added demo executable: wiser_demo")
        set(DEMO_TARGETS_CREATED TRUE)
    else ()
        message(STATUS "demo/demo.cpp not found, skipping wiser_demo")
    endif ()
    if (EXISTS ${CMAKE_SOURCE_DIR}/demo/loader_demo.cpp)
        add_executable(loader_demo demo/loader_demo.cpp)
        target_link_libraries(loader_demo PRIVATE wiser_core)
        message(STATUS "Added demo executable: loader_demo")
        set(DEMO_TARGETS_CREATED TRUE)
    else ()
        message(STATUS "demo/loader_demo.cpp not found, skipping loader_demo")
    endif ()
else ()
    message(STATUS "Demo programs disabled by BUILD_DEMO option")
endif ()

# web_server 程序
set(WEB_SERVER_CREATED FALSE)
if (BUILD_WEB_SERVER)
    if (EXISTS ${CMAKE_SOURCE_DIR}/src/web_server.cpp)
        add_executable(wiser_web src/web_server.cpp)
        target_link_libraries(wiser_web PRIVATE wiser_core)
        message(STATUS "Added web server executable: wiser_web")
        set(WEB_SERVER_CREATED TRUE)
    else ()
        message(WARNING "BUILD_WEB_SERVER is ON but src/web_server.cpp not found")
    endif ()
else ()
    message(STATUS "Web server disabled by BUILD_WEB_SERVER option")
endif ()

# Output directories
set(OUT_BIN ${CMAKE_SOURCE_DIR}/bin)
set(OUT_LIB ${CMAKE_SOURCE_DIR}/lib)
set(OUT_DEMO_BIN ${CMAKE_SOURCE_DIR}/demo/bin)

message(STATUS "Output directories:")
message(STATUS "  Binaries: ${OUT_BIN}")
message(STATUS "  Libraries: ${OUT_LIB}")
message(STATUS "  Demo Binaries: ${OUT_DEMO_BIN}")

# 设置输出目录
set_target_properties(wiser_core PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${OUT_LIB}
        LIBRARY_OUTPUT_DIRECTORY ${OUT_LIB}
        RUNTIME_OUTPUT_DIRECTORY ${OUT_BIN}
)
message(STATUS "Set output directories for wiser_core")

# 为不同目标分别设置可执行输出目录
if (TARGET wiser)
    set_target_properties(wiser PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUT_BIN})
    message(STATUS "Set wiser output to ${OUT_BIN}")
endif ()
if (TARGET wiser_web)
    set_target_properties(wiser_web PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUT_BIN})
    message(STATUS "Set wiser_web output to ${OUT_BIN}")
endif ()
if (TARGET wiser_demo)
    set_target_properties(wiser_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUT_DEMO_BIN})
    message(STATUS "Set wiser_demo output to ${OUT_DEMO_BIN}")
endif ()
if (TARGET loader_demo)
    set_target_properties(loader_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUT_DEMO_BIN})
    message(STATUS "Set loader_demo output to ${OUT_DEMO_BIN}")
endif ()

# For multi-config generators (e.g., MSVC)
set(MULTI_CONFIG "NO")
if (CMAKE_CONFIGURATION_TYPES)
    set(MULTI_CONFIG "YES")
    message(STATUS "Multi-config generator detected, setting per-configuration output directories")
    foreach (cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${cfg} CFG_UPPER)
        set_target_properties(wiser_core PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} ${OUT_LIB}
                LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${OUT_LIB}
                RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${OUT_BIN}
        )
        message(STATUS "  Configuration ${cfg}: ${OUT_BIN}, ${OUT_LIB}")
    endforeach ()
else ()
    message(STATUS "Single-config generator detected")
endif ()

# Install rules (optional)
message(STATUS "Install rules configured")
install(TARGETS wiser_core
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)
if (TARGET wiser)
    install(TARGETS wiser RUNTIME DESTINATION bin)
endif ()
if (TARGET wiser_web)
    install(TARGETS wiser_web RUNTIME DESTINATION bin)
endif ()

# Install web static assets so installed wiser_web can serve ../web
install(DIRECTORY ${CMAKE_SOURCE_DIR}/web/ DESTINATION web
        PATTERN ".git*" EXCLUDE
        PATTERN "node_modules" EXCLUDE)
message(STATUS "Web assets will be installed to web/ directory")

# 构建摘要 - 使用普通变量而不是生成器表达式
message(STATUS "
=== wiser-cpp Build Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Build Targets:")
message(STATUS "  Core Library: wiser_core")
message(STATUS "  Main Executable: wiser")
if (BUILD_WEB_SERVER)
    if (WEB_SERVER_CREATED)
        message(STATUS "  Web Server: ON (target created)")
    else ()
        message(STATUS "  Web Server: ON (target not created - file missing)")
    endif ()
else ()
    message(STATUS "  Web Server: OFF")
endif ()
if (BUILD_DEMO)
    if (DEMO_TARGETS_CREATED)
        message(STATUS "  Demos: ON (targets created)")
    else ()
        message(STATUS "  Demos: ON (no targets created - files missing)")
    endif ()
else ()
    message(STATUS "  Demos: OFF")
endif ()
message(STATUS "")
message(STATUS "Output Directories:")
message(STATUS "  Binaries: ${OUT_BIN}")
message(STATUS "  Libraries: ${OUT_LIB}")
message(STATUS "  Demo Binaries: ${OUT_DEMO_BIN}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  spdlog: ${spdlog_FOUND} (${spdlog_DIR})")
message(STATUS "  fmt: ${fmt_FOUND} (${fmt_DIR})")
message(STATUS "  SQLite3: ${SQLITE3_FOUND} (${SQLITE3_TARGET})")
message(STATUS "  SQLite3 Source: ${SQLITE3_SOURCE}")
message(STATUS "")
if (VCPKG_INFO)
    message(STATUS "Package Manager:")
    message(STATUS "  ${VCPKG_INFO}")
    message(STATUS "")
endif ()
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Multi-config: ${MULTI_CONFIG}")
message(STATUS "===========================================")

message(STATUS "Configuration completed successfully!")