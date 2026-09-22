execute_process(COMMAND "${ASTRA_PROGRAM}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic
  TIMEOUT 10
)
if(result STREQUAL "0" OR NOT diagnostic MATCHES "\"error_code\":1[,}]")
  message(FATAL_ERROR
    "未检测到样本核查程序参数崩溃：result=${result}; stderr=${diagnostic}")
endif()
if(UNIX AND NOT result MATCHES "[Aa]bort")
  message(FATAL_ERROR "样本核查程序没有异常终止：result=${result}")
endif()
message(STATUS "已验证样本核查程序参数错误：result=${result}")
