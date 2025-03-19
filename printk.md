# printk musings

Pure Storage carries a number of printk-related patches, mostly
written by me.  We should try to get them upstreamed.  But until that
happens, I can at least describe what they do and why.


# basic design

I like to view printk as an endless stream of bytes.  There are some
newer patches upstream that try to treat things as messages.  I have
no idea how that is supposed to be an improvement, but I see many
downsides and would revert the lot.  So let's stick with a mental
model of a stream of bytes.

Some consumers are asynchronous and can fall behind.  To solve that we
want a prink buffer.  I think pure uses 16MB, larger than the default.
Memory is cheap enough and a larger buffer allows consumers to handle
longer delays without losing messages.

Having a wrap counter is useful.  That way you specify a byte position
in the endless stream.  Low bits of the byte position are an offset in
the printk buffer, high bits are the wrap count.  Now it is easy to
return all messages newer than requested position and also notice gaps
when consumers waiting too long.

Anyway, Linux has something roughly like that since last millennium and
it mostly worked.  Except when it didn't.


# thread interleaving

If you receive two stack traces from two CPUs at the same time, lines
get interleaved and trying to make sense of the resulting mess is
decidedly non-fun.  I added the CPU-number to each logline.  Now you
can grep for one of the CPUs involved and get a clean stacktrace.

Upstream has a similar change that also adds the PID and is somewhat
inconsistent.  Some messages have more decoration than others.  Not a
huge fan of the inconsistency, but the general idea is solid.


# incomplete lines

Printk callers can pass any arguments.  There is no requirement to end
with a newline.  In most cases we print full lines, but the exceptions
are rather painful.  Hexdumps in particular tend to write one hex
number at a time.

When receiving two concurrent hexdumps from two CPUs, the resulting
mess is even less fun than interleaved lines.  Solution is very
simple, I gave each CPU a line-buffer of 1kB.  If there is a newline
anywhere, everything up to the last newline goes to printk proper.
The remainder stays in the line-buffer.

If a single line exceeds 1kB, I flush the line-buffer and add a
newline.  In practice this happens when printing the list of loaded
modules as part of a backtrace.  So far nobody has complained about
the extra newlines.

Upstream introduced `KERN_CONT` to annotate partial lines, then
received a metric shipload of patches to add such annotations.  I have
no idea how that is supposed to solve anything.  But if you urgently
needs more kernel patches to your name to improve your job prospects,
maybe there is some appeal.


# loglevels

Each printk line has an associated log level, a number from 0 to 7.
There are fancy names like `KERN_EMERG` (0) and `KERN_DEBUG` (7) that
give some intuition about the appropriate level to use.

This infrastructure used to be valuable when printk messages were
printed to the VGA console, the same console a human would use to
operate the machine.  Receiving too many kernel messages while trying
to type or read a directory listing is somewhat distracting.
Therefore users can decide to silence all messages with a loglevel
higher than `KERN_ERR` or whatever they prefer.

Syslog captures everything, which makes sense.  Syslog goes to a file
and you can read your directory listing just fine, no matter how many
`KERN_INFO` messages get generated every second.

Which brings us to the weird consoles.


# netconsole

Netconsole is a bit of a hack.  The kernel has infrastructure for
multiple consoles.  Presumably that used to mean multiple terminals
attached to a beefy PDP-11, not entirely sure.  But the infrastructure
exists and netconsole simply gets registered as yet another console.

What netconsole does is send a udp packets to some destination
machine.  Every printk message becomes a udp packet, no exceptions.
That is where having a line-buffer can dramatically reduce the number
of packets being sent.  It also has to potential of completely
overloading your network, a feat I only accomplished once.

There is yet another dangerous edge to netconsole.  Every udp packet
comes with two memory allocations, one each from kmalloc-1024 and
kmalloc-256.  If you manage to printk faster than your network device
can send messages out, there is no limit to how many such allocations
you can make.  Which results in the lovely bug category of
OOM-by-logspam.  Given how long it took us to understand things, I
wonder how many reader will suddenly remember those mysterious OOMs
they could never figure out and...


# cancd

Netconsole needs a receiving machine to listen on a udp port and write
things to a logfile.  Common advice is to use netcat or syslog.  I
would strongly discourage using syslog.  Main problem is that syslog
follows a protocol with a 40+ page specification.  And while many of
the netconsole messages happen to follow the syslog protocol by
accident, the netconsole protocol is better described as wygiwyg -
what you get is what you get.

Even if the sending side mostly follows the syslog protocol, packet
loss and packet reordering can result in the receiving side getting
something very different.

Netcat work, it simply receives bytes.  But it doesn't like to receive
messages from multiple machines on the same port.  And giving each
machine a different port to use for netconsole largely stops working
after a few dozen machines.

Joel Becker created `cancd` to solve that issue.  Basic idea is that
each sending machine writes to a different file and you can use a
single destination port for all machine.

I made several modifications over time to reduce CPU load, handle
logspam and a few more things.  End result is that an old machine that
should have been retired 10 years ago can still handle all the
netconsole messages from thousands of machines.  At this point I
suspect the machine will physically die of old age before we have to
replace it for performance problems.


# blockconsole

Blockconsole is basically a copy of netconsole.  But instead of
sending messages over the network, we send it to a block device.  At
least that was the original design, but adding another physical device
to every machine was rather annoying.

Second attempt technically still write to a block device, but only to
a few specific ranges of blocks.  Which ranges, you ask?  The ranges
used by your filesystem for a particular file, `/var/log/bcon.cur` in
our case.  Which means that effectively blockconsole is writing to a
file.  Except that it never tells the filesystem about it.

Swap files use the same mechanism.  In the beginning we ask the
filesystem for the ranges of blocks used for a particular file, then
we write to those block raw, without interacting with the filesystem.

This becomes important when your primary goal is to capture the kernel
panic messages shortly before the system dies and you want to minimize
the amount of code that could either fail or even misbehave and start
corrupting data.  Blockconsole does call into a single block device
driver, effectively `ahci` these days.  Chances of that actually
working are pretty good.

Both blockconsole and netconsole have about 99% reliability when it
comes to capturing kernel panic messages.  I never bothered to capture
good statistics, but 99% should be pretty close.  Using both should
give about 99.99%, which again roughly matches anecdotal experience.
Double-escapes have happened at least one.  But they are too rare to
seriously worry about.  If the bug that caused the kernel panic
reproduces, you will catch it next time.


# `CON_ALLDATA`

Finally getting back to printk proper, the weird consoles behave a lot
more like syslog than a VGA console.  You can read your directory
listing just fine, no matter how many `KERN_INFO` messages get
generated every second.  But because they use (or abuse) the console
infrastructure, they are subject to the same rules as your VGA console
and likely never show any `KERN_INFO` at all.

My solution is to introduce a new flag for consoles, `CON_ALLDATA`,
and mark the weird consoles that way.  Consoles marked with that flag
ignore loglevels and simply send every message.


# ratelimits

Final problem with printk is logspam.  Once your organization exceeds
two developers, logspam will simply be a reality.  Even if you have
good discipline and carefully test things, you often have hidden
logspam.  The episode when I melted our network?  I had introduced a
spammy logline that depended on a particular piece of hardware and
carefully tested on a machine lacking that hardware.  Oops.

Even harder to test are instances that depend on particular rare
messages being sent or card firmware crashing or similar events that
basically never happen in your testlab.  But one of your customers is
guaranteed to hit that code and you don't want to be on call that day.

Nor do you want to be in charge of playing whack-a-mole and adding
ratelimits to all such loglines you discovered the hard way.  That
approach guarantees job security, but not a lot of happiness.

I decided that every single printk instance has to receive a
ratelimit.  And importantly, every single printk has to receive an
_individual_ ratelimit.

People often misunderstand that point even after I explained it, so
let me try a different approach.  If you have one particular spammy
logline that would fill your /var/log partition in a millisecond, you
want to shut off that line.  And _only_ that _one_ line.  Every other
printk line will continue to be displayed as if nothing happened.

You silence the noisy neighbor and only the noisy neighbor.  For those
living in the US, imagine a magical wand that could turn off all
leafblower noise after 10s, automatically.  The effect on your kernel
logs is about the same.

Since each printk instance is treated individually, I added a
ratelimit state for each instance.  The standard ratelimit
infrastructure in the kernel is quite a mess and the state is 28 byte
or so, a bit too large to use everywhere.  I reduced the state to 4
bytes, 16bit for a counter and 16bit for a timestamp.

As long as the counter is low, you increment the counter and print.
When you hit the limit you print a special "ratelimit in effect now"
message on top, then silence everything for a while.  I think we
picked 300s before we allow a few messages out again.  And we allow a
huge initial burst of messages (30k) before the ratelimit kicks in.

Things like kernel panic can be spammy and people can ask for special
exceptions for debug messages.  That is a dangerous game to play.
Once you allow the first exception, it is hard to keep saying no and
you might as well not have ratelimits anymore.  I strongly suggest not
having any exceptions.  Which then explains the huge initial burst, as
a compromise.

The 16bit timestamp is in seconds and can overflow.  Which means that
we might occasionally get things wrong.  But to get things wrong, you
first have to get ratelimited.  That isn't easy.  Then you have to not
print a single message after your ratelimit expires (300s), until the
counter overflows and you falsely enter a new ratelimit window
(65536s).  If all of the above happens, your logline will be subjected
to an undeserved ratelimit, for another 300s.  Good enough for me.
