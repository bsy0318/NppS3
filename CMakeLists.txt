# NppS3 — Notepad++ plugin for S3-compatible object storage
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)
project(NppS3 VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "NppS3 targets Windows/MSVC (Visual Studio 2022).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT: a plugin DLL must not depend on a redistributable being present.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
add_compile_options(/W4 /utf-8 /EHsc /permissive- /Zc:__cplusplus)

# ---------------------------------------------------------------- third party
add_library(tinyxml2 STATIC third_party/tinyxml2/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC third_party/tinyxml2)
# tinyxml2 is third-party; do not fail its build on our warning level.
target_compile_options(tinyxml2 PRIVATE /W3)

# ---------------------------------------------------------------- core library
# Everything testable without the Notepad++ host.
add_library(npps3_core STATIC
    src/util/StringUtil.cpp
    src/util/Hash.cpp
    src/util/Mime.cpp
    src/util/Log.cpp
    src/storage/SigV4.cpp
    src/storage/HttpClient.cpp
    src/storage/S3Xml.cpp
    src/storage/S3Client.cpp
    src/security/CredentialStore.cpp
    src/config/ProfileManager.cpp
    src/cache/CacheManager.cpp
    src/documents/RemoteDocumentManager.cpp
    src/transfer/TransferManager.cpp
)
target_include_directories(npps3_core PUBLIC src)
target_link_libraries(npps3_core PUBLIC tinyxml2)

# ---------------------------------------------------------------- plugin DLL
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PluginMain.cpp")
    add_library(NppS3 SHARED
        src/plugin/PluginMain.cpp
        src/plugin/NppS3Plugin.cpp
        src/ui/I18n.cpp
        src/ui/DockPanel.cpp
        src/ui/Dialogs.cpp
        src/ui/NppS3.rc
    )
    target_link_libraries(NppS3 PRIVATE npps3_core)
    target_include_directories(NppS3 PRIVATE src)
    set_target_properties(NppS3 PROPERTIES
        OUTPUT_NAME "NppS3"
        PREFIX ""
    )
endif()

# ---------------------------------------------------------------- tests
enable_testing()

add_executable(npps3_tests
    tests/unit/test_main.cpp
    tests/unit/test_stringutil.cpp
    tests/unit/test_hash_sigv4.cpp
    tests/unit/test_mime.cpp
    tests/unit/test_s3xml.cpp
    tests/unit/test_profiles.cpp
    tests/unit/test_cache.cpp
    tests/unit/test_documents.cpp
)
target_link_libraries(npps3_tests PRIVATE npps3_core)
target_include_directories(npps3_tests PRIVATE third_party/doctest src)
add_test(NAME unit COMMAND npps3_tests)

# Console runner performing real Cloudflare R2 integration tests using
# NPPS3_TEST_* environment variables (or a local .env, never committed).
add_executable(npps3_integration
    tests/integration/integration_main.cpp
)
target_link_libraries(npps3_integration PRIVATE npps3_core)
target_include_directories(npps3_integration PRIVATE src)
