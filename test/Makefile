# TEST
TEST_SRC = test.c
TEST_BIN = test

# Добавляем каталог include
CFLAGS = -I$(CURDIR)/../include -Wall -Wextra -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS = -lpthread

default: help

b: $(TEST_SRC)
	@echo ""
	@echo "== BUILDING TEST =="
	mkdir -p build
	gcc $(CFLAGS) -o build/$(TEST_BIN) $< $(LDFLAGS)
	chmod +x build/$(TEST_BIN)

c:
	@echo ""
	@echo "== CLEANING TEST =="
	rm -f build/$(TEST_BIN)

# помощь
help: 
	@echo ""
	@echo "== TEST's HELP =="
	@echo "  b  - Build the TEST"
	@echo "  c  - Clean build artifacts"


