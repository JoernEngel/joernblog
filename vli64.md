# vli64

Variable length integers (varint or vli) is a common technique to
effectively compress integer values.
[wikipedia](https://en.wikipedia.org/wiki/Varint) has a decent
introduction, if you don't know it yet.

The concept appears to have been invented many times independently of
all previous such inventions.  Almost everyone uses the form where bit
7 indicates whether additional bytes need to be read.  Totally
obvious.  What bothers me is that you then have to mask off bit 7.

```
	u64 ret = read8(p++);
	if (ret<127) /* no extra bytes */
		return ret;
	ret &= ~128; /* mask off bit 7 */
	ret |= read8(p++) << 7;
	...
```

That masking is entirely unnecessary.  It also leads to the strange
effect that the second encoded byte will never be `0x00`.  While
something like `0x80 0x00` would be a legal encoding for `0`, the
shorter and more obvious encoding would be `0x00`.

So I have come up with an alternative that I like better.  It is
slightly less obvious and afaics nobody else has done the same before.
But it results in slightly faster code and occasionally shorter
encodings.

```
/* returns new output pointer after writing up to 9 bytes */
static void *write_vli64(void *dst, u64 val)
{
	u8 *op = dst;
	while (val>=0x80) {
		*op++ = 0x80|val;
		val >>= 7;
		val -= 1;
	}
	*op++ = val;
	return min((void*)op, dst+9);
}

/* returns new input pointer after reading up to 9 bytes */
static const void *read_vli64(u64 *retval, const void *src)
{
	const u8 *ip = src;
	int s = 0;
	u64 val = 0;
	u8 b;
	do {
		b = *ip++;
		val += (u64)b<<s;
		s += 7;
	} while (b>=0x80 && s<=56);
	*retval = val;
	return ip;
}
```

I call it vli64 for obvious reasons (it's vli and reads/writes u64)
and because the name still seems to be unused.

How does it work?  It adds the high byte to the low byte, like this:
```
  00000000
+        10000000
= 000000010000000
```
Therefore `0x80 0x00` is now encoding the number 128, not 0.  Typical
encoding for 128 would be `0x80 0x01`, but in vli64 that become 256.
We don't have to mask off bit 7, instead we simply add to it.  One
less instruction helps a tiny bit.  We also occasionally save one byte
to encode numbers, most notably for some powers of two (2^15, 2^22,
etc).

If you care about performance, the saved instruction won't help you
very much.  Most of the cost is in the conditional branches.  But it
is possible to remove the branches as well and the saved instruction
will make a difference after that.

Anyway, feel free to use this code if you find it useful.
