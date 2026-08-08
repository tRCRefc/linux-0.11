# 阶段一：Linux 0.11 x86-64 现代化移植 DAY 1

日期：2026-08-01

## 1. 起点：原版 Linux 0.11

仓库最初是面向 1991 年 i386 PC 的 Linux 0.11。原启动链由根目录
`boot/` 中的三个汇编文件组成：

- `bootsect.s`：由 BIOS 从 0x7c00 加载，在 16 位实模式下装载 setup
  和内核镜像，并确定根设备。
- `setup.s`：继续使用 BIOS 中断取得内存大小、显示模式、光标位置和
  硬盘参数，然后打开 A20、建立临时描述符表并进入 32 位保护模式。
- `head.s`：建立 32 位内核的 GDT、IDT 和两级页表，恒等映射前
  16 MiB，最后调用 `init/main.c`。

这套代码依赖 BIOS、实模式、软盘/传统硬盘布局、i386 分段机制和
32 位两级分页，不能直接用于 UEFI 和 x86-64 long mode。

## 2. 移植目标和原则

本分支的目标不是保留一套并行的 32 位 Linux 0.11，而是把 Linux
0.11 逐步现代化为由 QEMU/OVMF 以 UEFI 方式启动的 x86-64 内核。

已经确定的原则包括：

- UEFI 只承担固件和装载器必须完成的工作，不把项目变成 UEFI 教程。
- 进入内核接管点后，不再依赖 UEFI Boot Services。
- 原版代码的职责需要被现代实现替代，但不要求保留已经过时的 BIOS
  接口和历史实现形式。
- `note*.md` 和 `DAY*.md` 长期作为本地学习记录维护。当前仓库迁移使用一次性 Git 快照，
  后续按根目录 `MIGRATION.md` 恢复本地未跟踪状态。

## 3. 建立最小 UEFI x86-64 启动镜像

首先建立了独立的 x86-64 构建路径：

- 使用 freestanding GCC 编译 C 和汇编代码。
- 使用 PE/COFF 链接器生成 PE32+ EFI application。
- EFI 入口为 `efi_main`，遵守 Microsoft x64 ABI；内核内部函数遵守
  System V x86-64 ABI。
- 制作 FAT ESP 镜像，把程序安装为 `EFI/BOOT/BOOTX64.EFI`。
- 使用 QEMU、OVMF pflash 和串口进行无图形启动验证。
- 实现早期 COM1 驱动，使退出 Boot Services 后仍有诊断输出。

本地 UEFI 类型和表结构只声明目前实际使用的字段，并用静态断言检查
关键偏移，避免因为固件 ABI 布局错误而调用错误的服务指针。

## 4. 让 EFI 镜像与装载地址无关

UEFI 固件可以把镜像放到不同物理地址。为此完成了位置无关化：

- C 使用 `-fpic`，汇编使用 RIP-relative 寻址。
- 链接镜像不假定一个固定运行地址。
- 构建检查拒绝 `R_X86_64_32` 和 `R_X86_64_32S` 等会截断运行地址的
  绝对 32 位重定位。
- 使用 `objdump`、`readelf` 和 `nm` 检查 PE32+ 格式、入口点、未定义
  符号和重定位类型。

这使 OVMF 可以重定位整个 EFI 镜像，而内核入口、静态数据和汇编符号
仍能正确访问。

## 5. 获取 UEFI 内存图并退出 Boot Services

启动代码实现了可靠的 UEFI 内存图交接：

- 先调用 `GetMemoryMap` 获得所需缓冲区大小。
- 通过 `AllocatePool` 分配缓冲区，并为内存图增长预留额外描述符。
- 按固件返回的 `DescriptorSize` 遍历，而不是假定它等于本地 C 结构
  大小。
- 检查描述符大小、条目步长、整数溢出和缓冲区增长。
- 使用最新 memory-map key 调用 `ExitBootServices`。
- 若 key 因内存图变化而过期，则刷新内存图并立即重试。
- 成功退出后不再调用任何 Boot Service，并保留内存图缓冲区供内核
  后续物理内存管理使用。

共享的 `boot_info` 当前传递：

```text
memory_map
memory_map_size
memory_descriptor_size
memory_descriptor_version
```

这替代了原版 `setup.s` 通过 BIOS `int 0x15` 取得单一扩展内存大小并写入
固定物理地址的做法。

## 6. 建立内核自己的执行环境

退出 Boot Services 后，代码进入汇编入口 `x86_64_start`，并逐项替换
固件遗留的运行环境。

### 6.1 内核栈

- 在内核镜像中保留 16 KiB、按页对齐的早期栈。
- 保存 `boot_info` 指针后切换 `%rsp`。
- 按 System V ABI 保留 16 字节栈对齐。
- 清零 `%rbp`，并使用 `-mno-red-zone`，避免中断或异常破坏 red zone。

### 6.2 GDT

- 建立内核拥有的 64 位 GDT。
- `0x08` 是 64 位内核代码段，`0x10` 是内核数据段。
- 使用 `lgdt` 加载 GDTR，并通过 `lretq` 重新装载 CS。
- 重新装载 DS、ES、SS、FS 和 GS，消除对固件描述符表的依赖。

### 6.3 IDT

- 建立 256 项、每项 16 字节的 x86-64 IDT。
- 初始异常统一进入关中断后的停机循环。
- 为向量 3 安装单独的 breakpoint handler。
- 使用 `int3` 自检门描述符、`lidt` 和 `iretq` 路径。

这仍是早期 IDT，不是最终异常和硬件中断子系统。

## 7. 建立内核自己的四级页表

当前最后一个里程碑替换了 OVMF 提供的地址空间：

- 分配 1 页 PML4、1 页 PDPT 和 4 页 page directory，共 24 KiB。
- 使用 2048 个 2 MiB 大页恒等映射前 4 GiB。
- PML4E/PDPTE 使用 Present + Writable，PDE 额外设置 Page Size。
- 切换前确认代码、栈、页表、`boot_info` 和完整 UEFI 内存图缓冲区均
  位于前 4 GiB。
- 将 PML4 的物理地址写入 `%cr3`。
- 清除 `CR4.PGE`，刷新可能由固件留下的 global TLB translation。
- 设置 `CR0.WP`，让 ring 0 也遵守未来的只读页权限。
- C 入口读取 `%cr3` 并与早期 PML4 地址比较，完成动态自检。

这对应原版 `boot/head.s::setup_paging` 的现代化移植。区别是 long mode
已经启用分页，因此这里只替换 CR3；页表层级从两级变为四级，启动映射
从 4 KiB 页改为 2 MiB 大页，范围从 16 MiB 扩大到 4 GiB。

## 8. DAY 1 最终动态验证

完整清理、构建、静态检查、ESP 制作和 OVMF/QEMU 启动均已通过。最后
一次串口输出为：

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

内存图条目数和 conventional page 数可能随固件分配变化，不属于固定
测试值。真正的通过条件是退出 Boot Services、替换栈/GDT/IDT/CR3 后
仍能进入 C、输出串口信息、完成 `int3/iretq` 并读取启动信息。

## 9. Git 里程碑

DAY 1 形成的源码提交依次为：

```text
2066580 boot: add minimal x86-64 UEFI application
3c96083 boot: make x86-64 UEFI image position independent
714a247 boot: exit UEFI boot services
73685ac x86_64: enter kernel on an owned stack
e29f7b2 x86_64: install early descriptor tables
82e2680 x86_64: install early page tables
f41f8a6 boot: replace legacy BIOS startup
f003a23 x86_64: move port into original source layout
```

学习笔记没有进入这些提交。

## 10. 源码归位

早期开发曾使用 `arch/x86_64/` 隔离尚未稳定的启动骨架。验证启动职责后，
代码按照“就地现代化 Linux 0.11，而不是建立并行架构树”的项目原则归入
原目录：

```text
boot/head.S                         64 位内核汇编入口、GDT、IDT、早期页表
boot/uefi/main.c                    UEFI 入口、内存图和 ExitBootServices
boot/uefi/efi.h                     当前使用的 UEFI ABI 声明
include/asm/boot.h                  boot_info 交接接口
include/asm/serial.h                早期串口接口
init/main.c                         当前 64 位 C 内核接管点
kernel/chr_drv/serial.c             当前轮询式 COM1 实现
```

原版 `boot/bootsect.s`、`boot/setup.s` 和 32 位 `boot/head.s` 已由现代启动
路径替代。临时的 `arch/x86_64/` 已完全移除，默认根 `Makefile` 直接构建
x86-64 UEFI 镜像。后续移植继续使用原项目的 `init/`、`kernel/`、`mm/`、
`fs/` 和 `include/` 布局。

## 11. 当前边界和完成度

DAY 1 完成的是现代 x86-64 bootstrap，以及原版 `boot/head.s` 核心职责
的替代。这一段启动移植在当前里程碑范围内是完整且经过动态验证的。

目录归位不等于对应子系统已经完整移植：

- 当前 `init/main.c` 只验证启动交接、页表、描述符表和 UEFI 内存图，
  尚未恢复原版 main 中的内存、陷阱、设备、调度、文件系统和用户态初始化。
- 当前 `kernel/chr_drv/serial.c` 只提供启动期轮询 COM1，不具备原版串口
  TTY 的 `rs_init`、`rs_write`、IRQ、队列和流控功能。
- 新的 `include/asm/boot.h` 和 `serial.h` 是完整的当前接口，但整个
  `include/asm` 中其他 i386 汇编接口仍未移植。

因此，完成的是启动路径和源码结构整理，不是整个 Linux 0.11 的完整
x86-64 移植。未移植的旧代码仍保留在相应原目录中，后续应逐个子系统
替换，而不是再次创建平行架构目录。

尚未实质开始的部分包括：

- 根据 UEFI 内存图初始化物理页管理；
- `mem_init`、`get_free_page`、`free_page` 和最终页表管理；
- 异常、IRQ、时钟、系统调用和任务切换；
- 调度器、进程 0/1 和用户态；
- 块设备、根设备、文件系统和程序装载；
- 最终的内核虚拟地址布局、NX 和按段页权限。

下一阶段应从 UEFI 内存图中规范化可用物理区间、保留启动关键区域，
然后建立第一个可用的 4 KiB 物理页分配器。这将是从启动代码进入 Linux
0.11 内存管理移植的分界点。
