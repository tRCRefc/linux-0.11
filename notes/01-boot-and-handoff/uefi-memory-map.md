# 阶段一补充：UEFI memory map and ExitBootServices

Date: 2026-08-01

## Milestone

The x86-64 UEFI image now obtains the firmware memory map, exits UEFI Boot
Services, disables maskable interrupts, and stops at a kernel takeover point.

The verified serial output is:

```text
Linux 0.11 x86-64: efi_main reached
Linux 0.11 x86-64: UEFI console is available
UEFI memory map: entries=127
UEFI descriptor size: 48
UEFI conventional pages: 20269
UEFI boot services exited
Kernel takeover point reached
```

The exact entry and page counts may change between boots because firmware
allocations also appear in the memory map. The important invariants are that
the descriptor size comes from the firmware, the map is internally
consistent, and execution does not return to the firmware menu after
`ExitBootServices` succeeds.

## What this replaces

Linux 0.11 used BIOS interrupt `0x15` in `boot/setup.s` to obtain an extended
memory size and stored it at physical address `0x90002`. `init/main.c` later
read that fixed address through `EXT_MEM_K`.

The x86-64 path replaces that contract with the UEFI memory map. It preserves
the responsibility of discovering usable memory, but not the BIOS interface,
the fixed handoff address, or the assumption that all usable memory can be
represented by one size below 16 MiB.

## UEFI ABI boundary

The firmware calls `efi_main` using the Microsoft x64 ABI. UEFI service
function pointers use the same ABI through `EFIAPI`, while ordinary internal C
functions continue to use the compiler's normal System V ABI.

The local UEFI declarations intentionally cover only the interfaces currently
used. Compile-time assertions protect the most important table offsets:

```text
EFI_BOOT_SERVICES.GetMemoryMap       0x38
EFI_BOOT_SERVICES.ExitBootServices  0xe8
EFI_SYSTEM_TABLE.BootServices       0x60
```

An ABI-correct function signature is not enough if the function pointer is
read from the wrong table offset, so both properties are checked.

## Obtaining the memory map

`GetMemoryMap` is first called without a buffer. The expected result is
`EFI_BUFFER_TOO_SMALL` plus the required buffer size and descriptor size. The
loader allocates the buffer as `EfiLoaderData` and reserves space for eight
additional descriptors because allocating the buffer can itself enlarge the
map.

If the map still grows, the old buffer is released, a larger one is allocated,
and the operation is retried. Integer overflow, undersized descriptors, and a
map size that is not a multiple of the descriptor size are rejected.

UEFI may extend `EFI_MEMORY_DESCRIPTOR` in later revisions. The loader
therefore advances by the firmware-provided `DescriptorSize`, not by
`sizeof(struct efi_memory_descriptor)`. On the tested OVMF build, the C prefix
is 40 bytes while the firmware stride is 48 bytes.

## Exiting Boot Services

`ExitBootServices` accepts the image handle and the key associated with the
latest memory map. Any operation that changes the firmware memory map makes
that key stale.

The exit loop follows this sequence:

```text
GetMemoryMap
    -> ExitBootServices(map_key)
        -> success: never call Boot Services again
        -> EFI_INVALID_PARAMETER: refresh the map and immediately retry
        -> another error: release the map and return the error to firmware
```

After a successful exit, the memory map buffer is deliberately retained. It
is loader-owned memory that a future `boot_info` structure can pass to the
physical memory manager. Calling `FreePool`, console output, or any other Boot
Service after success would be invalid.

COM1 output does not depend on UEFI services, so it remains available after
the transition. The code executes `cli` before announcing success and then
halts forever. This prevents interrupts from reaching firmware-installed
handlers while no kernel IDT exists.

## What this proves

This milestone proves that:

- OVMF can load the position-independent PE32+ image.
- The UEFI table layouts and Microsoft x64 call boundary work on the tested
  firmware.
- The loader can acquire and traverse a real firmware memory map.
- Boot Services can be exited without returning to the firmware menu.
- Direct serial output remains usable after firmware boot services disappear.

It does not yet prove that the kernel has a safe execution environment. The
code still uses the firmware-provided stack, GDT, IDT, and page tables. The
next milestone must switch to an x86-64-owned stack and entry point before
attempting interrupts, memory allocation, or the Linux 0.11 initialization
sequence.

## Modernization policy

The `x86-64` branch is a modernized port, not a dual-architecture preservation
branch. The original `boot/` directory can be deleted after its remaining
startup responsibilities have verified replacements: an owned stack, GDT,
IDT, four-level page tables, and a kernel entry contract. Keeping the old BIOS
and 32-bit startup files beyond that point would no longer serve the active
build.
