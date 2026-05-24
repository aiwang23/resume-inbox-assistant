cmake_minimum_required(VERSION 3.20)

project(resume-inbox-assistant C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- curl config ----------------

set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 不构建 curl 文档，避免干净系统缺 Perl 报警告
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF)

if (WIN32)
    message(STATUS "curl TLS backend: Schannel")
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
else ()
    message(STATUS "curl TLS backend: OpenSSL")
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif ()

# ---------------- third party ----------------

add_subdirectory(third_party/curl)
add_subdirectory(third_party/json)
# set(SQLITE_BUILD_SHELL ON CACHE BOOL "Build sqlite3 command line shell")
add_subdirectory(third_party/sqlite3-cmake)
add_subdirectory(third_party/sqlite_orm)
add_subdirectory(third_party/cxxopts)

add_subdirectory(src/mail_fetcher)
add_subdirectory(src/mail_parser/)

# ---------------- app ----------------

add_executable(resume-inbox-assistant
        main.cpp
)

target_link_libraries(resume-inbox-assistant PUBLIC
        mail_fetcher
        mail_parser

        CURL::libcurl
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
        sqlite_orm::sqlite_orm
        cxxopts::cxxopts
)