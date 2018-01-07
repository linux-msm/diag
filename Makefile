DIAG := diag

CFLAGS := -Wall -g -O2
LDFLAGS := -ludev

SRCS := diag.c \
	diag_cntl.c \
	hdlc.c \
	mbuf.c \
	peripheral.c \
	router.c \
	socket.c \
	uart.c \
	usb.c \
	util.c \
	watch.c

OBJS := $(SRCS:.c=.o)

$(DIAG): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(DIAG)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<

clean:
	rm -f $(DIAG) $(OBJS)
