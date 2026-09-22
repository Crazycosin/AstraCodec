string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef install_id)
set(prefix "${ASTRA_BINARY_DIR}/test-output/install layout ${install_id}")
if(EXISTS "${prefix}")
  message(FATAL_ERROR "安装测试目录已经存在：${prefix}")
endif()
execute_process(
  COMMAND "${ASTRA_CMAKE_COMMAND}" --install "${ASTRA_BINARY_DIR}"
    --prefix "${prefix}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_diagnostic
  TIMEOUT 20
)
if(NOT install_result STREQUAL "0")
  message(FATAL_ERROR
    "安装布局生成失败：${install_result}\n${install_output}\n${install_diagnostic}")
endif()
set(program
  "${prefix}/bin/astracodec_verify_samples${ASTRA_EXECUTABLE_SUFFIX}")
set(runtime_root "${prefix}/share/astracodec")
execute_process(
  COMMAND "${program}" "${runtime_root}"
  WORKING_DIRECTORY "${ASTRA_BINARY_DIR}/test-output"
  RESULT_VARIABLE verify_result
  OUTPUT_VARIABLE verify_output
  ERROR_VARIABLE verify_diagnostic
  TIMEOUT 20
)
if(NOT verify_result STREQUAL "0")
  message(FATAL_ERROR
    "安装布局样本核查失败：${verify_result}\n${verify_output}\n${verify_diagnostic}")
endif()
if(NOT verify_output MATCHES "S01" OR NOT verify_output MATCHES "S02" OR
   NOT verify_output MATCHES "S03")
  message(FATAL_ERROR "安装布局样本核查输出不完整：${verify_output}")
endif()
message(STATUS "安装布局和三个固定样本核查通过")
