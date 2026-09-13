# 主机端 InfoNES 测试程序（核心按 C 编译，和 MCU 固件一致）
CORE   := ../lib/infones
CC     ?= gcc
CFLAGS := -std=gnu11 -O2 -g -w -fno-strict-aliasing -DINFONES_SMALL_RAM -DNES_BG_FLAG=0x0020 -DpAPU_QUALITY=2 -I$(CORE) -I../src/nes
CORE_SRCS := $(CORE)/InfoNES.cpp $(CORE)/InfoNES_Mapper.cpp $(CORE)/InfoNES_pAPU.cpp $(CORE)/K6502.cpp

nes_host: $(CORE_SRCS) nes_host.c ../src/nes/nes_common.c ../src/nes/nes_common.h $(wildcard $(CORE)/*.h $(CORE)/mapper/*.cpp)
	$(CC) $(CFLAGS) -o $@ -x c $(CORE_SRCS) nes_host.c ../src/nes/nes_common.c

clean:
	rm -f nes_host
.PHONY: clean
