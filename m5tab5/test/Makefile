# Host tests for m5tab5/core: build umtest and compare it with the web
# version. Needs a C compiler and Node.js; no ESP-IDF.
#
#   make -C m5tab5/test          build and check (2000 games, about a minute)
#   make -C m5tab5/test GAMES=200     a quick run
#
# SPDX-License-Identifier: MIT

CC     ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
GAMES  ?= 2000

umtest: umtest.c ../core/umcore.c ../core/umcore.h
	$(CC) $(CFLAGS) -I../core -o $@ umtest.c ../core/umcore.c

tilecheck: tilecheck.c ../main/tilefaces.c ../main/tilefaces.h
	$(CC) $(CFLAGS) -I../main -o $@ tilecheck.c ../main/tilefaces.c

check: umtest tilecheck
	./tilecheck
	./umtest valid
	node check.js ./umtest $(GAMES)

.DEFAULT_GOAL := check
.PHONY: check clean
clean:
	rm -f umtest tilecheck
