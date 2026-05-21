CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c99

TARGETS = getinodeinfo getinodedata parsedirentry

$(TARGETS): %: %.c ext2common.h ext2fs.h
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGETS)
	bash test.sh

valgrind: $(TARGETS)
	@if [ ! -f testdir/ext2.img ]; then echo "Run 'make test' first"; exit 1; fi
	valgrind --leak-check=full --error-exitcode=1 ./getinodeinfo testdir/ext2.img 2
	valgrind --leak-check=full --error-exitcode=1 ./getinodedata testdir/ext2.img 2 > /dev/null
	valgrind --leak-check=full --error-exitcode=1 ./parsedirentry testdir/checksums.txt > /dev/null
	@echo "Valgrind: OK"

clean:
	rm -f $(TARGETS)

.PHONY: test valgrind clean
