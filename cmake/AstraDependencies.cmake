find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
find_package(Threads REQUIRED)
pkg_check_modules(FMT REQUIRED IMPORTED_TARGET fmt>=9)

set(ASTRA_DEPENDENCY_PROVIDER "SYSTEM" CACHE STRING "SYSTEM/FETCH")
set_property(CACHE ASTRA_DEPENDENCY_PROVIDER PROPERTY STRINGS SYSTEM FETCH)
if(ASTRA_DEPENDENCY_PROVIDER STREQUAL "SYSTEM")
  find_package(nlohmann_json 3.12.0 EXACT CONFIG REQUIRED)
  if(BUILD_TESTING)
    find_package(GTest 1.17.0 EXACT CONFIG REQUIRED)
  endif()
elseif(ASTRA_DEPENDENCY_PROVIDER STREQUAL "FETCH")
  include(FetchContent)
  set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  set(json_archive "${CMAKE_SOURCE_DIR}/.cache/dependencies/json-v3.12.0.tar.gz")
  if(EXISTS "${json_archive}")
    set(json_url "${json_archive}")
  else()
    set(json_url "https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0")
  endif()
  FetchContent_Declare(nlohmann_json SYSTEM
    URL "${json_url}"
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
  if(BUILD_TESTING)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(gtest_archive "${CMAKE_SOURCE_DIR}/.cache/dependencies/googletest-v1.17.0.tar.gz")
    if(EXISTS "${gtest_archive}")
      set(gtest_url "${gtest_archive}")
    else()
      set(gtest_url "https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0")
    endif()
    FetchContent_Declare(googletest SYSTEM
      URL "${gtest_url}"
      URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
  endif()
else()
  message(FATAL_ERROR "未知 ASTRA_DEPENDENCY_PROVIDER=${ASTRA_DEPENDENCY_PROVIDER}")
endif()
