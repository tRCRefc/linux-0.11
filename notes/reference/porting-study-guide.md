# Linux 0.11 x86-64 移植历史学习指南

> 状态说明（2026-08-05）：本文记录的是较早的 `ef35b8f` 启动与内存阶段，正文中的
> “当前状态”不再代表 `5d0f848`。当前代码阅读顺序请以 `code-reading-guide.md` 为准；
> 综合问题见 `../review/vm-review-2026-08-05.md`，原版语义对照见
> `linux-0.11-comparison.md`，开发路线见 `../review/vm-roadmap-2026-08-05.md` 和
> `../../NEXT_STEP.md`。本文保留作为前期移植过程和基础概念的历史记录。

日期：2026-08-02

当前源码基线：`ef35b8f`

本文档原为本地资料，当前仅通过一次性迁移快照纳入 Git。它的用途不是记录某一次提交，
而是帮助你从头到尾理解：

1. 原版 Linux 0.11 是怎样启动的。
2. 为什么原版代码不能直接运行在 UEFI x86-64 机器上。
3. 当前移植版替换了哪些部分。
4. 哪些职责仍然保持了 Linux 0.11 的设计。
5. 应当按什么顺序阅读原版和当前源码。
6. 编译、链接、反汇编和 QEMU 输出分别能够证明什么。
7. 下一阶段应该如何继续移植，而不是盲目重写。

---

# 第一部分：先建立整体认识

## 1. 这个项目现在是什么

原版 Linux 0.11 面向 1991 年的 i386 PC：

- BIOS 启动。
- 16 位实模式开始执行。
- 使用 BIOS 中断读取磁盘和机器参数。
- 手动开启 A20。
- 进入 32 位保护模式。
- 使用两级页表。
- 最多管理 16 MiB 物理内存。

当前分支的目标是让它在现代环境中运行：

- QEMU 提供虚拟 x86-64 机器。
- OVMF 提供 UEFI 固件。
- 固件加载 PE32+ EFI application。
- CPU 已经处于 x86-64 long mode。
- 内核建立自己的栈、GDT、IDT 和四级页表。
- 内存管理仍然保留 Linux 0.11 的 `mem_map[]` 思想。

这不是简单地“给原版加几个编译选项”。启动协议、寄存器宽度、页表格式和硬件接口都发生了变化。

移植时遵循一个核心原则：

```text
保留职责，替换已经失效的实现方式。
```

例如：

```text
原版职责：发现可用内存
原版实现：BIOS int 0x15
当前实现：UEFI GetMemoryMap

原版职责：建立内核页表
原版实现：i386 两级页表
当前实现：x86-64 四级页表

原版职责：分配一个清零的物理页
原版实现：32 位内联汇编
当前实现：保持同一算法，改成 64 位 C
```

## 2. 当前完整启动路径

先把下面这条路径记住。以后阅读任何文件，都先问它位于路径的哪一段。

```text
QEMU
  |
  v
OVMF / UEFI 固件
  |
  | 从 FAT ESP 找到 EFI/BOOT/BOOTX64.EFI
  v
boot/uefi/main.c::efi_main
  |
  | 初始化串口
  | 获取 UEFI 内存图
  | 退出 Boot Services
  | 选择一段连续可用 RAM
  v
boot/uefi/main.c::handoff_to_kernel
  |
  | boot_info 通过 %rdi 传递
  v
boot/head.S::x86_64_start
  |
  | 切换内核栈
  | 加载内核页表到 CR3
  | 加载 GDT
  | 加载 IDT
  v
init/main.c::x86_64_kernel_main
  |
  | 检查启动环境
  | 初始化 mem_map[]
  | 测试分配和释放物理页
  v
hlt 循环
```

当前还没有进入调度器、进程、文件系统或用户态。执行到最后会停在 `hlt` 循环，这是目前的正确行为，不是启动失败。

## 3. 原版启动路径

原版路径是：

```text
BIOS
  |
  v
boot/bootsect.s
  |
  | 从磁盘读取 setup 和 system
  v
boot/setup.s
  |
  | BIOS 查询内存和显示参数
  | 开启 A20
  | 建立临时 GDT
  | 进入 32 位保护模式
  v
boot/head.s
  |
  | 建立 GDT、IDT
  | 建立两级页表
  | 开启分页
  v
init/main.c::main
  |
  | 计算 memory_end
  | mem_init
  | trap_init
  | sched_init
  | 设备和文件系统初始化
  v
进程 0 / 进程 1 / 用户态
```

原版源码没有从当前工作树消失。最可靠的阅读方式是从 `master` 分支或 Git 历史读取：

```bash
git show master:boot/bootsect.s
git show master:boot/setup.s
git show master:boot/head.s
git show master:init/main.c
git show master:mm/memory.c
```

不要为了查看原版而切换分支。`git show` 是只读操作，不会改变工作区。

---

# 第二部分：理解构建系统

## 4. 为什么首先阅读 Makefile

操作系统源码里有很多文件，但“仓库里存在”不等于“当前会被编译”。

当前真正进入 EFI 镜像的对象由 `Makefile` 的 `EFI_OBJS` 决定：

```text
boot/uefi/main.c
boot/head.S
init/main.c
mm/memory.c
kernel/panic.c
kernel/chr_drv/serial.c
```

因此阅读当前可执行路径时，应先看这些文件。其他 `fs/`、`kernel/sched.c`、`kernel/system_call.s` 等仍然是未移植的原版代码，目前没有链接进镜像。

阅读命令：

```bash
sed -n '1,180p' Makefile
```

重点找：

```make
EFI_OBJS := ...
```

它回答“最终镜像由哪些模块组成”。

## 5. 重要编译选项

### `-m64`

生成 x86-64 代码，而不是 i386 代码。

### `-ffreestanding`

告诉编译器：这不是普通用户程序，不能假定完整 C 标准库和操作系统环境存在。

### `-fno-builtin`

禁止编译器擅自把循环替换成 `memcpy`、`memset` 等库函数。当前内核还没有可靠的运行库。

### `-fpic`

生成位置无关代码。UEFI 可以把 EFI 镜像加载到不同地址，代码不能假定固定装载基址。

### `-mno-red-zone`

System V x86-64 ABI 默认允许普通函数使用栈顶以下 128 字节的 red zone。内核异常或中断可能破坏这一区域，因此内核禁用它。

### `-mgeneral-regs-only`

避免编译器自动使用 SSE、AVX 或 x87 寄存器。当前内核还没有保存和恢复这些扩展状态。

### `-fno-stack-protector`

避免编译器插入对用户态运行库 stack protector 的依赖。

### `-Wall -Wextra -Werror`

启用严格警告，并把警告视为错误。早期内核错误很难调试，最好尽量在编译期发现。

## 6. 链接结果是什么

当前不是生成传统 Linux ELF 内核，而是把引导和内核代码链接成一个 PE32+ EFI application：

```text
build/x86_64/BOOTX64.EFI
```

入口是：

```text
efi_main
```

链接命令中的关键部分：

```text
-mi386pep
--subsystem 10
--entry efi_main
--image-base 0
```

含义：

- 输出 PE32+ x86-64。
- 子系统 10 表示 EFI application。
- 固件从 `efi_main` 开始调用。
- 不依赖固定 image base。

## 7. ESP 镜像

UEFI 可移动启动路径是：

```text
EFI/BOOT/BOOTX64.EFI
```

`make image` 创建一个 FAT 文件系统镜像：

```text
build/x86_64/esp.img
```

然后将程序复制到上述路径。OVMF 会像真实 PC 固件一样寻找它。

---

# 第三部分：UEFI 启动代码

## 8. 从 `efi_main` 开始读

文件：`boot/uefi/main.c`

第一次阅读时，不要从第一行开始研究所有结构。先直接找到：

```c
efi_status_t EFIAPI efi_main(...)
```

按调用顺序记录：

```text
serial_init
refresh_memory_map
print_memory_map
leave_boot_services
handoff_to_kernel
```

这五步就是 UEFI 启动阶段的主线。

## 9. 为什么有 `EFIAPI`

UEFI x86-64 使用 Microsoft x64 ABI。内核内部 C 函数通常使用 System V x86-64 ABI。

ABI 决定：

- 参数放在哪些寄存器。
- 栈如何对齐。
- 哪些寄存器由调用者保存。
- 哪些寄存器由被调用者保存。

固件调用 `efi_main` 时，前两个参数按照 Microsoft ABI 传递。函数声明上的：

```c
#define EFIAPI __attribute__((ms_abi))
```

告诉 GCC 使用正确约定。

如果缺少它，源码可能成功编译，但函数会从错误寄存器读取参数。这类错误通常表现为一进入 `efi_main` 就崩溃。

## 10. 串口为什么重要

文件：`kernel/chr_drv/serial.c`

UEFI 自己有控制台服务，但 `ExitBootServices` 成功后不能继续使用 Boot Services。

COM1 串口是直接访问硬件 I/O 端口，不依赖 UEFI。因此它可以贯穿：

```text
进入 efi_main
退出 Boot Services
切换页表
加载 GDT/IDT
进入内核 C 函数
内存分配测试
```

串口输出是早期内核最重要的运行轨迹。

它能够证明程序执行到了某一行之后，但不能单独证明所有数据结构都完全正确。

## 11. UEFI 内存图是什么

内存图不是页表。它是一张“物理地址用途清单”。

简化示例：

```text
物理起始地址       页数       类型
0x00100000         128       LoaderData
0x00200000        4096       ConventionalMemory
0x01200000          32       ACPI
0xFEC00000           1       MMIO
```

描述符主要字段：

```text
type
physical_start
virtual_start
number_of_pages
attribute
```

其中一页始终是 4096 字节。

`ConventionalMemory` 表示普通、当前未被固件使用的 RAM。当前启动器只把这种类型作为候选内存。

## 12. 为什么描述符结构可能是 40 字节，步长却是 48 字节

本地 C 结构只声明当前已知字段，大小是 40 字节。

固件返回的 `descriptor_size` 是数组中相邻描述符起点的距离，OVMF 当前通常返回 48。

可以想象成：

```text
一个 48 字节槽位：
+--------------------------------------+--------+
| 已知公共字段 40 字节                 | 8 字节 |
+--------------------------------------+--------+
```

读取一项时只解释前 40 字节，寻找下一项时跳过完整的 48 字节。

正确遍历方式：

```text
base + index * descriptor_size
```

错误方式：

```text
descriptor[index]
```

后一种写法会按本地结构大小 40 前进，第二项开始就错位。

## 13. `refresh_memory_map()` 手把手阅读

第一次调用 `GetMemoryMap` 时通常还没有缓冲区。固件返回：

```text
EFI_BUFFER_TOO_SMALL
```

同时告诉调用者需要多大缓冲区。

启动器接着：

1. 释放旧缓冲区。
2. 为需要的大小加上若干额外描述符空间。
3. 调用 `AllocatePool`。
4. 再次调用 `GetMemoryMap`。

为什么要额外留空间？

因为分配内存图缓冲区这个动作本身会改变内存图。刚刚获得的“所需大小”可能马上过时。

这里的溢出检查属于必要的固件边界检查。它们防止错误固件数据导致分配大小回绕。

## 14. `ExitBootServices()` 为什么需要 map key

内存图附带一个 key。它表示“这份图对应当前固件内存状态”。

任何会改变内存分配的 Boot Service 都可能让 key 失效。

正确顺序：

```text
GetMemoryMap
  -> 获得 map key
  -> 立即调用 ExitBootServices(key)
```

如果返回 `EFI_INVALID_PARAMETER`，通常说明 key 已过期。代码重新获取内存图，再立即重试。

一旦成功：

- 不再调用 UEFI Boot Services。
- 不再调用 UEFI 控制台。
- 不再释放内存图缓冲区。
- 直接进入内核交接。

## 15. 为什么不把完整内存图交给内核

为了让学习路径更接近 Linux 0.11，复杂 UEFI 描述符被限制在启动器内部。

`handoff_to_kernel()` 做以下工作：

1. 遍历所有描述符。
2. 只看 `ConventionalMemory`。
3. 排除 1 MiB 以下部分。
4. 截断到 4 GiB 以下。
5. 选择页数最多的连续区间。
6. 最多保留 3840 页，即 15 MiB。
7. 生成 `mem_start` 和 `mem_end`。

为什么是 3840？

```text
Linux 0.11 最大内存：16 MiB
低端保留：           1 MiB
可分页内存：        15 MiB

15 MiB / 4 KiB = 3840 页
```

这种设计会浪费其他可用内存段，但换来一个非常简单的内核接口：

```c
mem_init(mem_start, mem_end);
```

这正是当前阶段为了学习而选择的取舍。

---

# 第四部分：UEFI 到内核的交接

## 16. `struct boot_info`

文件：`include/asm/boot.h`

```c
struct boot_info {
    __UINT64_TYPE__ mem_start;
    __UINT64_TYPE__ mem_end;
};
```

它是启动器和内核共同遵守的二进制协议。

布局：

```text
偏移 0：mem_start，8 字节
偏移 8：mem_end，8 字节
总大小：16 字节
```

为什么需要 `_Static_assert`？

因为 C 编译器可能因为字段类型或对齐规则改变结构布局。汇编代码不会自动知道这种变化，它仍然会读取固定偏移。

如果 C 认为 `mem_end` 在偏移 16，而汇编仍读偏移 8，代码可能成功编译但运行时读取垃圾值。

`_Static_assert` 让这种错误在编译期失败。

## 17. 参数怎样进入汇编

`handoff_to_kernel()` 调用：

```c
x86_64_start(&kernel_boot_info);
```

这是内核内部的 System V x86-64 调用约定。第一个整数或指针参数放在 `%rdi`。

所以 `x86_64_start` 一开始执行：

```asm
movq %rdi, %r12
```

将指针保存在 `%r12`。

为什么不能一直留在 `%rdi`？

后面还要调用多个函数。`%rdi` 是调用者保存寄存器，子函数可以修改它。`%r12` 是被调用者保存寄存器，适合在启动流程中长期保存交接指针。

---

# 第五部分：`boot/head.S`

## 18. 阅读汇编的方法

不要一次理解整个文件。按函数拆开：

```text
x86_64_start
load_page_tables
x86_64_page_table_self_test
load_gdt
load_idt
early_exception
early_breakpoint
x86_64_idt_self_test
```

每读一个函数，只回答：

```text
输入在哪里？
输出是什么？
修改了哪个 CPU 状态？
失败会到哪里？
```

## 19. `x86_64_start`

按指令阅读：

```asm
cli
```

关闭可屏蔽中断。此时内核还没有自己的完整中断系统，不能让固件遗留中断进入未知 handler。

```asm
cld
```

清除方向标志。之后 `rep stosq` 等字符串指令按地址递增方向工作。

```asm
movq %rdi, %r12
```

保存 `boot_info` 指针。

```asm
leaq boot_stack_top(%rip), %rsp
andq $-16, %rsp
```

切换到内核自己保留的 16 KiB 栈，并按 System V ABI 对齐到 16 字节。

```asm
xorq %rbp, %rbp
```

清零旧栈帧指针，切断对固件栈帧链的依赖。

接着依次调用：

```text
load_page_tables
load_gdt
load_idt
x86_64_kernel_main
```

## 20. CR3 是什么

CR3 是 x86 控制寄存器，保存当前页表根的物理地址。

x86-64 四级地址转换：

```text
虚拟地址
  -> PML4
  -> PDPT
  -> Page Directory
  -> Page Table
  -> 4 KiB 物理页
```

CR3 指向 PML4。

当前使用 2 MiB 大页，最后一级被省略：

```text
CR3 -> PML4 -> PDPT -> Page Directory -> 2 MiB 页
```

## 21. 48 位虚拟地址怎样拆分

普通四级页表中的地址位：

```text
47..39  PML4 索引，9 位
38..30  PDPT 索引，9 位
29..21  PD 索引，9 位
20..12  PT 索引，9 位
11..0   页内偏移，12 位
```

每级 9 位，因此每张表有：

```text
2^9 = 512 项
```

每项 8 字节，因此每张表大小：

```text
512 * 8 = 4096 字节
```

正好是一页。

## 22. 当前页表用了多少内存

```text
1 页 PML4
1 页 PDPT
4 页 Page Directory
总计 6 页 = 24 KiB
```

四张 Page Directory 共有：

```text
4 * 512 = 2048 项
```

每项映射 2 MiB：

```text
2048 * 2 MiB = 4 GiB
```

## 23. 为什么使用恒等映射

恒等映射表示：

```text
虚拟地址 X -> 物理地址 X
```

例如：

```text
虚拟 0x01780000 -> 物理 0x01780000
```

这样切换 CR3 时，当前指令地址、栈地址、全局变量地址和物理页指针都不需要同时改变。

它是非常适合早期启动的过渡布局，但不是最终内核虚拟地址布局。

## 24. 页表项标志

非叶子项使用：

```text
0x003
```

含义：

```text
bit 0 Present
bit 1 Writable
```

大页 PDE 使用：

```text
0x083
```

多出的：

```text
bit 7 Page Size
```

表示该 PDE 直接映射 2 MiB，而不是指向下一层 Page Table。

## 25. 写入 CR3

```asm
leaq early_pml4(%rip), %rax
movq %rax, %cr3
```

第一行取得 PML4 地址，第二行让 CPU 使用它进行地址转换。

加载 CR3 还会刷新大部分 TLB 缓存。TLB 保存最近使用的地址转换，如果不刷新，CPU 可能继续使用固件页表留下的旧结果。

代码还清除 `CR4.PGE`，避免固件的 global translation 留在 TLB 中。

## 26. `CR0.WP`

设置 `CR0.WP` 后，即使 CPU 在 ring 0，写入只读页面也会产生保护异常。

当前页面仍全部可写，所以暂时看不到区别。它为以后真正保护内核 `.text` 和 `.rodata` 做准备。

## 27. GDT 是什么

GDT 是 Global Descriptor Table。

当前 GDT 有三项：

```text
0x00 空描述符
0x08 64 位内核代码段
0x10 内核数据段
```

在 x86-64 long mode 中，普通数据段的基址和长度大多被忽略，但代码段描述符仍决定：

- 当前是否是 64 位代码。
- 当前权限级别。
- 段是否可执行。

将来进入用户态还需要用户代码段和数据段。

## 28. `lgdt` 后为什么还要重载 CS

`lgdt` 只更新 GDTR，也就是“GDT 在哪里”。

CPU 的 `CS` 内部还缓存着旧描述符内容。必须通过远控制转移重新加载它。

当前使用：

```asm
lretq
```

压入新代码段选择子和返回地址后执行长返回，让 CPU 用新 GDT 重新解释 `CS`。

随后重新加载 `DS`、`ES`、`SS`、`FS`、`GS`。

## 29. IDT 是什么

IDT 是 Interrupt Descriptor Table。

它有 256 项。事件向量号就是数组下标：

```text
0   除零
3   int3 断点
6   非法指令
14  页故障
32  常用作第一个外部 IRQ
```

x86-64 每项 16 字节，保存 handler 地址、代码段选择子和属性。

当前初始化把所有 256 项指向 `early_exception`，然后单独把第 3 项改成 `early_breakpoint`。

## 30. `int3` 自检证明什么

自检执行：

```asm
int3
```

CPU 查 IDT 第 3 项，跳到：

```asm
early_breakpoint
```

handler 设置标志，然后执行：

```asm
iretq
```

成功返回后检查标志。

它证明：

- IDTR 指向当前 IDT。
- 第 3 项编码可用。
- handler 地址正确。
- CS 选择子可用。
- `iretq` 能恢复现场。

它不能证明其他 255 项都正确，也不能证明硬件 IRQ 已经可用。

---

# 第六部分：进入 C 内核

## 31. `x86_64_kernel_main`

文件：`init/main.c`

当前它不是原版完整的 `main()`，而是一个阶段性接管点。

它按顺序验证：

```text
内核栈已经安装
CR3 指向 early_pml4
GDT/IDT 已安装
int3 能往返
boot_info 可用
mem_map 初始化正确
物理页分配正确
物理页释放正确
```

## 32. `main_mem_ok()`

它只做当前真正需要的最低检查：

- `boot_info` 不为空。
- `mem_start` 不低于 1 MiB。
- `mem_end` 大于 `mem_start`。
- 起止地址按 4 KiB 对齐。

4 GiB 上限和 15 MiB 上限已经由 UEFI 筛选及汇编入口保证，因此没有在这里重复检查。

这个例子展示了边界检查的原则：

```text
跨信任边界时检查。
内部已经建立不变量后，不重复堆叠检查。
```

---

# 第七部分：物理内存管理

## 33. 物理页、虚拟页、页表页

先区分三个概念。

### 物理页

真实 RAM 中一段 4 KiB 地址区间，例如：

```text
0x0267F000 - 0x0267FFFF
```

### 虚拟页

CPU 指令使用的虚拟地址区间。它通过页表映射到物理页。

### 页表页

用来保存页表项的物理页。它本身也是普通 4 KiB RAM，但内容被 CPU 当作地址转换结构。

当前 `get_free_page()` 只负责取得一个物理页。它还没有把任意虚拟地址映射到该页。

## 34. 原版 `mem_map[]`

原版源码：

```bash
git show master:mm/memory.c | less
```

搜索：

```text
mem_map
mem_init
get_free_page
free_page
```

当前仍采用一字节管理一页：

```c
static unsigned char mem_map[PAGING_PAGES];
```

含义：

```text
0       空闲
1       一个引用
2..99   多个引用
100     USED，不可分配
```

3840 字节即可管理 3840 个物理页，也就是 15 MiB。

## 35. `mem_init()` 手把手阅读

文件：`mm/memory.c`

第一步记录管理范围：

```c
low_mem = start_mem;
high_mem = end_mem;
```

原版 `LOW_MEM` 是固定的 1 MiB。当前物理区间由 UEFI 选择，所以低端地址必须是运行时变量。

第二步全部设置为不可用：

```c
for (...)
    mem_map[i] = USED;
```

为什么不直接全部设为 0？

因为数组固定有 3840 项，但实际交接内存可能少于 3840 页。不存在的尾部项不能被分配。

第三步计算实际页数：

```text
(high_mem - low_mem) / 4096
```

第四步把实际页对应项设置为 0。

## 36. 地址和 `mem_map` 下标的转换

从下标得到地址：

```text
address = low_mem + index * PAGE_SIZE
```

从地址得到下标：

```text
index = (address - low_mem) / PAGE_SIZE
```

举例：

```text
low_mem = 0x01780000
index   = 3

address = 0x01780000 + 3 * 0x1000
        = 0x01783000
```

反向：

```text
(0x01783000 - 0x01780000) / 0x1000 = 3
```

理解这两个公式后，`get_free_page()` 和 `free_page()` 就很容易读。

## 37. `get_free_page()`

它从 `PAGING_PAGES` 开始向下扫描。

为什么从高处开始？

这是原版行为。早期内核通常保留低地址给内核、缓冲区和硬件兼容区域，优先取高页更自然。

找到 `mem_map[index] == 0` 后：

```text
mem_map[index] = 1
page = low_mem + index * 4096
```

然后清零整页。

x86-64 的 `unsigned long` 是 8 字节，所以需要：

```text
4096 / 8 = 512 次写入
```

返回值是物理地址。因为当前前 4 GiB 恒等映射，这个数值也可以直接转换成 C 指针访问。

没有空闲页时返回 0。

## 38. 为什么新页必须清零

主要原因：

- 避免新使用者看到旧数据。
- 页表页必须从全零状态开始。
- 用户页不能泄漏其他进程内容。
- 原版 `get_free_page()` 也保证清零。

## 39. `free_page()`

步骤：

1. 低于 `low_mem` 的地址不属于主内存管理范围，直接返回。
2. 高于等于 `high_mem` 或未页对齐，调用 `panic()`。
3. 计算 `mem_map` 下标。
4. 如果计数已经是 0，说明重复释放，调用 `panic()`。
5. 否则引用计数减 1。

为什么不是直接设为 0？

因为未来写时复制会让多个页表共享同一个物理页。引用计数可能大于 1，每次释放只能减 1。

## 40. 当前 `panic()`

文件：`kernel/panic.c`

原版 panic 会使用 `printk`，判断当前任务，可能同步文件系统。

这些子系统还没有移植，所以当前早期版本只做：

```text
cli
串口打印错误
hlt 循环
```

这是阶段性实现，不代表最终 panic 已完成。

---

# 第八部分：动态自检

## 41. 为什么要在真实启动中测试

单独编译 `mm/memory.c` 只能证明 C 语法、类型和重定位正确。

它不能证明：

- UEFI 交接的地址是真实 RAM。
- 页表真的映射了该物理地址。
- 写入页面不会触发异常。
- `mem_map` 与实际区间一致。

因此 `init/main.c` 中保留动态自检。

## 42. 内存初始化自检

```text
nr_pages = (mem_end - mem_start) / PAGE_SIZE
mem_init(mem_start, mem_end)
free = nr_free_pages()
比较 free 和 nr_pages
```

成功输出：

```text
memory initialization passed
```

## 43. 页面分配自检

由于分配器从高地址向低地址找，第一个返回值应当是：

```text
last_page = mem_end - PAGE_SIZE
```

自检先把这一页全部写成 `~0UL`，确保它不是零。

然后调用 `get_free_page()`。

如果返回正确地址，而且整页变成 0，就证明：

- 扫描方向正确。
- 下标到地址的公式正确。
- 页面标记从 0 变成 1。
- 清零循环覆盖整页。
- 当前页表允许访问这段物理内存。

成功输出：

```text
physical page allocation passed
```

## 44. 页面释放自检

释放刚刚分配的页，再检查空闲页数恢复。

成功输出：

```text
physical page release passed
```

## 45. 自检不能证明什么

当前自检只测试一次正常分配和释放。它没有证明：

- 分配直到耗尽的行为。
- 多引用页面的行为。
- 重复释放时 panic 输出。
- 所有 3840 个页面都可安全访问。
- 并发分配安全。

当前尚未启用中断和多任务，所以并发问题还不是本阶段阻塞项。

---

# 第九部分：静态验证工具

## 46. `make check`

执行：

```bash
make clean
make check
```

它检查：

- 输出确实是 PE32+ EFI application。
- 架构确实是 x86-64。
- 入口点非零。
- 最终镜像没有未定义符号。
- 对象文件没有 `R_X86_64_32` 或 `R_X86_64_32S`。

## 47. 为什么禁止绝对 32 位重定位

UEFI 可以把镜像加载到任意较高地址。

如果某个地址被写成 32 位绝对值，高 32 位会被截断。代码在低地址测试时可能工作，换一个装载位置就崩溃。

允许的典型形式是 RIP-relative：

```text
R_X86_64_PC32
```

它表达“目标相对当前指令有多远”，不依赖固定镜像基址。

## 48. `nm`

查看最终符号：

```bash
nm -n build/x86_64/BOOTX64.EFI | less
```

寻找：

```text
efi_main
x86_64_start
x86_64_kernel_main
mem_init
get_free_page
free_page
panic
```

`nm -u` 查看未定义符号：

```bash
nm -u build/x86_64/BOOTX64.EFI
```

正确结果应为空。

## 49. `objdump`

反汇编内存管理：

```bash
objdump -dr build/x86_64/mm/memory.o | less
```

你可以观察：

- `get_free_page` 是否从高下标向低下标扫描。
- 清零循环是否写入 512 个 8 字节字。
- 全局变量是否通过 RIP-relative 地址访问。

查看 PE 头：

```bash
objdump -x build/x86_64/BOOTX64.EFI | less
```

## 50. `readelf`

对象文件仍然是 ELF relocatable object，最终才链接成 PE。

查看重定位：

```bash
readelf -r build/x86_64/mm/memory.o
```

它能证明编译器生成了什么链接请求，但不能证明运行时地址转换正确。

## 51. QEMU/OVMF

运行：

```bash
make run
```

程序最终不会退出，可以使用：

```bash
timeout 12s make run
```

返回码 124 表示被 `timeout` 终止，这是预期结果。

重点不是返回码，而是串口最后几行。

当前成功输出示例：

```text
Linux 0.11 x86-64: efi_main reached
UEFI memory map: entries=128
UEFI descriptor size: 48
UEFI boot services exited
UEFI selected main memory: 0x0000000001780000 - 0x0000000002680000
x86-64 kernel stack installed
x86-64 kernel main reached
x86-64 page tables installed
x86-64 CR3 self-test passed
x86-64 descriptor tables installed
x86-64 IDT int3 self-test passed
main memory pages: 3840
free memory pages: 3840
memory initialization passed
physical page allocation passed
physical page release passed
```

内存图条目数和 ConventionalMemory 总页数可能变化，不应写成固定断言。

---

# 第十部分：如何对照原版学习

## 52. 不要直接看总 diff

下面的命令变化太大，不适合第一次学习：

```bash
git diff master..x86-64
```

它会同时展示 BIOS、UEFI、汇编、构建和内存管理变化，很容易失去主线。

正确方法是“一次对照一个职责”。

## 53. 对照任务一：内存发现

先读原版：

```bash
git show master:boot/setup.s | less
```

搜索：

```text
0x15
0x90002
```

理解：BIOS 返回扩展内存 KiB 数，代码写到固定物理地址。

再读原版 `init/main.c`：

```bash
git show master:init/main.c | less
```

搜索：

```text
EXT_MEM_K
memory_end
main_memory_start
mem_init
```

最后读当前：

```bash
sed -n '54,228p' boot/uefi/main.c
sed -n '1,30p' include/asm/boot.h
sed -n '1,75p' init/main.c
```

写下对应关系：

```text
BIOS EXT_MEM_K
    -> UEFI memory map

原版 memory_end
    -> boot_info.mem_end

原版 main_memory_start
    -> boot_info.mem_start
```

## 54. 对照任务二：早期分页

先读原版：

```bash
git show master:boot/head.s | less
```

搜索：

```text
setup_paging
pg_dir
```

观察：

- 一个页目录。
- 四个页表。
- 4096 个 4 KiB PTE。
- 映射前 16 MiB。

再读当前：

```bash
sed -n '35,122p' boot/head.S
```

观察：

- 一个 PML4。
- 一个 PDPT。
- 四个 Page Directory。
- 2048 个 2 MiB PDE。
- 映射前 4 GiB。

把差异写成表：

| 问题 | 原版 | 当前 |
|---|---|---|
| 页表层级 | 2 | 4 |
| 叶子页大小 | 4 KiB | 2 MiB |
| 映射范围 | 16 MiB | 4 GiB |
| CR3 指向 | Page Directory | PML4 |

## 55. 对照任务三：物理页初始化

先读取原版函数：

```bash
git show master:mm/memory.c | sed -n '/void mem_init/,/^}/p'
```

再看当前：

```bash
sed -n '18,45p' mm/memory.c
```

回答：

1. 为什么都先写 `USED`？
2. 为什么当前 `LOW_MEM` 变成 `low_mem`？
3. 为什么 `PAGING_PAGES` 仍然是 3840？
4. 哪些地方保持不变？

## 56. 对照任务四：分配物理页

原版：

```bash
git show master:mm/memory.c | sed -n '/unsigned long get_free_page/,/^}/p'
```

当前：

```bash
sed -n '47,71p' mm/memory.c
```

不要逐指令机械翻译。先把原版汇编写成伪代码：

```text
从 mem_map 尾部找 0
找到后写 1
计算物理地址
清零 4096 字节
返回地址
```

然后确认当前 C 代码是否实现同样职责。

## 57. 对照任务五：释放物理页

原版：

```bash
git show master:mm/memory.c | sed -n '/void free_page/,/^}/p'
```

当前：

```bash
sed -n '73,100p' mm/memory.c
```

重点理解引用计数，而不是只看 `--mem_map[index]`。

---

# 第十一部分：一套可重复的学习方法

## 58. 每个函数都写一张四列表

示例：

| 项目 | `get_free_page()` |
|---|---|
| 输入 | 无 |
| 输出 | 物理页地址或 0 |
| 修改状态 | `mem_map[index]`，以及物理页内容 |
| 不变量 | 返回地址页对齐，并在 `[low_mem, high_mem)` |

再为 `free_page()`、`mem_init()`、`load_page_tables()` 分别写一张。

## 59. 把代码分成三类

### 保留的 Linux 0.11 逻辑

例如：

- `mem_map[]` 引用计数。
- 高地址优先分配。
- 新页清零。
- 重复释放是内核错误。

### 必须替换的架构代码

例如：

- BIOS 中断。
- i386 两级页表。
- 32 位寄存器内联汇编。
- 实模式到保护模式切换。

### 新平台脚手架

例如：

- UEFI ABI 声明。
- PE32+ 构建。
- OVMF pflash 配置。
- `ExitBootServices` 重试。

不要把第三类误认为原版 Linux 子系统已经移植完成。

## 60. 每个里程碑只回答一个问题

已有提交可以作为学习索引：

```text
2066580 boot: add minimal x86-64 UEFI application
3c96083 boot: make x86-64 UEFI image position independent
714a247 boot: exit UEFI boot services
73685ac x86_64: enter kernel on an owned stack
e29f7b2 x86_64: install early descriptor tables
82e2680 x86_64: install early page tables
923dead boot: pass a simple memory range
33295f1 mm: add early memory map
fe53c08 mm: initialize physical pages
8f8d594 mm: allocate physical pages
d272e5c kernel: add early panic
68cd04e mm: release physical pages
ef35b8f style: simplify early memory code
```

逐提交阅读：

```bash
git show --stat 8f8d594
git show 8f8d594
```

先看 `--stat` 确定范围，再看完整 diff。

## 61. 先预测，再运行

运行前先写下你预计看到的最后一行。

例如学习 `get_free_page()` 时预测：

```text
physical page allocation passed
```

如果输出不一致，再根据最后成功行定位故障阶段。

这样比“运行一下看看”更能建立因果理解。

## 62. 区分证据强度

### 编译通过

证明语法、类型、警告约束通过。

不证明硬件行为正确。

### 链接通过

证明所有当前使用的符号都有定义。

不证明 ABI 正确。

### 静态格式检查通过

证明镜像格式、入口和重定位满足预期。

不证明页表内容正确。

### QEMU 串口继续输出

证明 CPU 实际越过了相应阶段。

不证明未执行的错误路径正确。

### 有目的的动态自检

比单纯“继续运行”更强。例如先写非零再验证新页清零，能够证明清零逻辑确实执行。

## 63. 不要过早阅读未链接代码

当前暂时不要深入：

```text
kernel/sched.c
kernel/system_call.s
fs/*
kernel/blk_drv/*
```

它们仍包含大量 i386 分段、任务切换和 32 位汇编假设。等页表、异常和用户地址空间具备后再读，理解成本会低得多。

---

# 第十二部分：下一阶段

## 64. 为什么下一步不是直接恢复 `fork()`

`fork()` 依赖：

- 可分配物理页。
- 可动态建立页表。
- 可复制或共享用户页。
- 页故障处理。
- 任务结构和上下文切换。

当前只完成第一项。

## 65. 下一项架构问题：2 MiB 大页与 4 KiB 映射

原版 `put_page()` 修改 4 KiB PTE。

当前前 4 GiB 由 2 MiB PDE 直接映射，没有最后一级 Page Table。

因此不能直接照搬原版 `put_page()`。需要先具备：

1. 取得当前 PML4。
2. 遍历 PML4、PDPT、PD。
3. 必要时用 `get_free_page()` 分配页表页。
4. 如果目标 PDE 是 2 MiB 大页，将它拆成 512 个保持原映射的 4 KiB PTE。
5. 修改目标 PTE。
6. 刷新对应 TLB 项。

这是“保留 `put_page` 职责、替换页表实现”的典型移植工作。

## 66. 后续推荐顺序

```text
动态页表基础
  -> put_page
  -> get_empty_page
  -> free_page_tables
  -> copy_page_tables
  -> IDT 向量 14 页故障
  -> do_no_page
  -> 写时复制
  -> 调度器
  -> fork
  -> 用户态
  -> 文件系统
```

## 67. 当前明确未完成

- 动态 4 KiB 页表。
- `put_page()` 实现。
- `get_empty_page()`。
- 页表复制和释放。
- 页故障处理。
- 写时复制。
- 用户态 GDT 项和 TSS。
- IRQ、时钟、PIC/APIC。
- 调度器和任务切换。
- 系统调用入口。
- 块设备、根文件系统和程序装载。
- 最终高半内核布局。
- NX 和 `.text/.rodata/.data` 分页权限。

---

# 第十三部分：建议的实际学习日程

## 第 1 次：只理解启动顺序

阅读：

```text
Makefile
boot/uefi/main.c::efi_main
boot/head.S::x86_64_start
init/main.c::x86_64_kernel_main
```

目标：不看细节，能画出调用箭头。

## 第 2 次：只理解 UEFI 内存交接

阅读：

```text
refresh_memory_map
leave_boot_services
handoff_to_kernel
include/asm/boot.h
```

目标：解释为什么内核只收到 `mem_start/mem_end`。

## 第 3 次：只理解栈、GDT、IDT、CR3

阅读：

```text
x86_64_start
load_page_tables
load_gdt
load_idt
```

目标：分别用一句话解释四者职责。

## 第 4 次：只理解 `mem_map`

对照原版和当前：

```text
mem_init
get_free_page
free_page
```

目标：可以手算任意下标对应的物理地址。

## 第 5 次：只理解验证证据

执行：

```bash
make clean
make check
timeout 12s make run
```

目标：解释每个命令证明什么、不能证明什么。

## 第 6 次：逐提交重放思路

不要真的 reset。只使用：

```bash
git show --stat <commit>
git show <commit>
```

目标：每个提交用一句话概括“新增了哪个可验证能力”。

---

# 第十四部分：自测问题

## 基础问题

1. 为什么 UEFI 内存图不是页表？
2. 为什么退出 Boot Services 需要最新 map key？
3. 为什么 `boot_info` 需要静态布局断言？
4. 为什么 `%rdi` 中的指针先保存到 `%r12`？
5. 为什么内核不能继续使用固件栈？
6. CR3 中保存的是什么？
7. 当前为什么只需要 6 页启动页表？
8. GDT 和 IDT 的职责有什么区别？
9. 为什么 `int3` 可以测试 IDT？
10. `mem_map[index] == 2` 表示什么？

## 计算问题

1. 15 MiB 有多少个 4 KiB 页？
2. 4 个 Page Directory 使用 2 MiB PDE 能映射多少内存？
3. `low_mem=0x01780000` 时，`index=10` 对应什么地址？
4. `address=0x01785000` 对应什么下标？
5. 为什么一张 x86-64 页表正好是 4096 字节？

## 深入问题

1. 如果直接用 40 字节步长遍历 OVMF 的 48 字节描述符，会发生什么？
2. 如果 `get_free_page()` 不清零，未来用户进程会有什么安全问题？
3. 如果写入新 CR3 后代码仍能输出串口，至少证明了哪些映射有效？
4. 为什么当前不能直接实现原版 `put_page()`？
5. 为什么 `free_page()` 使用减引用计数，而不是直接写 0？

## 参考答案要点

1. 内存图描述物理地址用途；页表描述虚拟地址到物理地址的转换。
2. 固件内存分配变化会使旧 key 失效。
3. C 和汇编必须对字段偏移达成同一二进制协议。
4. `%rdi` 会被后续调用覆盖，`%r12` 适合长期保存。
5. 退出固件后内核必须拥有可控、可映射、符合 ABI 的栈。
6. 当前四级页表根 PML4 的物理地址。
7. 1 PML4 + 1 PDPT + 4 PD。
8. GDT 描述执行模式和权限；IDT 描述异常/中断入口。
9. `int3` 强制 CPU 查询 IDT 第 3 项并通过 `iretq` 返回。
10. 两个引用共享同一物理页。

计算答案：

```text
15 MiB / 4 KiB = 3840
4 * 512 * 2 MiB = 4 GiB
0x01780000 + 10 * 0x1000 = 0x0178A000
(0x01785000 - 0x01780000) / 0x1000 = 5
512 项 * 8 字节 = 4096 字节
```

---

# 第十五部分：常用命令速查

查看当前真正修改状态：

```bash
git status --short
```

查看最近提交：

```bash
git log --oneline --decorate -20
```

查看一个提交范围：

```bash
git show --stat <commit>
```

查看原版文件：

```bash
git show master:<path>
```

查看当前文件带行号：

```bash
nl -ba <path> | less
```

搜索函数：

```bash
rg -n "函数名" .
```

干净构建：

```bash
make clean
make check
```

动态启动：

```bash
timeout 12s make run
```

查看符号：

```bash
nm -n build/x86_64/BOOTX64.EFI | less
```

查看反汇编：

```bash
objdump -dr build/x86_64/mm/memory.o | less
```

查看重定位：

```bash
readelf -r build/x86_64/mm/memory.o
```

---

# 最后的学习原则

不要用“代码行数”衡量移植进度，要用“已经恢复了哪些原版职责”衡量。

当前已经恢复：

```text
启动加载入口
退出固件服务
内核自有栈
内核自有 GDT
内核自有 IDT
内核自有 CR3 和早期页表
物理内存范围交接
mem_map 初始化
物理页分配
物理页释放
早期 panic
```

当前尚未恢复：

```text
动态页表
页故障
写时复制
调度
进程
用户态
系统调用
设备
文件系统
```

最有效的学习方式始终是：

```text
先找到原版职责
再写出不变量
然后识别架构专属实现
最后查看当前如何替换它
用编译、静态检查和动态自检分别验证
```

当你能够不用看代码，独立解释下面这条链时，就真正理解了当前阶段：

```text
UEFI 内存图
  -> 连续 RAM 区间
  -> boot_info
  -> x86_64_start
  -> CR3/GDT/IDT
  -> mem_init
  -> mem_map
  -> get_free_page
  -> free_page
```
