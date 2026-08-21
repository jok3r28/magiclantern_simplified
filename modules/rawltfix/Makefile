# EOS 200D lower-bit RAW EDMAC correction module
MODULE_NAME = rawltfix
MODULE_OBJS = $(BUILD_DIR)/rawltfix.o \
              $(BUILD_DIR)/hooks_thumb_200D.o
TOP_DIR = ../..

include $(TOP_DIR)/modules/Makefile
CFLAGS_ARCHLESS = $(filter-out -march=armv7-r -march=armv7-a -march=armv5te,$(CFLAGS))
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

$(BUILD_DIR)/hooks_thumb_200D.o: hooks_thumb_200D.c
	$(CC) $(CFLAGS_ARCHLESS) -mthumb -march=armv7 $(CDEPS_FLAGS) -c -o $@ $<
