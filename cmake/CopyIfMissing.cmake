if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "CopyIfMissing requires SRC and DST")
endif()

# 防御性处理：旧版生成命令可能把双引号作为变量值的一部分传进来。
string(REGEX REPLACE "^\"(.*)\"$" "\\1" SRC "${SRC}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" DST "${DST}")

if(NOT EXISTS "${DST}")
  file(COPY_FILE "${SRC}" "${DST}")
endif()
