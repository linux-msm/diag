HAVE_LIBUDEV=1

DIAG := diag

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
	usb.c \
	util.c \
	watch.c

ifeq ($(HAVE_LIBUDEV),1)
SRCS += peripheral-rpmsg.c
endif

OBJS := $(SRCS:.c=.o)

$(DIAG): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(DIAG)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<

clean:
	rm -f $(DIAG) $(OBJS)
