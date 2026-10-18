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

umtest: umtest.c ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -o $@ umtest.c ../core/umcore.c

guidecheck: guidecheck.c ../main/guide.c ../main/guide.h ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -I../main -o $@ guidecheck.c ../main/guide.c ../core/umcore.c

# includes ../core/umcore.c itself, to read the plan deal() keeps
dealcheck: dealcheck.c ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -o $@ dealcheck.c

tilecheck: tilecheck.c ../main/tilefaces.c ../main/tilefaces.h
	$(CC) $(CFLAGS) -I../main -o $@ tilecheck.c ../main/tilefaces.c

check: umtest tilecheck dealcheck guidecheck
	./tilecheck
	./umtest valid
	./dealcheck $(DEALS)
	node check.js ./umtest $(GAMES) ./guidecheck

.DEFAULT_GOAL := check
.PHONY: check clean
clean:
	rm -f umtest tilecheck dealcheck guidecheck
