# 裸机交叉编译工具链定义
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# 交叉编译时不要去链接一个可执行文件做探测（没有 libc 启动代码会失败）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER        arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER      arm-none-eabi-gcc)
set(CMAKE_OBJCOPY           arm-none-eabi-objcopy)
set(CMAKE_SIZE              arm-none-eabi-size)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
