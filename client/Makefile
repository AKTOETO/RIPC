# Клиент
CLIENT_SRC = client.c
CLIENT_BIN = client

# Добавляем каталог include
CFLAGS = -I$(CURDIR)/../include -Wall -Wextra

default: help

b: $(CLIENT_SRC)
	@echo ""
	@echo "== BUILDING CLIENT =="
	mkdir -p build
	gcc $(CFLAGS) -o build/$(CLIENT_BIN) $<
	chmod +x build/$(CLIENT_BIN)

c:
	@echo ""
	@echo "== CLEANING CLIENT =="
	rm -f build/$(CLIENT_BIN)

# помощь
help: 
	@echo ""
	@echo "== CLIENT's HELP =="
	@echo "  b  - Build the client"
	@echo "  c  - Clean build artifacts"


