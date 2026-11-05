# DisplaceBox - C/raylib port
CC      ?= cc
RAYLIB_DIR ?= ../raylib-6.2_linux_amd64
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -MMD -MP \
           -Isrc -I$(RAYLIB_DIR)/include
LDLIBS  = $(RAYLIB_DIR)/lib/libraylib.a -lGL -lX11 -lm -lpthread -ldl -lrt

SRC     := $(filter-out src/platform_win32.c src/entry_win32.c,$(wildcard src/*.c))
OBJ     := $(SRC:.c=.o)
DEP     := $(OBJ:.o=.d)
TARGET  := displacebox

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

tests/convert_cli: tests/convert_cli.c src/converter.c src/png_io.c src/zlib_codec.c src/platform_common.c src/platform_posix.c
	$(CC) $(CFLAGS) $^ -o $@ -lX11 -lm

test: tests/convert_cli
	python3 tests/compare_with_python.py

clean:
	rm -f $(OBJ) $(DEP) $(TARGET) tests/convert_cli

-include $(DEP)
