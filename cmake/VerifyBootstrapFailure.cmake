string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef empty_id)
set(empty_packages
  "${ASTRA_BINARY_DIR}/test-output/absent-pkgconfig-${empty_id}")
if(EXISTS "${empty_packages}")
  message(FATAL_ERROR "缺失依赖测试目录已经存在：${empty_packages}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PKG_CONFIG_PATH=" "PKG_CONFIG_LIBDIR=${empty_packages}"
    bash "${ASTRA_SOURCE_DIR}/scripts/bootstrap.sh" debug
  WORKING_DIRECTORY "${ASTRA_SOURCE_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE diagnostic
  TIMEOUT 10
)
if(NOT result STREQUAL "1" OR NOT output STREQUAL "" OR
   NOT diagnostic MATCHES "pkg-config 依赖不可用：libavformat")
  message(FATAL_ERROR
    "缺失依赖的终止状态或诊断不符合要求：${result}\n${output}\n${diagnostic}")
endif()
string(REGEX MATCH "证据目录：([^\r\n]+)" evidence_match "${diagnostic}")
if(NOT evidence_match)
  message(FATAL_ERROR "缺失依赖未提供证据目录：${diagnostic}")
endif()
set(evidence "${CMAKE_MATCH_1}")
if(NOT EXISTS "${evidence}/report.json" OR
   NOT EXISTS "${evidence}/preflight.log")
  message(FATAL_ERROR "缺失依赖的证据文件不存在：${evidence}")
endif()
file(READ "${evidence}/report.json" report)
string(JSON report_status GET "${report}" status)
string(JSON exit_code GET "${report}" exit_code)
string(JSON stage_name GET "${report}" stages 0 name)
string(JSON stage_exit_code GET "${report}" stages 0 exit_code)
if(NOT report_status STREQUAL "failed" OR NOT exit_code EQUAL 1 OR
   NOT stage_name STREQUAL "preflight" OR NOT stage_exit_code EQUAL 1)
  message(FATAL_ERROR "缺失依赖的报告状态不符合要求：${report}")
endif()
message(STATUS "缺失依赖的诊断和失败报告通过")
