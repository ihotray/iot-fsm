PROG ?= iot-fsm
DEFS ?= -llua -liot-base-nossl -liot-json
EXTRA_CFLAGS ?= -Wall -Werror
CFLAGS += $(DEFS) $(EXTRA_CFLAGS)

SRCS = main.c fsm.c

all: $(PROG)

$(PROG):
	$(CC) $(SRCS) $(CFLAGS) -o $@


clean:
	rm -rf $(PROG) *.o
