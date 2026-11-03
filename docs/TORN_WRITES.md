# Torn writes under SIGKILL: measurements

TielDB writes 8 KB pages with `pwrite`. This document records what happens to such a write when the
process is killed with `SIGKILL` (`kill -9`) in the middle of it, on several file systems and Linux
kernels, and why that says little about a power loss. Everything here was measured; none of it is
documented kernel behavior, and the explanations are inferences. It matters for the design of the
crash tests (`kill -9`) and for the torn page protection of Phase 5 (see `ROADMAP.md`).

## Method

A child process opens a file and writes one block of N bytes at offset 0 in a loop, every byte of
the block set to the same value, a different value each time. The parent sleeps for a random time,
sends `SIGKILL`, waits for the child, reads the block back and calls it *torn* if its bytes are not
all equal. A shared-memory flag, set by the child around the `pwrite`, tells how many kills landed
inside the syscall (80% to 99% in all runs). The source is at the end of this file.

The runs: the development VM (Linux 7.2, OrbStack on a MacBook; ext2, ext4, xfs on loop-mounted
images, btrfs and tmpfs), GitHub-hosted runners (Ubuntu 22.04 with Linux 6.8, Ubuntu 24.04/26.04
with Linux 7.0), older distributions booted in QEMU with KVM (Ubuntu 18.04, 20.04, 22.04 and Debian
12 with their own kernels), and exact upstream kernels from the Ubuntu mainline archive booted the
same way (Linux 6.1 to 6.18).

## Buffered 8 KB writes, by upstream kernel release

Torn blocks out of 3000 kills. tmpfs was also run every time and was torn in 24% to 37% of the kills.

| Kernel | ext4 | xfs |
|---|---|---|
| 6.1 | 982 | 712 |
| 6.2 | 870 | 673 |
| 6.3 | 1083 | 696 |
| 6.4 | 875 | 754 |
| 6.5 | 831 | 687 |
| 6.6 | 969 | 681 |
| 6.7 | 983 | 778 |
| 6.8 | 936 | 787 |
| 6.9 | 983 | **0** |
| 6.10 | 909 | 0 |
| 6.11 | 805 | 0 |
| 6.12 | 818 | 0 |
| 6.13 | 886 | 0 |
| 6.14 | 967 | 0 |
| 6.15 | 1067 | 0 |
| 6.16 | **0** | 0 |
| 6.17 | 0 | 0 |
| 6.18 | 0 | 0 |

So an 8 KB page was no longer torn on xfs from **6.9** and on ext4 from **6.16**. Both file systems
are still torn by larger writes (below). The ext4 change fits what I recall about large folio
support for regular files arriving in 6.16, but I have not checked it in the kernel source, and I
have not identified the commit for either file system (that would need a build and bisect between
6.8 and 6.9, and between 6.15 and 6.16).

Distribution kernels (also 3000 kills each):

| Kernel | tmpfs | ext4 | xfs |
|---|---|---|---|
| 4.15 (Ubuntu 18.04) | 908 | 1218 | 353 |
| 5.4 (Ubuntu 20.04) | 696 | 746 | 575 |
| 5.15 (Ubuntu 22.04) | 765 | 828 | 743 |
| 6.1 (Debian 12) | 1114 | 928 | 683 |
| 6.8.0-azure (GitHub Ubuntu 22.04 runner) | 838 | 801 to 874 | **0** |
| 7.0.0-azure (GitHub Ubuntu 24.04/26.04 runner) | 543 to 770 | 0 | 0 |
| 7.2 (development VM) | about 930 | 0 | 0 |

The Ubuntu 6.8 kernel does not tear xfs, while upstream 6.8 does (787 of 3000): the distribution
backports changes, so the distribution matters as much as the version number. btrfs and ext2 were
tried on 7.2 only, and neither tore an 8 KB write (0 of 4000 and 0 of 3000).

## Larger buffered writes

Writes of 32 KB tore on every file system on every kernel up to 6.8 (for example xfs on 5.15: 1898
of 3000). Writes of 4 MB and more were torn on ext4 and xfs on **every kernel tried, including the
newest**: at 4 MB, ext4 tore 112 to 309 of 400 kills and xfs 72 to 301 of 400. On 7.2 the tear was
always at a multiple of 2 MB (4 MB: about a quarter of the kills; 16 MB: about two thirds); the
positions were not recorded on the other kernels. So the newer kernels did not make buffered writes
atomic against a kill; they made the copy steps larger.

## Why: an explanation (an inference)

A buffered `write` copies the data into the page cache in pieces, one folio at a time. A *folio* is
a group of 1 to 512 contiguous pages (up to 2 MB) that the kernel's memory manager treats as one
unit; before large folios the unit was a 4 KB page. Between two pieces the kernel checks whether a
fatal signal is pending (`generic_perform_write` in `mm/filemap.c`), and if so it stops and
returns a short count; a process that is being killed never sees it, so the block stays half
written. An 8 KB write is therefore safe from a kill only if it fits in one piece, which is the case
once the file system uses folios of 8 KB or more for it. That fits the data: tmpfs always copies one
page at a time and tears at byte 4096; xfs and ext4 stop tearing 8 KB when they start to use large
folios; and for writes above 2 MB the tear is at a multiple of 2 MB, the largest folio size.

## With `O_DIRECT`

With `O_DIRECT` and 4096-aligned buffers (and a direct read for the check), nothing was torn on
ext4, xfs, ext2 and btrfs on Linux 7.2: 0 of 3000 for 8 KB (94% to 99% of the kills inside the
`pwrite`), 0 of 3000 for 32 KB on ext4 and xfs, 0 of 400 for 4 MB, 0 of 200 for 16 MB. tmpfs accepts
`O_DIRECT` but keeps its own cache, copies page by page and still tore 940 of 3000 8 KB writes.

An explanation (an inference): a direct write does not copy into the page cache. The kernel pins
the pages of the buffer and submits a block I/O request to the device, with no copy loop and no
signal check between pieces; the call returns when the request completes, and a `SIGKILL` does not
cancel an I/O that has been submitted. That is also why folios do not matter here: they set the size
of the copy steps, and there are none. Only Linux 7.2 was tested with `O_DIRECT`.

## Why this is about `SIGKILL` only; a power loss is a different story

When a process is killed, the operating system keeps running. Whatever the copy loop had put into the
page cache stays there, the kernel writes it to disk later in the usual way, and a flush
(`fsync`) that completed before the kill covers it. The tear exists only because the *copy* was cut
short, so the question is how big the copy steps are. The same applies to a process that dies for
another reason (an out-of-memory kill is a `SIGKILL`).

A power loss or a kernel crash loses everything that is only in memory: the page cache and the
write cache of the drive. What survives is what had been written back *and* flushed, and what is
torn depends on other layers, none of which these experiments exercise:

- **Writeback.** The page cache writes dirty pages to the device in its own order and units, so an
  8 KB page that is in the cache as one folio can still reach the disk as two 4 KB bios, or be split
  at the limits of the device, and a power loss can come between them. Large folios make the *copy*
  atomic against a signal, not the *writeback* atomic against a power loss.
- **The device.** Atomicity is promised only per sector (512 B or 4 KB); some NVMe drives advertise a
  larger atomic write unit, and Linux has `RWF_ATOMIC` for direct writes where the hardware allows
  it. A direct write is not atomic at 8 KB either, unless one of these applies.
- **The file system.** Journaling (for example ext4 `data=ordered`) orders metadata against data,
  but does not make a data write atomic.
- **Flush semantics.** Whether a flush is honored all the way to the medium depends on the stack,
  and a virtual disk adds the host's behavior. A VM cannot test a power loss, since the host's cache
  decides.

So a result like "xfs on 6.9 or later does not tear an 8 KB write on a kill" is no reason to skip
torn page protection: it must assume that a power loss tears pages on every file system and kernel,
including the newest ones (the Phase 5 task lists the designs). The kill findings are useful for
two things: the crash tests, which use `kill -9` and must not depend on the file system (see the two
crash test tasks in `ROADMAP.md`), and for knowing which kernels turn a plain process crash into a
torn page.

## Caveats

- Absence of tears in 3000 kills is strong evidence against a tear rate of a few percent, and no
  evidence that a tear is impossible.
- The mainline kernels are Ubuntu's builds of upstream releases (`.0` releases, generic config)
  booted in a QEMU guest with an Ubuntu 22.04 userland; the file systems were loop-mounted images of
  512 MB. Other configurations may differ.
- Only 8 KB, 32 KB, 4 MB and 16 MB were measured, and only offset 0 of a file that already had
  contents.
- The development VM has 512-byte logical sectors; devices with 4096-byte sectors were not tested.

## The test program (buffered version)

Compile with `cc -O1 -std=gnu11 tear.c -o tear`, run as `./tear <file> <trials> <bytes>
<max_delay_us>`, with the file on the file system to test. The `O_DIRECT` version opens the file
with `O_RDWR | O_DIRECT`, uses buffers from `aligned_alloc(4096, size)`, and reads the result back
with `O_RDONLY | O_DIRECT`.

```c
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int SIZE = 8192;

static void child(const char *path, volatile int *in_write)
{
    int fd = open(path, O_RDWR);
    unsigned char *buf = malloc((size_t) SIZE);
    for (unsigned char value = 1;; value = (unsigned char) (value % 250 + 1)) {
        memset(buf, value, (size_t) SIZE);
        *in_write = 1;
        ssize_t n = pwrite(fd, buf, (size_t) SIZE, 0);
        *in_write = 0;
        if (n != SIZE) { _exit(1); }
    }
}

int main(int argc, char **argv)
{
    const char *path = argv[1];
    int trials = atoi(argv[2]);
    SIZE = atoi(argv[3]);
    int max_delay_us = atoi(argv[4]);
    volatile int *in_write = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int hit = 0, torn = 0, torn_when_hit = 0;
    srand(777);
    for (int t = 0; t < trials; t++) {
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        unsigned char *init = calloc(1, (size_t) SIZE);
        (void) !pwrite(fd, init, (size_t) SIZE, 0);
        close(fd);
        free(init);
        *in_write = 0;
        pid_t pid = fork();
        if (pid == 0) { child(path, in_write); }
        struct timespec delay = {0, 20000L + ((long) (rand() % max_delay_us) * 1000L)};
        nanosleep(&delay, NULL);
        int was_in_write = *in_write;
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        fd = open(path, O_RDONLY);
        unsigned char *got = malloc((size_t) SIZE);
        (void) !pread(fd, got, (size_t) SIZE, 0);
        close(fd);
        int mixed = 0;
        for (int i = 1; i < SIZE; i++) { if (got[i] != got[0]) { mixed = 1; break; } }
        free(got);
        hit += was_in_write;
        torn += mixed;
        torn_when_hit += (mixed && was_in_write);
    }
    printf("%-28s size=%-8d trials=%d  in-write-at-kill=%d (%.0f%%)  torn=%d  torn-while-in-write=%d\n", path, SIZE, trials, hit, 100.0 * hit / trials, torn, torn_when_hit);
    unlink(path);
    return 0;
}
```
