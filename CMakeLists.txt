cmake_minimum_required(VERSION 3.22)

# 在 project() 之前指定工具链，这样不用每次在命令行传 -DCMAKE_TOOLCHAIN_FILE
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake"
    CACHE FILEPATH "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(nes-emulator C ASM)

set(CMAKE_C_STANDARD 11)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(NOT EXISTS ${TP}/hal_f4/Src)
    message(FATAL_ERROR "缺少依赖，先运行 ./scripts/fetch-deps.sh")
endif()

# Cortex-M4F：硬浮点，单精度 FPU
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(OPT_FLAGS $<$<CONFIG:Debug>:-Og -g3> $<$<CONFIG:Release>:-O2 -g>)

# ---------------- 第三方：CMSIS + HAL + FatFs ----------------
file(GLOB HAL_SOURCES ${TP}/hal_f4/Src/*.c)
list(FILTER HAL_SOURCES EXCLUDE REGEX "_template\\.c$")

add_library(vendor STATIC
    ${TP}/cmsis_device_f4/Source/Templates/gcc/startup_stm32f407xx.s
    ${TP}/cmsis_device_f4/Source/Templates/system_stm32f4xx.c
    ${HAL_SOURCES}
    ${TP}/fatfs/source/ff.c
    ${TP}/fatfs/source/ffunicode.c
)
target_include_directories(vendor PUBLIC
    src                                   # stm32f4xx_hal_conf.h / ffconf.h
    ${TP}/cmsis_core/Include
    ${TP}/cmsis_device_f4/Include
    ${TP}/hal_f4/Inc
    ${TP}/fatfs/source
)
target_compile_definitions(vendor PUBLIC STM32F407xx USE_HAL_DRIVER)
target_compile_options(vendor PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections -w ${OPT_FLAGS})

# ---------------- 板级驱动 ----------------
add_library(platform STATIC
    src/board.c
    src/lcd.c
    src/sdcard.c
)
target_include_directories(platform PUBLIC src)
target_link_libraries(platform PUBLIC vendor)
target_compile_options(platform PRIVATE ${MCU_FLAGS} -Wall -Wextra -Wshadow
    -ffunction-sections -fdata-sections ${OPT_FLAGS})

set(INFONES_OPT -O2 CACHE STRING "InfoNES 核心的优化选项")
# ---------------- InfoNES 核心（lib/infones，打过补丁，见 lib/infones/PORTING.md）----------------
# 源文件是 .cpp 后缀但内容是 C（上游如此），按 C 编译（工具链里没有 arm-none-eabi-g++）
set(INFONES_SOURCES
    lib/infones/InfoNES.cpp
    lib/infones/InfoNES_Mapper.cpp
    lib/infones/InfoNES_pAPU.cpp
    lib/infones/K6502.cpp
)
set_source_files_properties(${INFONES_SOURCES} PROPERTIES LANGUAGE C)
add_library(infones STATIC ${INFONES_SOURCES})
target_include_directories(infones PUBLIC lib/infones)
target_compile_options(infones PRIVATE ${MCU_FLAGS}
    -include ${CMAKE_CURRENT_SOURCE_DIR}/src/nes/infones_config.h
    ${INFONES_OPT} -g -w -fno-strict-aliasing -ffunction-sections -fdata-sections)   # 模拟器核心 Debug 下也优化

# ---------------- 固件 ----------------
function(add_firmware name)
    # 中断函数和 syscalls 必须直接编进可执行文件：放在静态库里时，启动文件里的弱定义
    # 已经满足了引用，链接器不会去库里取强定义 → SysTick 进 Default_Handler 死循环
    add_executable(${name}.elf ${ARGN}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stm32f4xx_it.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/syscalls.c)
    target_compile_options(${name}.elf PRIVATE ${MCU_FLAGS} -Wall -Wextra
        -ffunction-sections -fdata-sections ${OPT_FLAGS})
    target_link_options(${name}.elf PRIVATE
        ${MCU_FLAGS}
        -T${CMAKE_CURRENT_SOURCE_DIR}/ld/STM32F407VETx.ld
        --specs=nano.specs --specs=nosys.specs
        -Wl,--gc-sections -Wl,--no-warn-rwx-segments
        -Wl,-Map=${name}.map,--cref
        -Wl,--print-memory-usage)
    set_target_properties(${name}.elf PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ld/STM32F407VETx.ld)
    add_custom_command(TARGET ${name}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}.elf> ${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}.elf>)
endfunction()

# NES 模拟器
add_firmware(nes-emulator
    src/app_nes.c
    src/nes/nes_port.c
    src/nes/rom_store.c
    src/nes/input.c
    src/nes/audio.c
    src/nes/filebrowser.c
    src/nes/nes_common.c
)
target_include_directories(nes-emulator.elf PRIVATE src/nes)
target_link_libraries(nes-emulator.elf PRIVATE platform infones)
# 调试开关（cmake -DNES_DEFS="NES_NO_PACING;NES_CRC_FRAMES=\"100,300\"" 之类）
if(NES_DEFS)
    target_compile_definitions(nes-emulator.elf PRIVATE ${NES_DEFS})
endif()

# 外设自检（LCD + SD）
add_firmware(selftest
    src/app_selftest.c
    src/lcd_bench.c
)
target_link_libraries(selftest.elf PRIVATE platform)
