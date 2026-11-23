cmake_minimum_required(VERSION 3.16)
project(libsxcftar VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.2.0 REQUIRED)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCONFIGPP REQUIRED IMPORTED_TARGET libconfig++)

add_library(sxcftar STATIC
    src/service/cfgapi/cfg_schema.cpp
    src/service/cfgapi/cfg_tar.cpp
    src/service/cfgapi/cfg_archive.cpp
    src/service/cfgapi/cfg_archive_session.cpp
    src/service/cfgapi/cfg_io.cpp
    src/service/cfgapi/cfg_exposure.cpp
    src/service/cfgapi/cfg_file_lock.cpp
    src/service/cfgapi/cfg_runtime_tree.cpp
    src/service/cfgapi/cfg_pack.cpp
    src/detail/crypto.cpp)
add_library(sxcftar::sxcftar ALIAS sxcftar)
add_library(Smithproxy::CfTar ALIAS sxcftar)
target_compile_features(sxcftar PUBLIC cxx_std_17)
target_include_directories(sxcftar PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(sxcftar PRIVATE LibArchive::LibArchive OpenSSL::Crypto
    Threads::Threads nlohmann_json::nlohmann_json PkgConfig::LIBCONFIGPP)

install(TARGETS sxcftar EXPORT sxcftarTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/service/cfgapi/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/service/cfgapi
    FILES_MATCHING PATTERN "*.hpp")
set(package_dir ${CMAKE_INSTALL_LIBDIR}/cmake/sxcftar)
install(EXPORT sxcftarTargets NAMESPACE sxcftar:: DESTINATION ${package_dir})
configure_package_config_file(cmake/sxcftarConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sxcftarConfig.cmake INSTALL_DESTINATION ${package_dir})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/sxcftarConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sxcftarConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/sxcftarConfigVersion.cmake DESTINATION ${package_dir})

set(sxcftar_tests_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(sxcftar_tests_default ON)
endif()
option(SXCFTAR_BUILD_TESTS "Build CFtar tests" ${sxcftar_tests_default})
if(SXCFTAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
