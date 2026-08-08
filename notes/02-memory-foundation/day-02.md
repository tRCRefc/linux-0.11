# 阶段二：Linux 0.11 x86-64 移植 DAY 2 学习记录

日期：2026-08-02

## 1. 当前目标

这个分支不是重新写一个全新的操作系统，而是保留 Linux 0.11 的核心职责和函数关系，替换不能在现代 UEFI x86-64 环境中工作的硬件接口。

DAY 1 完成了启动环境的建立。DAY 2 开始进入原版 Linux 0.11 的内存管理。

当前已经做到：UEFI 找到一段连续的可用物理内存，内核接收这段区间，并用原版风格的 `mem_map[]` 管理其中的 4 KiB 页面。

## 2. 先看完整启动流程

启动顺序如下：

```text
UEFI 固件
  -> boot/uefi/main.c::efi_main
  -> GetMemoryMap
  -> ExitBootServices
  -> handoff_to_kernel
  -> boot/head.S::x86_64_start
  -> 安装内核栈、页表、GDT、IDT
  -> init/main.c::x86_64_kernel_main
  -> mem_init
  -> get_free_page / free_page 自检
```

原版 Linux 0.11 的对应流程是：

```text
BIOS
  -> boot/bootsect.s
  -> boot/setup.s
  -> boot/head.s
  -> init/main.c::main
  -> mm/memory.c::mem_init
```

两条流程的职责大致相同，但接口不同：

| 原版职责 | 当前替代 |
|---|---|
| BIOS 加载 bootsect | UEFI 加载 PE32+ EFI 程序 |
| BIOS `int 0x15` 查询内存 | UEFI `GetMemoryMap` |
| `setup.s` 进入保护模式 | 固件已经进入 x86-64 long mode |
| i386 两级页表 | x86-64 四级页表 |
| `head.s` 建立早期环境 | `boot/head.S` 建立栈、GDT、IDT、CR3 |
| `EXT_MEM_K` 和 `memory_end` | UEFI 选出的 `mem_start`、`mem_end` |
| `mem_map[]` 管理 1 MiB 以上页面 | 当前仍使用 `mem_map[3840]` |

## 3. UEFI 侧做了什么

文件：[boot/uefi/main.c](../../boot/uefi/main.c)

### 3.1 获取内存图

UEFI 的内存图是一个描述符数组。每个描述符告诉我们一段物理内存的：

- 类型
- 起始地址
- 页数
- 属性

UEFI 返回的描述符大小可能是 48 字节，而我们当前已知的公共字段只有 40 字节。因此遍历时使用固件返回的 `descriptor_size`，不能假定相邻描述符间隔是 `sizeof(struct efi_memory_descriptor)`。

### 3.2 退出 Boot Services

`ExitBootServices` 必须使用最新内存图对应的 key。如果 key 失效，就重新获取内存图并重试。

成功退出后，不再调用 UEFI Boot Services。串口输出继续使用 COM1，因此退出固件后仍然能够诊断内核状态。

### 3.3 只交给内核一段简单区间

为了让内核更接近原版 Linux 0.11，UEFI 侧不把原始描述符数组交给内核，而是在 `handoff_to_kernel()` 中：

1. 只选择 `ConventionalMemory`。
2. 忽略 1 MiB 以下区域。
3. 忽略 4 GiB 以上区域，因为当前早期页表只映射前 4 GiB。
4. 选择最大的连续区间。
5. 最多交给内核 3840 页，也就是 15 MiB。
6. 通过 `struct boot_info` 传递 `mem_start` 和 `mem_end`。

这相当于把复杂的 UEFI 内存图转换成了原版 `mem_init(start_mem, end_mem)` 所需要的接口。

## 4. 内核交接协议

文件：[include/asm/boot.h](../../include/asm/boot.h)

当前协议只有两个 64 位字段：

```c
struct boot_info {
    __UINT64_TYPE__ mem_start;
    __UINT64_TYPE__ mem_end;
};
```

布局检查保证：

- 结构总大小为 16 字节。
- `mem_end` 位于偏移 8。

文件：[boot/head.S](../../boot/head.S)

汇编入口从 `%r12` 取得 `boot_info` 指针，然后读取偏移 0 和偏移 8 的两个地址，确认：

- 结束地址大于起始地址。
- 结束地址仍在 4 GiB 范围内。

之后才加载内核自己的页表、GDT 和 IDT。

## 5. 原版 `mem_init()` 如何工作

原版代码可以从 Git 历史查看：

```bash
git show f003a23^:mm/memory.c
```

原版的核心设计是：

```text
mem_map[i] == 0       页面空闲
mem_map[i] == 1       页面被一个使用者占用
mem_map[i] >= 2       页面被多个地址空间共享
mem_map[i] == USED    这项不是可分配主内存
```

当前实现保留了最重要的初始化逻辑：

文件：[mm/memory.c](../../mm/memory.c)

```c
static unsigned char mem_map[PAGING_PAGES];
```

`mem_init()` 先把全部 3840 项设置为 `USED`，再把交接区间对应的页面设置为 0。

如果 UEFI 交接：

```text
mem_start = 0x01780000
mem_end   = 0x02680000
```

那么：

```text
(mem_end - mem_start) / 4096 = 3840
```

内核就会开放这 3840 个页面。

## 6. `get_free_page()` 如何工作

当前实现仍然遵循原版的“从高地址向低地址找”：

```text
index = PAGING_PAGES - 1
while index >= 0:
    如果 mem_map[index] != 0，继续向前
    mem_map[index] = 1
    page = low_mem + index * PAGE_SIZE
    清零 page 开始的 4096 字节
    返回 page
返回 0
```

原版使用 i386 汇编完成扫描和清零，当前改成了 64 位 C 循环。原因不是改变算法，而是避免继续依赖 32 位寄存器、32 位地址和 i386 页表布局。

文件：[mm/memory.c](../../mm/memory.c)

## 7. `free_page()` 如何工作

当前实现保留原版引用计数思想：

- 低于 `low_mem` 的地址直接忽略。
- 高于等于 `high_mem` 的地址触发 `panic`。
- 非页对齐地址触发 `panic`。
- 已经是 0 的页面再次释放，触发 `panic`。
- 合法页面的 `mem_map[index]` 减 1。

文件：[mm/memory.c](../../mm/memory.c)

文件：[kernel/panic.c](../../kernel/panic.c)

当前早期 `panic()` 不依赖调度器、文件系统同步或 `printk`，只通过串口打印并停机。这是为了让内存管理能够在调度器尚未移植时先运行。

## 8. 当前启动自检

文件：[init/main.c](../../init/main.c)

当前内核入口执行以下检查：

1. 检查 `boot_info` 和内存区间是否合法。
2. 调用 `mem_init()`。
3. 确认空闲页数为 3840。
4. 把最高空闲页预先填成非零值。
5. 调用 `get_free_page()`。
6. 确认返回的是最高页。
7. 确认页面已被清零。
8. 确认空闲页数减少 1。
9. 调用 `free_page()`。
10. 确认空闲页数恢复。

成功输出：

```text
memory initialization passed
physical page allocation passed
physical page release passed
```

## 9. 如何阅读原版和当前代码

推荐顺序如下。

### 第一步：先读原版职责

查看原版启动代码：

```bash
git show master:boot/bootsect.s
git show master:boot/setup.s
git show master:boot/head.s
```

查看原版主初始化：

```bash
git show master:init/main.c
```

重点寻找：

- 内存从哪里获得。
- 参数如何交给内核。
- `mem_init()` 在什么时候调用。
- `get_free_page()` 返回的是什么地址。

### 第二步：再读当前替代

```bash
sed -n '1,320p' boot/uefi/main.c
sed -n '1,180p' boot/head.S
sed -n '1,140p' init/main.c
sed -n '1,180p' mm/memory.c
```

不要一开始通读整个仓库。先按一个职责对照：

```text
原版 setup.s 的内存发现
    对照 boot/uefi/main.c 的内存图筛选

原版 init/main.c 的 memory_end
    对照 include/asm/boot.h 的 mem_start/mem_end

原版 mem_init
    对照当前 mm/memory.c 的 mem_map 初始化

原版 get_free_page
    对照当前 mm/memory.c 的高地址扫描和清零
```

### 第三步：查每个函数的输入和输出

对每个函数写出四件事：

```text
输入是什么？
输出是什么？
修改了哪个全局状态？
失败时应该怎样处理？
```

例如 `get_free_page()`：

```text
输入：无
输出：一个物理页地址，或者 0
修改：mem_map 中对应项从 0 变成 1，并清零物理页
失败：没有空闲页时返回 0
```

### 第四步：最后看架构差异

只有当原版职责已经理解后，再看这些变化：

- i386 的 32 位寄存器变成 x86-64 寄存器。
- 原版两级页表变成四级页表。
- 原版固定 `LOW_MEM` 变成 UEFI 选择出的 `low_mem`。
- UEFI 负责发现并筛选物理内存，内核负责管理页面。

## 10. 如何通过源码学习

每次只学习一个职责，不要同时移植整个子系统。

推荐固定流程：

```text
读原版函数
  -> 写下输入、输出和不变量
  -> 找出 i386 专属部分
  -> 保留职责，替换硬件接口
  -> 先编译
  -> 再做启动自检
  -> 最后提交一个小 commit
```

阅读汇编时，重点看寄存器和内存布局，不要先纠结每条指令：

```bash
objdump -dr build/x86_64/mm/memory.o
nm -S build/x86_64/BOOTX64.EFI
readelf -r build/x86_64/mm/memory.o
```

运行时，串口输出回答的是“代码实际走到了哪里”；它不能证明所有边界情况都正确，所以还要结合源码和静态检查。

每个提交都应该只表达一个意思，例如：

```text
mm: allocate physical pages
mm: release physical pages
kernel: add early panic
```

## 11. 当前还没有完成的部分

以下内容尚未移植：

- 动态 4 KiB 页表映射。
- `put_page()` 和 `get_empty_page()`。
- `copy_page_tables()` 和 `free_page_tables()`。
- 页故障处理和写时复制。
- 中断、时钟和调度器。
- 进程 0/1、`fork()` 和用户态。
- 块设备、文件系统和程序装载。

下一阶段应继续从原版 `put_page()` 开始，但首先要解决当前启动页表使用 2 MiB 大页、而 Linux 0.11 后续需要 4 KiB 页表这一架构差异。
