# Host tests for m5tab5/core: build umtest and compare it with the web
# version. Needs a C compiler and Node.js; no ESP-IDF.
#
#   make -C m5tab5/test          build and check (2000 games, about a minute)
#   make -C m5tab5/test GAMES=200     a quick run
#   make -C m5tab5/test DEALS=5000    more deals checked for solvability
#
# SPDX-License-Identifier: MIT

CC     ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
GAMES  ?= 2000
DEALS  ?= 500

umtest: umtest.c ../core/umcore.c ../core/umcore.h ../core/umreplay.c ../core/umreplay.h ../core/umach.c ../core/umach.h
	$(CC) $(CFLAGS) -I../core -o $@ umtest.c ../core/umcore.c ../core/umreplay.c ../core/umach.c

# main/save.c over an in-memory NVS (fake/)
savecheck: savecheck.c ../main/save.c ../main/save.h ../core/umcore.c ../core/umcore.h ../core/umach.c ../core/umach.h fake/*.h
	$(CC) $(CFLAGS) -Ifake -I../core -I../main -o $@ savecheck.c ../main/save.c ../core/umcore.c ../core/umach.c

guidecheck: guidecheck.c ../main/guide.c ../main/guide.h ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -I../main -o $@ guidecheck.c ../main/guide.c ../core/umcore.c

# includes ../core/umcore.c itself, to read the plan deal() keeps
dealcheck: dealcheck.c ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -o $@ dealcheck.c

# main/qrscan.c and quirc, reading qrcodegen's codes from drawn frames
QUIRC := ../components/quirc
scancheck: scancheck.c ../main/qrscan.c ../main/qrscan.h $(QUIRC)/*.c $(QUIRC)/*.h ../components/qrcodegen/qrcodegen.c
	$(CC) $(CFLAGS) -Wno-sign-compare -Wno-unused-parameter -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH -I../main -I$(QUIRC) -I../components/qrcodegen -o $@ \
	  scancheck.c ../main/qrscan.c $(QUIRC)/decode.c $(QUIRC)/identify.c $(QUIRC)/quirc.c $(QUIRC)/version_db.c ../components/qrcodegen/qrcodegen.c -lm

tilecheck: tilecheck.c ../main/tilefaces.c ../main/tilefaces.h
	$(CC) $(CFLAGS) -I../main -o $@ tilecheck.c ../main/tilefaces.c

check: umtest tilecheck dealcheck guidecheck savecheck scancheck
	./tilecheck
	./scancheck
	./umtest valid
	./dealcheck $(DEALS)
	./savecheck
	node check.js ./umtest $(GAMES) ./guidecheck

.DEFAULT_GOAL := check
.PHONY: check clean
clean:
	rm -f umtest tilecheck dealcheck guidecheck savecheck scancheck
