# NppS3 — Notepad++ plugin for S3-compatible object storage
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)
project(NppS3 VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "NppS3 targets Windows/MSVC (Visual Studio 2022 or newer).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT: a plugin DLL must not depend on a redistributable being present.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
add_compile_options(/W4 /utf-8 /EHsc /permissive- /Zc:__cplusplus)

# ------------------------------------------------------------------ platform
# The plugin must match the Notepad++ process it is loaded into, so the same
# tree builds for x64, x86 (Win32) and ARM64. NPPS3_ARCH names the build for
# packaging; it follows the Notepad++ download naming.
if(CMAKE_GENERATOR_PLATFORM)
    set(_npps3_platform "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(_npps3_platform "${CMAKE_VS_PLATFORM_NAME}")
endif()
string(TOLOWER "${_npps3_platform}" _npps3_platform_lower)

if(_npps3_platform_lower STREQUAL "win32" OR _npps3_platform_lower STREQUAL "x86")
    set(NPPS3_ARCH "x86")
elseif(_npps3_platform_lower STREQUAL "arm64")
    set(NPPS3_ARCH "arm64")
elseif(_npps3_platform_lower STREQUAL "x64")
    set(NPPS3_ARCH "x64")
else()
    # Ninja/NMake builds inherit the architecture of the active MSVC toolchain.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(NPPS3_ARCH "x86")
    else()
        set(NPPS3_ARCH "x64")
    endif()
endif()
message(STATUS "NppS3 target architecture: ${NPPS3_ARCH}")

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
    src/storage/MultipartUpload.cpp
    src/storage/UploadJournal.cpp
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
        src/ui/ErrorText.cpp
        src/ui/Icons.cpp
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
    tests/unit/test_multipart.cpp
    tests/unit/test_errortext.cpp
    src/ui/I18n.cpp
    src/ui/ErrorText.cpp
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

# Helper CLI for host-level GUI verification scripts.
add_executable(npps3_s3cli tests/hosttest/s3cli.cpp)
target_link_libraries(npps3_s3cli PRIVATE npps3_core)
target_include_directories(npps3_s3cli PRIVATE src)
