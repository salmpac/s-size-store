cmake_minimum_required(VERSION 3.25)

project(ssize
    VERSION 0.1.0
    DESCRIPTION "s-size — curated clothing catalog"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SSIZE_BUILD_TESTS "Build unit tests" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# --- dependencies (provided by the nix dev shell / flake) ---
find_package(Boost REQUIRED COMPONENTS fiber context system program_options)
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
        Boost::fiber
        Boost::context
        Boost::system
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
endif()

# --- install / packaging ---
include(GNUInstallDirs)

install(TARGETS ssize ssize-admin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY migrations/ DESTINATION ${CMAKE_INSTALL_DATADIR}/ssize/migrations)
install(DIRECTORY templates/  DESTINATION ${CMAKE_INSTALL_DATADIR}/ssize/templates)
install(DIRECTORY static/     DESTINATION ${CMAKE_INSTALL_DATADIR}/ssize/static)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "ssize")
set(CPACK_PACKAGE_CONTACT "sergeydushenkov@fastforward.pro")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sergey Dushenkov")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)
