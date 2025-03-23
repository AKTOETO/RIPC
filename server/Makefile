# Сервер
SERVER_SRC = server.c
SERVER_BIN = server

# Добавляем каталог include
CFLAGS = -I$(CURDIR)/../include -Wall -Wextra

default: help

b: $(SERVER_SRC)
	@echo ""
	@echo "== BUILDING SERVER =="
	mkdir -p build
	gcc $(CFLAGS) -o build/$(SERVER_BIN) $<
	chmod +x build/$(SERVER_BIN)

c:
	@echo ""
	@echo "== CLEANING SERVER =="
	rm -f build/$(SERVER_BIN)

# помощь
help: 
	@echo ""
	@echo "== SERVER's HELP =="
	@echo "  b  - Build the server"
	@echo "  c  - Clean build artifacts"


