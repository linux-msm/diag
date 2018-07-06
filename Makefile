HAVE_LIBUDEV=1

.PHONY: all

DIAG := diag
SEND_DATA := send_data

all: $(DIAG) $(SEND_DATA)

CFLAGS := -Wall -g -O2
ifeq ($(HAVE_LIBUDEV),1)
CFLAGS += -DHAS_LIBUDEV=1
LDFLAGS += -ludev
endif

SRCS := app_cmds.c \
	circ_buf.c \
	common_cmds.c \
	diag.c \
	diag_cntl.c \
	dm.c \
	hdlc.c \
	masks.c \
	mbuf.c \
	peripheral.c \
	router.c \
	socket.c \
	uart.c \
	unix.c \
	usb.c \
	util.c \
	watch.c

ifeq ($(HAVE_LIBUDEV),1)
SRCS += peripheral-rpmsg.c
endif

OBJS := $(SRCS:.c=.o)

$(DIAG): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

SEND_DATA_SRCS := send_data.c
SEND_DATA_OBJS := $(SEND_DATA_SRCS:.c=.o)

$(SEND_DATA): $(SEND_DATA_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(DIAG) $(SEND_DATA)
	install -D -m 755 $(DIAG) $(DESTDIR)$(prefix)/bin/$(DIAG)
	install -D -m 755 $(SEND_DATA) $(DESTDIR)$(prefix)/bin/$(SEND_DATA)

clean:
	rm -f $(DIAG) $(OBJS) $(SEND_DATA) $(SEND_DATA_OBJS)
