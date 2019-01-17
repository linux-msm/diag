HAVE_LIBUDEV=1
HAVE_LIBQRTR=1

.PHONY: all

DIAG := diag-router
SEND_DATA := send_data

all: $(DIAG) $(SEND_DATA)

CFLAGS ?= -Wall -g -O2
ifeq ($(HAVE_LIBUDEV),1)
CFLAGS += -DHAS_LIBUDEV=1
LDFLAGS += -ludev
endif
ifeq ($(HAVE_LIBQRTR),1)
CFLAGS += -DHAS_LIBQRTR=1
LDFLAGS += -lqrtr
endif

SRCS := router/app_cmds.c \
	router/circ_buf.c \
	router/common_cmds.c \
	router/diag.c \
	router/diag_cntl.c \
	router/dm.c \
	router/hdlc.c \
	router/masks.c \
	router/mbuf.c \
	router/peripheral.c \
	router/router.c \
	router/socket.c \
	router/uart.c \
	router/unix.c \
	router/usb.c \
	router/util.c \
	router/watch.c

ifeq ($(HAVE_LIBUDEV),1)
SRCS += router/peripheral-rpmsg.c
endif

ifeq ($(HAVE_LIBQRTR),1)
SRCS += router/peripheral-qrtr.c
endif

OBJS := $(SRCS:.c=.o)

$(DIAG): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

SEND_DATA_SRCS := tools/send_data.c
SEND_DATA_OBJS := $(SEND_DATA_SRCS:.c=.o)

$(SEND_DATA): $(SEND_DATA_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(DIAG) $(SEND_DATA)
	install -D -m 755 $(DIAG) $(DESTDIR)$(prefix)/bin/$(DIAG)
	install -D -m 755 $(SEND_DATA) $(DESTDIR)$(prefix)/bin/$(SEND_DATA)

clean:
	rm -f $(DIAG) $(OBJS) $(SEND_DATA) $(SEND_DATA_OBJS)
