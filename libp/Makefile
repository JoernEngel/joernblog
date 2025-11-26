CFLAGS	+= -fno-omit-frame-pointer
CFLAGS	+= -fPIC
CFLAGS	+= -g
CFLAGS	+= -I.
CFLAGS	+= -O2
CFLAGS	+= -std=gnu11
CFLAGS	+= -Wall
CFLAGS	+= -Wcast-qual
CFLAGS	+= -Werror
CFLAGS	+= -Wextra
CFLAGS	+= -Wmissing-declarations
CFLAGS	+= -Wmissing-prototypes
CFLAGS	+= -Wno-pointer-arith
CFLAGS	+= -Wshadow
CFLAGS	+= -Wstrict-prototypes

.PHONY: example
example: example_avx512 example_avx2 example_sse42

example_avx512: example.c p*.h
	$(CC) $(CFLAGS) $< -o $@ -march=icelake-client -mtune=icelake-client

example_avx2: example.c p*.h
	$(CC) $(CFLAGS) $< -o $@ -march=haswell -mtune=haswell

example_sse42: example.c p*.h
	$(CC) $(CFLAGS) $< -o $@ -march=nehalem -mtune=nehalem

clean:
	$(RM) example_avx512 example_avx2 example_sse42
