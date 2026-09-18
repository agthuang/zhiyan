CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I../include
SRC = ../src/vk_protocol.c ../src/vk_battery.c ../src/vk_keys.c ../src/vk_hid.c ../src/vk_ui.c ../src/vk_idle.c
PREVIEW_DIR = ../../../../docs/ui-preview

.PHONY: test preview clean

test: test_all
	./test_all

preview: test_all
	mkdir -p $(PREVIEW_DIR)
	./test_all --preview $(PREVIEW_DIR)
	python3 ../../../../tools/ppm_to_png.py $(PREVIEW_DIR)/*.ppm

test_all: test_all.c $(SRC)
	$(CC) $(CFLAGS) -o $@ test_all.c $(SRC)

clean:
	rm -f test_all
