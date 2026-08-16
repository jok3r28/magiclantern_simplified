# EOS 200D lower-bit RAW EDMAC correction
MODULE_NAME = rawltfix
MODULE_OBJS = $(BUILD_DIR)/rawltfix.o \
              $(BUILD_DIR)/hooks_thumb_200D.o
TOP_DIR = ../..

include $(TOP_DIR)/modules/Makefile

# Core module code stays ARM, matching the 200D module-loader-safe pattern.
# Only the firmware entry wrapper is Thumb-2.
CFLAGS_ARCHLESS = $(filter-out -march=armv7-r -march=armv7-a -march=armv5te,$(CFLAGS))
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

$(BUILD_DIR)/hooks_thumb_200D.o: hooks_thumb_200D.c
	$(CC) $(CFLAGS_ARCHLESS) -mthumb -march=armv7 $(CDEPS_FLAGS) -c -o $@ $<
