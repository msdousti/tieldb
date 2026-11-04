# Torn writes: what a kill and a power loss do to an 8 KB write

TielDB writes 8 KB pages with `pwrite`. This document records what happens to such a write when the
process is killed with `SIGKILL` (`kill -9`) in the middle of it, on several file systems and Linux
kernels; what the kernel sources say about it; why none of that tells much about a power loss; and
how a power loss can be emulated, with existing tools and with our own I/O layer. It matters for the
design of the crash tests and for the torn page protection of Phase 5 (see `ROADMAP.md`).

## How to read this document

Three kinds of statements are mixed here, and each is marked:

- **Measured**: the numbers come from the experiments below (method and program included), and
  nowhere else. No document gives them, and they depend on the kernel build, so they are
  observations and not guarantees.
- **Documented**: stated in a kernel source that I read, with a link: the pull request that
  introduced large folios for ext4 regular files in Linux 6.16, the kernel test robot report on it,
  a commit message about how the buffered write loop treats a fatal signal, and mailing list threads
  about the power loss test tools. Everything that is not linked was not checked against a source.
- **Inferred**: my explanation that connects the two, marked *(an inference)*. The kernel sources
  confirm the release in which ext4 got large folios and the existence of a signal check in the
  write loop; that these are *why* writes tear is an inference that fits all the measurements.

## Findings

1. A `SIGKILL` in the middle of a buffered `pwrite` can leave the block half written. On tmpfs an
   8 KB block was torn at byte 4096 in 24% to 37% of the kills on every kernel tried. *(measured)*
2. Whether an 8 KB block survives depends on the kernel version and the file system. Upstream
   releases: on **xfs** it tears up to Linux 6.8 and not from **6.9**; on **ext4** it tears up to
   6.15 and not from **6.16**. *(measured)* The ext4 release matches the pull request that enabled
   large folios for regular files in 6.16. *(documented)* The commit that changed xfs was not
   identified.
3. Distribution kernels differ from upstream: the Ubuntu 6.8 kernel did not tear xfs, while upstream
   6.8 does, so the distribution matters as much as the version number. *(measured)*
4. Larger buffered writes tear on every kernel tried, including 6.18: a 4 MB write was torn in 18% to
   77% of the kills on ext4 and xfs; on Linux 7.2 always at a multiple of 2 MB. *(measured)* So the
   newer kernels did not make buffered writes atomic against a kill; they made the copy steps larger
   *(an inference)*.
5. With `O_DIRECT` and aligned buffers nothing was torn on ext4, xfs, ext2 and btrfs, at 8 KB, 32 KB,
   4 MB and 16 MB (Linux 7.2). tmpfs accepts `O_DIRECT` and still tore 31% of the 8 KB writes.
   *(measured)* A direct write has no copy loop, which would explain it *(an inference)*.
6. The test robot measured the ext4 change at +37.7% `fsmark` files per second, and its profile shows
   the buffered write loop doing less work per byte. *(documented)* That fits the explanation, but a
   CPU profile of a different workload is not evidence about signals.
7. Large folios are not used everywhere: ext4's patch excludes `data=journal`, fscrypt and fsverity.
   *(documented, from the patch text in a search result)* Tears at 8 KB may persist there.
8. None of this is about a power loss. A kill leaves the kernel running; a power loss loses the page
   cache and the drive's cache, and what is torn depends on writeback, the device and the file
   system. No kernel makes the Phase 5 torn page protection unnecessary.
9. A power loss can be emulated in three ways: block-level tools (dm-log-writes, CrashMonkey,
   dm-flakey), a model of the file system at system call level (ALICE), or a recording layer in our own
   `io->` interface. The last one is the only one that can tear a single `pwrite` at sector
   boundaries and that does not depend on the kernel, the file system or the machine; it is proposed
   for Phase 5 (below).

## Method

A child process opens a file and writes one block of N bytes at offset 0 in a loop, every byte of
the block set to the same value, a different value each time. The parent sleeps for a random time,
sends `SIGKILL`, waits for the child, reads the block back and calls it *torn* if its bytes are not
all equal. A shared-memory flag, set by the child around the `pwrite`, tells how many kills landed
inside the syscall (80% to 99% in all runs). The source is at the end of this file.

The runs: the development VM (Linux 7.2, OrbStack on macOS; ext2, ext4, xfs on loop-mounted
images, btrfs and tmpfs), GitHub-hosted runners (Ubuntu 22.04 with Linux 6.8, Ubuntu 24.04/26.04
with Linux 7.0), older distributions booted in QEMU with KVM (Ubuntu 18.04, 20.04, 22.04 and Debian
12 with their own kernels), and exact upstream kernels from the Ubuntu mainline archive booted the
same way (Linux 6.1 to 6.18). The workflows were throwaway branches that were deleted after the
runs.

## Buffered 8 KB writes, by upstream kernel release

Torn blocks out of 3000 kills. tmpfs was also run every time and was torn in 24% to 37% of the kills.
*(measured)*

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

So an 8 KB page was no longer torn on xfs from **6.9** and on ext4 from **6.16**. I have not
identified the commit for xfs (that would need a build and bisect between 6.8 and 6.9); for ext4 the
release is the one in which the large folio series was merged (see "What the kernel sources say").

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
backports changes. btrfs and ext2 were tried on 7.2 only, and neither tore an 8 KB write (0 in 3000
to 4000 kills).

## Larger buffered writes

Writes of 32 KB (measured on the distribution kernels only) tore on tmpfs, ext4 and xfs on 4.15 to
6.1 (for example xfs on 5.15: 1898 of 3000), and on tmpfs and ext4 on the Ubuntu 6.8 kernel; on the
Ubuntu 7.0 and the 7.2 kernels they were not torn on ext4 and xfs. Writes of 4 MB and more were
torn on ext4 and xfs on **every kernel tried, including the newest**: at 4 MB, ext4 tore 112 to 309
of 400 kills and xfs 72 to 301 of 400. On 7.2 the tear was always at a multiple of 2 MB (4 MB: about
a quarter of the kills; 16 MB: about two thirds); the positions were not recorded on the other
kernels. *(measured)*

## With `O_DIRECT`

With `O_DIRECT` and 4096-aligned buffers (and a direct read for the check), nothing was torn on
ext4, xfs, ext2 and btrfs on Linux 7.2: 0 of 3000 for 8 KB (94% to 99% of the kills inside the
`pwrite`), 0 of 3000 for 32 KB on ext4 and xfs, 0 of 400 for 4 MB, 0 of 200 for 16 MB. tmpfs accepts
`O_DIRECT` but keeps its own cache, copies page by page and still tore 940 of 3000 8 KB writes.
*(measured; only Linux 7.2 was tested with `O_DIRECT`)*

An explanation *(an inference)*: a direct write does not copy into the page cache. The kernel pins
the pages of the buffer and submits a block I/O request to the device, with no copy loop and no
signal check between pieces; the call returns when the request completes, and a `SIGKILL` does not
cancel an I/O that has been submitted. Folios do not matter here for that reason: they set the size
of the copy steps, and there are none.

## What the kernel sources say

*Documented*, with links:

- **ext4 got large folios for regular files in Linux 6.16.** Ted Ts'o's pull request for 6.16
  lists "Fast commit performance improvements", "Multi-fsblock atomic write support for bigalloc file
  systems" and "Large folio support for regular files", and says the last "can result in really
  stupendous performance for the right workloads", citing the test robot report. Its shortlog has
  seven folio entries: `ext4_mpage_readpages()`, the buffered write path, `__ext4_block_zero_page_range()`,
  jbd2's `blocks_per_page`, the writeback path, online defragmentation, and "ext4: enable large folio
  for regular file" ([pull request](https://lists.openwall.net/linux-ext4/2025/05/27/3),
  [Phoronix summary](https://www.phoronix.com/news/Linux-6.16-EXT4-Performance)).
  This is the release in which our ext4 column goes from torn to not torn.
- **The test robot measured it.** [The report](https://lore.kernel.org/all/202505161418.ec0d753f-lkp@intel.com/)
  blames commit `34696dd792d8` ("ext4: enable large folio for regular file") for +37.7% `fsmark`
  files per second (529.15 to 728.75): 4 MB files, one thread, 24 GB in total, no sync, ext4 on a
  RAM block device, on a 96-thread Xeon with 128 GB. Its profile shows `generic_perform_write`
  (the buffered write loop) falling from 24.4% to 16.7% of the cycles, `__block_commit_write` from
  5.8% to 1.2%, `__filemap_get_folio` from 4.9% to 1.9% and `fault_in_iov_iter_readable` from 0.36%
  to 0.09%, while the copy itself (`copy_page_from_iter_atomic`) grows slightly: fewer, larger
  steps for the same bytes. It is a performance report and says nothing about signals.
- **The write loop checks for a fatal signal.** A commit by Jan Kara that moves the
  `fatal_signal_pending()` check in `generic_perform_write()` says that the check already triggered,
  but "because we always succeed in writing something before the check is done, we return value > 0",
  that is a short write ([commit](https://github.com/torvalds/linux/commit/296291cd)). A 2017 mailing
  list message says that everybody must be ready for short writes
  ([LKML](https://lkml.iu.edu/hypermail/linux/kernel/1701.3/01027.html)). A process that is being
  killed never sees the short count, so the block stays half written.
- **Which files get large folios.** The patch ([v2, 8/8](https://patchew.org/linux/20250512063319.3539411-1-yi.zhang@huaweicloud.com/20250512063319.3539411-9-yi.zhang@huaweicloud.com))
  enables them for regular files except with `data=journal`, fscrypt and fsverity. I know this from a
  search result quoting it and did not open the patch.

*Found but not read*: a regression report from the same series (`lmbench` mmap read latency, from
6.16-rc1), the thread under the test robot report with performance reports in June 2025, a July 2025
commit "ext4: limit the maximum folio order" (the place to look for why ext4's largest folio is what
it is), and the XFS atomic writes that Phoronix lists for 6.16.

*Not documented anywhere I looked*: the xfs release (6.9) and its commit, why the tears on 7.2 are at
multiples of 2 MB, and the difference between the Ubuntu and upstream 6.8 kernels.

## Why: an explanation *(an inference)*

A buffered `write` copies the data into the page cache in pieces, one folio at a time. A *folio* is
a group of 1 to 512 contiguous pages (up to 2 MB) that the kernel's memory manager treats as one
unit; before large folios the unit was a 4 KB page. Between two pieces the kernel checks whether a
fatal signal is pending, and if so it stops and returns a short count (the sources above). An 8 KB
write is therefore safe from a kill only if it fits in one piece, which is the case once the file
system uses folios of 8 KB or more for it. That fits all the data: tmpfs always copies one page at a
time and tears at byte 4096; ext4 stops tearing 8 KB at the release that enabled large folios;
xfs does so earlier; and for writes above 2 MB the tear is at a multiple of 2 MB, the largest folio
size. The measurements do not prove it: for instance, the exact folio sizes used were not observed.

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
  it (from memory, since 6.11). A direct write is not atomic at 8 KB either, unless one of these
  applies. The 6.16 pull request lists "multi-fsblock atomic write support for bigalloc file
  systems" for ext4, and Phoronix links an article about XFS atomic writes in the same release;
  I have not read the details.
- **The file system.** Journaling (for example ext4 `data=ordered`) orders metadata against data,
  but does not make a data write atomic.
- **Flush semantics.** Whether a flush is honored all the way to the medium depends on the stack,
  and a virtual disk adds the host's behavior. A VM cannot test a power loss, since the host's cache
  decides.

So a result like "xfs on 6.9 or later does not tear an 8 KB write on a kill" is no reason to skip
torn page protection: it must assume that a power loss tears pages on every file system and kernel,
including the newest ones (the Phase 5 task lists the designs).

## Emulating a power loss

What the tools below can emulate is a crash at a chosen point in a stream of writes. Three
questions decide whether they fit: where can the crash happen (between requests, or inside one),
which reorderings of unsynced writes are tried, and what the tool needs (a kernel module, an old
kernel, a particular file system).

| Tool | What it emulates | Torn write inside one request? | Fit for TielDB |
|---|---|---|---|
| **dm-log-writes** | Logs every write (and flush/FUA flag) to a log device, with named marks set from userspace; `replay-log` replays the log up to a mark or an entry onto a copy, giving the disk state at any prefix of the writes. Used by xfstests for crash consistency tests. | No. A request is atomic; the crash is between requests. If an 8 KB page goes out as two bios, a prefix can hold only the first. | Best fit for checking a real file system with the real kernel. |
| **dm-flakey** | A device that is up for N seconds and down for M; while down it can drop writes, return errors or corrupt a byte of a bio. | Per bio, on a timer. | Crude: time-based, not at a chosen write. Good for I/O error paths. |
| **CrashMonkey** | A block-level recorder like dm-log-writes, plus replaying subsets of the writes between flush/FUA barriers (the reorderings a drive may do), then `fsck` and checks of what was synced. | Not inside a request. | The idea is the best of the three, but it was built for Linux 4.x and I would not expect it to work on a current kernel without effort. |
| **ALICE** | Works at the system call level: traces an application, builds a model of what a file system does (how writes can be split, reordered or only partly persisted), and enumerates the post-crash states the model allows; a checker you write validates each one. | Yes, to the granularity of the model (sectors). | Closest in spirit to testing a database, but a research tool of the Python 2 era. |
| **QEMU stop or sysrq** | Killing the QEMU process (or `echo b > /proc/sysrq-trigger` in the guest) drops the guest's page cache but keeps what reached the virtual disk. | Not controllable. | Full stack, easy, not repeatable at a chosen point. |

From the mailing lists *(documented)*: dm-log-writes was proposed by Josef Bacik as a target for
"testing power fail and fs consistency"; the 2017 patch series added `replay-log` and the xfstests
([series v3](https://lore.kernel.org/all/20171128172152.ktvpnwv233govfwl@destiny/T/)); and in a 2017
exchange he argued that it already covers most of what CrashMonkey does, while the CrashMonkey author
pointed out that replaying subsets of writes between FLUSH/FUA points is something dm-log-writes does
not do ([thread](https://lore.kernel.org/all/20171127150439.i4tpt4yhooxnoaiz@destiny/)). The
descriptions of dm-flakey (its `drop_writes`, `error_writes`, `error_reads` and `corrupt_bio_byte`
options and the up/down intervals), of CrashMonkey's bounded black-box testing, of ALICE (Pillai et
al., OSDI 2014) and of the QEMU stop and sysrq approach are from memory and were not checked in this
work.

### Our own I/O layer

All file I/O of TielDB goes through `io->`, and the fault layer in `tests/io_fault` already breaks
single calls (`IO_FAULT_TORN_WRITE` writes half of the bytes of the Nth write). A *recording* layer
would go further, and it is the only one of these options that tears a single `pwrite` at sector
boundaries and does not depend on the kernel, the file system or the machine:

1. **Record.** Every `pwrite` (file, offset, bytes), `fsync`, `fdatasync`, `rename`, `unlink` and
   file creation, in order, while a workload runs on the real file system.
2. **Build crash states** from the recording, under a deliberately weak model of the storage (the
   least that POSIX promises, as ALICE does), and write each state into a fresh copy of the data
   directory:
   - every *prefix* of the operations;
   - between two syncs, any *subset* of the unsynced writes, in any order (the operating system and
     the drive may persist them in any order), exhaustively for small epochs and sampled for large ones;
   - for the write that was in flight, any subset of its *sectors* (512 B, or 4096 for a model of a
     4Kn drive), which is a torn page;
   - a file created or renamed persists only if the directory was synced after it.
3. **Recover and check.** Start the recovery on each state and check the invariants (every
   transaction that was committed before the crash survives; no page fails its checksum after
   recovery; the catalog loads).

What it gives: deterministic states that can be named and replayed from a seed, a test that runs in
the normal suite in seconds, and a statement that is stronger than "it passed on ext4 on this
kernel", because the model allows more than any one file system does. What it does not: it models
the storage API and not the hardware (a write cache that reorders across files, firmware bugs), and
the model must be kept honest (for example, `fsync` of one file does not order writes to another file
unless the model says so). The real file systems and kernels can still be checked, with less control,
by dm-log-writes in a VM, as a second layer.

Where it would be used: the Phase 5 tests (WAL recovery, the torn page protection, the checkpoint),
and earlier, for the atomic replace of small files (temporary file, sync, rename) that the control
file task needs. A rough size: 200 to 300 lines for the recorder and the state builder, plus the
model test of the builder itself (the states it must produce for a small recording).

## Consequences for TielDB

- **Crash tests.** The Phase 2 `kill -9` test kills the child only after the flush and the sync have
  finished, so no write is in flight and it does not depend on the file system. The Phase 5
  random-kill test expects torn pages and uses them (tmpfs tears often), with the injected torn write
  and the recording layer as the deterministic complement.
- **Torn page protection.** Required whatever the kernel does for a kill: a power loss tears pages
  on every file system (the Phase 5 task lists the candidate designs).
- **Deployment.** On kernels before ext4's 6.16 and xfs's 6.9 (and in `data=journal`, fscrypt and
  fsverity setups), a plain process crash such as an out-of-memory kill can turn an 8 KB page write
  into a torn page. The checksum detects it, but nothing repairs it before the WAL exists.
- **Large writes.** Any design that writes big blocks with one buffered `pwrite` (WAL segments in
  large batches, SSTable blocks of an LSM engine) can be cut in the middle by a kill on every kernel
  tried; write in smaller units or checksum them.
- **Atomic writes.** Kernel support for atomic writes (6.16 for ext4 with bigalloc and for XFS) could
  one day make the full-page-image mechanism unnecessary on supporting hardware; it is listed as a
  candidate in the Phase 5 task, with the caveat that it is not portable.

## Caveats

- Absence of tears in 3000 kills is strong evidence against a tear rate of a few percent, and no
  evidence that a tear is impossible.
- The mainline kernels are Ubuntu's builds of upstream releases (`.0` releases, generic config)
  booted in a QEMU guest with an Ubuntu 22.04 userland; the file systems were loop-mounted images of
  512 MB. Other configurations may differ.
- Only 8 KB, 32 KB, 4 MB and 16 MB were measured, and only offset 0 of a file that already had
  contents.
- The development VM has 512-byte logical sectors; devices with 4096-byte sectors were not tested.
- The kernel sources were read through mailing list archives, a web page and search results; I did
  not read the kernel tree, and parts of this document (marked) are from memory.

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
