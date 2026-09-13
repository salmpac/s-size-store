cmake_minimum_required(VERSION 3.25)

project(ssize
    VERSION 0.1.0
    DESCRIPTION "s-size — curated clothing catalog"
    LANGUAGES CXX
)

# Language standard and export of compile_commands.json come from the presets
# (CMakePresets.json), so a build configured by hand and one configured by a
# preset cannot drift apart. The flake passes the same values explicitly.

if(NOT DEFINED CMAKE_CXX_STANDARD)
    message(FATAL_ERROR
        "CMAKE_CXX_STANDARD is not set. Configure with a preset "
        "(cmake --preset dev) or pass -DCMAKE_CXX_STANDARD=20.")
endif()

option(SSIZE_BUILD_TESTS "Build unit tests" ON)
option(SSIZE_STATIC "Link everything statically (for the shipped .deb)" OFF)

if(SSIZE_STATIC)
    # Prefer archives over shared objects when resolving find_package.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(Boost_USE_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# --- dependencies (provided by the nix dev shell / flake) ---
# CONFIG mode: FindBoost is gone in CMake 4, and Boost ships its own config.
# boost::system is header-only since 1.69 and no longer a component — it comes
# in through Boost::headers.
cmake_policy(SET CMP0167 NEW)
find_package(Boost 1.83 CONFIG REQUIRED COMPONENTS fiber context program_options)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# inja ships a CMake config in nixpkgs; fall back to a header-only interface.
find_package(inja CONFIG QUIET)
if(NOT inja_FOUND)
    add_library(ssize_inja INTERFACE)
    find_path(INJA_INCLUDE_DIR inja/inja.hpp REQUIRED)
    target_include_directories(ssize_inja INTERFACE ${INJA_INCLUDE_DIR})
    add_library(pantor::inja ALIAS ssize_inja)
endif()

# --- core library: everything except main() ---
add_library(ssize_core STATIC
    src/config.cpp
    src/models.cpp
    src/db.cpp
    src/migrations.cpp
    src/links.cpp
    src/catalog.cpp
    src/events.cpp
    src/db_worker.cpp
    src/templating.cpp
    src/handlers.cpp
    src/http_server.cpp
)

target_include_directories(ssize_core PUBLIC src)

target_link_libraries(ssize_core
    PUBLIC
        Boost::headers
        Boost::fiber
        Boost::context
        SQLite::SQLite3
        PkgConfig::SODIUM
        nlohmann_json::nlohmann_json
        pantor::inja
        spdlog::spdlog
        tomlplusplus::tomlplusplus
        Threads::Threads
)

# --- binaries ---
add_executable(ssize src/main.cpp)
target_link_libraries(ssize PRIVATE ssize_core Boost::program_options)

add_executable(ssize-admin src/admin_cli.cpp)
target_link_libraries(ssize-admin PRIVATE ssize_core Boost::program_options)

# --- tests ---
if(SSIZE_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
    target_compile_definitions(ssize_tests PRIVATE SSIZE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# --- install / packaging ---
# Destinations are spelled out rather than taken from GNUInstallDirs: nixpkgs
# makes those absolute (/nix/store/...), which lands the binaries outside
# /usr/bin in the .deb.
install(TARGETS ssize ssize-admin RUNTIME DESTINATION bin)
install(DIRECTORY migrations/ DESTINATION share/ssize/migrations)
install(DIRECTORY templates/  DESTINATION share/ssize/templates)
install(DIRECTORY static/     DESTINATION share/ssize/static)
install(FILES config.example.toml DESTINATION share/ssize)
install(FILES deploy/ssize.service DESTINATION lib/systemd/system)
install(FILES deploy/nginx-ssize.conf.snippet DESTINATION share/ssize)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "ssize")
set(CPACK_PACKAGE_CONTACT "sergeydushenkov@fastforward.pro")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sergey Dushenkov")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
# A static binary carries its libraries, so there is nothing for shlibdeps to
# resolve. It is also the only thing that works here: a dynamically linked
# build points at /nix/store paths that do not exist on the target server.
if(SSIZE_STATIC)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
else()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    message(STATUS "Non-static build: the .deb is for local testing only")
endif()
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)
