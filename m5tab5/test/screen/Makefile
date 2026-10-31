# Run the firmware's ui.c on the host and save screenshots. Needs a
# copy of feckless_graphics_handler (an IDF build puts one in
# managed_components/; in a checkout of feckless-libs-for-m5tab5 it is
# components/feckless_graphics_handler); its own host shim compiles
# gfx.c here.
#
#   make -C m5tab5/test/screen GFX=/path/to/feckless_graphics_handler
#   ./screentest out/shot 640,660 ...
#
# SPDX-License-Identifier: MIT

GFX ?= ../../managed_components/feckless_graphics_handler
CC  ?= cc
INC := -Ifake -I$(GFX)/test -I$(GFX)/test/fake -I$(GFX)/include -I$(GFX)/fonts/ark12 -I$(GFX)/fonts/arabixel \
       -I../../main -I../../core -I../../components/qrcodegen -I../../components/quirc
CFLAGS ?= -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE \
          -fsanitize=address,undefined -fno-omit-frame-pointer -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH
SRCS := screentest.c ../../main/ui.c ../../main/tiledraw.c ../../main/tilefaces.c ../../main/qricon.c ../../main/i18n.c ../../main/i18n_tab.c ../../main/guide.c ../../core/umcore.c ../../core/umreplay.c ../../core/umach.c ../../components/qrcodegen/qrcodegen.c \
        ../../main/qrscan.c $(wildcard ../../components/quirc/*.c) \
        $(GFX)/test/shim.c $(GFX)/src/gfx.c $(GFX)/fonts/ark12/ark12.c \
        $(GFX)/fonts/arabixel/arabixel_tab.c $(GFX)/fonts/arabixel/arabixel_shape.c

screentest: $(SRCS) fake/*.h ../../main/*.h ../../core/umcore.h
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRCS) -lm

.PHONY: clean
clean:
	rm -f screentest
