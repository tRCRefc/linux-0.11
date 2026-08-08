# 阶段一补充：Kernel-owned x86-64 page tables

Date: 2026-08-01

## Milestone

The x86-64 kernel now replaces the firmware page tables before entering its C
main function. It builds a kernel-owned four-level hierarchy, identity-maps
the first 4 GiB with 2 MiB pages, loads the new PML4 address into `CR3`, clears
`CR4.PGE`, and enables supervisor write protection with `CR0.WP`.

The verified OVMF serial output is:

```text
Linux 0.11 x86-64: efi_main reached
Linux 0.11 x86-64: UEFI console is available
UEFI memory map: entries=128
UEFI descriptor size: 48
UEFI conventional pages: 20256
UEFI boot services exited
x86-64 kernel stack installed
x86-64 kernel main reached
x86-64 page tables installed
x86-64 CR3 self-test passed
x86-64 descriptor tables installed
x86-64 IDT int3 self-test passed
boot memory map entries: 128
```

Firmware allocation counts can vary between boots. The important result is
that execution, stack access, C data access, serial I/O, GDT/IDT use, and an
`int3`/`iretq` round trip all continue after the `CR3` replacement.

## Four-level address translation

A conventional 48-bit x86-64 virtual address is divided as follows:

```text
63             48 47       39 38       30 29       21 20       12 11        0
+----------------+-----------+-----------+-----------+-----------+-----------+
| sign extension | PML4 index| PDPT index|  PD index |  PT index | 4 KiB off |
+----------------+-----------+-----------+-----------+-----------+-----------+
                     9 bits      9 bits      9 bits      9 bits      12 bits
```

Bits 63:48 must be a sign extension of bit 47 for a canonical address. Each
nine-bit index selects one of 512 eight-byte entries:

```text
CR3 -> PML4 -> PDPT -> page directory -> page table -> 4 KiB page
```

Each table occupies one 4 KiB page because `512 * 8 = 4096`.

## Why bootstrap with 2 MiB pages

If the Page Size bit is set in a page-directory entry, that entry maps a
2 MiB page directly. The final page-table level disappears:

```text
CR3 -> PML4 -> PDPT -> page directory -> 2 MiB page
```

The virtual-address layout for such a translation is:

```text
47       39 38       30 29       21 20                              0
+-----------+-----------+-----------+---------------------------------+
| PML4 index| PDPT index|  PD index |        2 MiB page offset        |
+-----------+-----------+-----------+---------------------------------+
```

The bootstrap uses six 4 KiB pages in total:

```text
1 PML4 + 1 PDPT + 4 page directories = 6 pages = 24 KiB
```

Four page directories contain 2048 entries. Each entry covers 2 MiB, so the
mapped range is `2048 * 2 MiB = 4 GiB`. A 4 KiB-page version would additionally
need 2048 page-table pages, or 8 MiB, before an allocator even exists.

## Entries and flags

The current non-leaf PML4 and PDPT entries use `0x003`:

```text
bit 0: Present  - hardware may use the entry
bit 1: Writable - writes are allowed if upper levels also permit them
```

The page-directory entries use `0x083`:

```text
bit 0: Present
bit 1: Writable
bit 7: Page Size - this PDE maps a 2 MiB page instead of a page table
```

All current mappings are supervisor-only because the User/Supervisor bit is
clear. They are also executable because NX is not yet used. These permissive
permissions are acceptable only for the early transition; later mappings must
separate executable text, read-only data, writable data, and non-executable
memory.

## Identity mapping

An identity mapping makes virtual address `X` translate to physical address
`X`. The first PDE maps physical `0x00000000` at virtual `0x00000000`, the next
maps `0x00200000` at `0x00200000`, and so on through the first 4 GiB.

This avoids changing instruction addresses, stack pointers, and C pointers at
the same instant that the page-table root changes. The UEFI image is
position-independent, but RIP-relative code still expects its runtime virtual
addresses to remain valid. Identity mapping provides that bridge.

Before loading `CR3`, the assembly checks that the executing image, owned
stack, page tables, `boot_info`, and the complete UEFI memory-map buffer fit
below 4 GiB. Failure halts before the switch instead of causing a page fault
with no useful diagnostic path.

## CR3, the TLB, and CR0.WP

`CR3` contains the physical base address of the active PML4. The bootstrap
loads the address of `early_pml4` into `CR3`; the self-test later masks the low
12 bits of `CR3` and compares the result with that same PML4 address.

Loading `CR3` changes the active translation hierarchy and invalidates the
applicable cached translations in the TLB. Global entries can survive an
ordinary `CR3` load, so the bootstrap clears `CR4.PGE` after the switch. This
also invalidates firmware global translations and leaves global pages disabled
until the kernel deliberately introduces them.

Setting bit 16, `CR0.WP`, makes supervisor writes respect read-only page-table
permissions. The current mappings are writable, so it has no immediate visible
effect, but enabling it early prevents future read-only kernel mappings from
being silently writable in ring 0.

## Relationship to Linux 0.11

Original Linux 0.11 performs `setup_paging` in `boot/head.s`. It clears one
page directory and four page tables, fills 4096 32-bit PTEs backwards, maps the
first 16 MiB with 4 KiB pages, loads `CR3`, and sets the paging bit in `CR0`.

The modernized path preserves the responsibility, not the exact mechanism:

```text
Linux 0.11 i386              x86-64 port
--------------------------  --------------------------------------------
two-level paging            four-level paging
one page directory          PML4 + PDPT + four page directories
4 KiB pages                 2 MiB bootstrap pages
first 16 MiB mapped         first 4 GiB mapped
enables CR0.PG              long mode already has paging enabled
loads initial CR3           replaces firmware CR3 with a kernel-owned CR3
```

This is therefore a real architectural port of an original early-kernel
responsibility. UEFI gets the CPU to long mode, but the kernel no longer relies
on firmware-owned translation tables after this point.

## What the test proves

The `CR3` self-test proves that the processor reports the expected PML4 as its
active root. Continuing to the final serial line proves more than the direct
comparison alone:

- instructions remain fetchable through the new mapping;
- the owned stack remains readable and writable;
- C strings and global state remain accessible;
- the new GDT and IDT can be loaded and used;
- the breakpoint handler can execute `iretq` successfully;
- the handed-off UEFI memory-map metadata remains accessible.

It does not prove that every one of the 2048 PDEs is correct, nor does it make
all physical memory usable. It establishes a controlled bootstrap address
space on which a memory manager can be built.

## Current limits and next work

Only the first 4 GiB is mapped. This is sufficient for the tested OVMF image
placement and early objects, but RAM above 4 GiB is inaccessible even if it is
listed in the UEFI map. Expanding beyond this point requires parsing usable
physical ranges, allocating page-table pages, and creating mappings on demand.

The next memory-management stages are:

1. Normalize the UEFI memory map and reserve the image, stack, page tables,
   boot metadata, and other non-free ranges.
2. Build an early physical page allocator.
3. Add 4 KiB mappings where fine-grained permissions are required.
4. Enable NX and enforce text/rodata/data permissions.
5. Introduce the final kernel virtual layout, potentially including a
   higher-half kernel and a deliberate physical-memory mapping.
6. Replace the fixed bootstrap tables once the allocator can build the final
   hierarchy.
