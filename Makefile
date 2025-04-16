BIN = rpc
SRC = main.c rpc.c
BUILD_DIR = build

CC     = gcc
FLAGS  += -O3 -pipe -Wall -Wextra -Wno-unused-parameter -ggdb3
DEFINE += -DLINUX -D_GNU_SOURCE -D__USE_MISC
INCLUDE = -I. -I/usr/include/
CFLAGS  += $(FLAGS) $(INCLUDE) $(DEFINE)
LDFLAGS += -L/usr/local/lib
LDLIBS  = -lc

# Create build objects with path
OBJ = $(addprefix $(BUILD_DIR)/,$(SRC:.c=.o))

all: dirs $(BUILD_DIR)/$(BIN)

# Create build directory
dirs:
	@mkdir -p $(BUILD_DIR)

static: CFLAGS += -static
static: dirs $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $(BUILD_DIR)/$(BIN)_static

$(BUILD_DIR)/$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@

# Pattern rule for object files
$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean static dirs