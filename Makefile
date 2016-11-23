DIAG := diag

CFLAGS := -Wall -g -O2
LDFLAGS :=

SRCS := crc_ccitt.c diag.c diag_cntl.c  mbuf.c util.c watch.c
OBJS := $(SRCS:.c=.o)

$(DIAG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

install: $(DIAG)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<

clean:
	rm -f $(DIAG) $(OBJS)
