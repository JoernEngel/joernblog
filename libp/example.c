#include <stddef.h>
#include <stdio.h>

#include <p.h>

static u64 dot_product(u8 *a, u8 *b, size_t n)
{
	u64 acc = 0;
	while (n>=VECTORLEN) {
		u16v a0, a1;
		u16v b0, b1;
		widen_u8v(&a0, &a1, readv(a));
		widen_u8v(&b0, &b1, readv(b));
		acc += hsum_u16v(a0*b0);
		acc += hsum_u16v(a1*b1);
		a += VECTORLEN;
		b += VECTORLEN;
		n -= VECTORLEN;
	}
	/* tail handling left out */
	return acc;
}

int main(void)
{
	u8 a[256];
	u8 b[256];
	u64 expected = 0;
	for (int i=0; i<256; i++) {
		a[i] = i;
		b[i] = i;
		expected += i*i;
	}
	printf("product  = %llx\n", dot_product(a, b, 256));
	printf("expected = %llx\n", expected);
	return 0;
}
