if(ASTRA_MODE STREQUAL "invalid")
  execute_process(COMMAND "${ASTRA_PROGRAM}" --unknown
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic
    TIMEOUT 10)
  set(expected_code 1)
elseif(ASTRA_MODE STREQUAL "closed_stdout")
  execute_process(COMMAND /bin/sh -c "exec \"$1\" --version 1>&-" astracodec "${ASTRA_PROGRAM}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic
    TIMEOUT 10)
  set(expected_code 8)
else()
  message(FATAL_ERROR "未知失败验证模式")
endif()
if(result STREQUAL "0" OR NOT diagnostic MATCHES "\"error_code\":${expected_code}[,}]")
  message(FATAL_ERROR "未检测到预期崩溃诊断：result=${result}; stderr=${diagnostic}")
endif()
if(UNIX AND NOT result MATCHES "[Aa]bort")
  message(FATAL_ERROR "程序没有按预期异常终止：result=${result}")
endif()
message(STATUS "已验证 ${ASTRA_MODE}：result=${result}; error_code=${expected_code}")
