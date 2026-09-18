cmake_minimum_required(VERSION 3.25)
project(AstraCodec VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "请使用独立构建目录或 CMake preset")
endif()

include(CTest)
include(cmake/AstraOptions.cmake)
include(cmake/AstraDependencies.cmake)
include(cmake/AstraBuildInfo.cmake)

add_library(astra_core
  core/base/error.cc
  core/base/logger.cc
  core/base/version.cc
)
add_library(astra::core ALIAS astra_core)
target_include_directories(astra_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)
target_link_libraries(astra_core
  PUBLIC astra_options
  PRIVATE PkgConfig::FFMPEG PkgConfig::FMT nlohmann_json::nlohmann_json Threads::Threads
)

add_executable(astracodec apps/astracodec/main.cc)
target_link_libraries(astracodec PRIVATE astra::core)
install(TARGETS astracodec RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  add_executable(astra_foundation_tests
    tests/dependency_test.cc
    tests/error_test.cc
    tests/logger_test.cc
    tests/result_test.cc
    tests/version_test.cc
  )
  target_link_libraries(astra_foundation_tests PRIVATE
    astra::core GTest::gtest_main PkgConfig::FFMPEG
    nlohmann_json::nlohmann_json Threads::Threads
  )
  target_compile_definitions(astra_foundation_tests PRIVATE
    ASTRA_TEST_OUTPUT_ROOT="${CMAKE_CURRENT_BINARY_DIR}/test-output"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-output")
  include(GoogleTest)
  gtest_discover_tests(astra_foundation_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30 LABELS "foundation"
  )
  add_test(NAME cli_version
    COMMAND "${CMAKE_COMMAND}"
      "-DASTRA_PROGRAM=$<TARGET_FILE:astracodec>"
      -DASTRA_MODE=version
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCli.cmake"
  )
  add_test(NAME cli_help
    COMMAND "${CMAKE_COMMAND}"
      "-DASTRA_PROGRAM=$<TARGET_FILE:astracodec>"
      -DASTRA_MODE=help
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCli.cmake"
  )
  set_tests_properties(cli_version cli_help PROPERTIES TIMEOUT 15 LABELS "smoke")
  add_test(NAME cli_invalid_argument
    COMMAND "${CMAKE_COMMAND}"
      "-DASTRA_PROGRAM=$<TARGET_FILE:astracodec>"
      -DASTRA_MODE=invalid
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCliFailure.cmake"
  )
  set_tests_properties(cli_invalid_argument PROPERTIES TIMEOUT 15 LABELS "foundation")
  if(UNIX)
    add_test(NAME cli_closed_stdout
      COMMAND "${CMAKE_COMMAND}"
        "-DASTRA_PROGRAM=$<TARGET_FILE:astracodec>"
        -DASTRA_MODE=closed_stdout
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCliFailure.cmake"
    )
    set_tests_properties(cli_closed_stdout PROPERTIES TIMEOUT 15 LABELS "foundation")
  endif()
endif()

message(STATUS "AstraCodec ${PROJECT_VERSION}; C++20; ${CMAKE_BUILD_TYPE}")
message(STATUS "FFmpeg avformat=${FFMPEG_libavformat_VERSION}, avcodec=${FFMPEG_libavcodec_VERSION}, avutil=${FFMPEG_libavutil_VERSION}")
message(STATUS "Dependency provider=${ASTRA_DEPENDENCY_PROVIDER}; commit=${ASTRA_GIT_COMMIT}; dirty=${ASTRA_GIT_DIRTY}")
