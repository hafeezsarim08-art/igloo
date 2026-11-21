cat << 'EOF' > dizi-root/Makefile
CC      ?= clang
STRIP   ?= strip
CFLAGS  := -O2 -Wall -Wextra
LDFLAGS :=
SRC     := igloo.c
BIN     := igloo
SU_BIN  := su

.PHONY: all cross clean status

all: $(BIN) $(SU_BIN)

$(BIN): $(SRC)
$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(BIN)
$(STRIP) $(BIN)

$(SU_BIN): $(BIN)
cp $(BIN) $(SU_BIN)

cross:
$(MAKE) CC=aarch64-linux-gnu-gcc STRIP=aarch64-linux-gnu-strip CFLAGS="-O2 -Wall -Wextra -static" LDFLAGS="-lutil"

status: $(BIN)
./$(BIN) --status

clean:
rm -f $(BIN) $(SU_BIN)
EOF
