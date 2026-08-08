# Linux 0.11 x86-64 当前代码阅读学习指南

历史适用基线：`5d0f848 kernel: wait for child tasks`

更新日期：2026-08-05

本文档原为本地资料，当前仅通过一次性迁移快照纳入 Git；后续策略见根目录 `MIGRATION.md`。

> 状态补记（2026-08-06）：本文保留 `5d0f848` 时的逐文件阅读顺序。之后的页异常边界、
> SIGCHLD 唤醒和用户错误隔离见
> [`阶段五`](../05-process-boundaries/README.md)。当前代码基线是 `462db18`。

这份指南面向当前可执行代码，而不是仓库中的所有历史 i386 文件。目标是让阅读者能够回答：

1. 当前镜像实际编译了什么；
2. CPU 从 UEFI 到 CPL3 经过哪些状态变化；
3. 四级页表、COW、任务切换和 int 0x80 怎样连成一条路径；
4. 哪些语义来自 Linux 0.11，哪些实现是 long mode 替换；
5. 当前代码能证明什么，哪些仍只是阶段性脚手架；
6. 修改前应该保护哪些跨 C/汇编不变量。

综合问题清单见 `../review/vm-review-2026-08-05.md`，原版逐项对照见
`linux-0.11-comparison.md`，下一提交见 `../../NEXT_STEP.md`。

## 1. 第一原则：先看构建边界

仓库有 100 多个受跟踪文件，但当前 x86-64 镜像只链接 `Makefile` 中的 12 个对象。阅读任何代码
前先看：

```bash
sed -n '1,220p' Makefile
```

重点变量：

```text
EFI_OBJS
X86_64_CFLAGS
X86_64_ASFLAGS
EFI_IMAGE
ESP_IMAGE
OVMF_CODE / OVMF_VARS
```

当前正式对象：

| 顺序 | 源文件 | 核心职责 |
| ---: | --- | --- |
| 1 | `boot/uefi/main.c` | UEFI 入口、内存图、退出 Boot Services |
| 2 | `boot/head.S` | 自有栈、页表、GDT、IDT、TSS、CPL3 帮助函数 |
| 3 | `init/main.c` | 正式内核 C 入口和物理内存初始化 |
| 4 | `mm/memory.c` | 物理页和四级页表策略 |
| 5 | `mm/page.S` | #PF 保存现场、分流、iretq |
| 6 | `kernel/sched.c` | task 0、调度选择、切换协调、getpid |
| 7 | `kernel/switch.S` | 软件保存/恢复内核上下文 |
| 8 | `kernel/system_call.S` | int 0x80 保存现场、分派和返回 |
| 9 | `kernel/fork.c` | PID、任务页、子现场和页表复制 |
| 10 | `kernel/exit.c` | exit、zombie、waitpid 和回收 |
| 11 | `kernel/panic.c` | 轮询串口 panic 后停机 |
| 12 | `kernel/chr_drv/serial.c` | COM1 轮询输出 |

`fs/`、`kernel/system_call.s`、`mm/page.s`、块设备和 TTY 文件仍有学习价值，但没有进入当前镜像。
阅读时必须把“原版参考代码”和“当前运行代码”分开。

## 2. 推荐的五遍阅读法

不要第一次就逐行读完整 `mm/memory.c`。按五遍推进：

```text
第一遍：构建和启动调用图
第二遍：地址布局与页表所有权
第三遍：异常和系统调用栈帧
第四遍：fork/schedule/exit/wait 生命周期
第五遍：逐项对照 master 中的原版职责
```

每一遍只回答少量问题，并实际用 `objdump` 或 QEMU 输出核对。

## 3. 第一遍：从固件走到内核

### 3.1 UEFI 主线

先读 `boot/uefi/efi.h`，但只记住四件事：

- `EFIAPI` 是 Microsoft x64 ABI；
- EFI status 的最高位表示错误；
- memory descriptor 实际 stride 由固件返回，不能假定等于 C struct 大小；
- Boot Services 表中只声明当前实际调用的三个服务。

然后读 `boot/uefi/main.c`，只沿调用链：

```text
efi_main
-> serial_init
-> refresh_memory_map
-> print_memory_map
-> leave_boot_services
-> handoff_to_kernel
-> x86_64_start
```

阅读问题：

1. 为什么第一次 `GetMemoryMap` 允许 buffer 为 NULL？
2. 为什么分配 memory-map buffer 后必须重新取 map key？
3. 为什么 `ExitBootServices` 返回 `EFI_INVALID_PARAMETER` 时还要重试？
4. 为什么只选 `EFI_CONVENTIONAL_MEMORY`？
5. 为什么限制在 4 GiB 以下且最多 3840 页？

关键结论：这里选择的是“交给早期页分配器的一段连续物理 RAM”，不是建立页表，也不是把所有
UEFI conventional memory 都纳入管理。

### 3.2 x86_64_start

读 `boot/head.S:22-39`：

```text
cli / cld
-> 保存 boot_info
-> 切到 boot_stack_top
-> load_page_tables
-> load_gdt
-> load_idt
-> sched_init
-> x86_64_kernel_main
```

注意调用 `sched_init()` 发生在 `mem_init()` 之前。task 0 是静态对象，不需要物理页分配；它只
记录当前早期 CR3 和启动内核栈顶。

### 3.3 正式入口的真实终点

读 `init/main.c` 全文件。它只有 33 行：

```text
打印启动状态
-> mem_init(mem_start, mem_end)
-> 打印 free pages
-> 永久 hlt
```

这说明正式镜像不会自动进入 CPL3，也不会执行 fork。任何关于进程闭环的理解都必须来自源码、
反汇编和已记录的专项探针，不能从普通启动输出推导。

## 4. 第二遍：建立地址地图

先读 `include/linux/mm.h`，画出当前虚拟地址空间：

```text
0x0000000000000000
  |
  |  PML4[0]: 低 4 GiB identity map，supervisor-only，2 MiB 页
  v
0x0000000100000000

0x0000008000000000
  |
  |  PML4[1..255]: 每任务用户区，4 KiB 页
  v
0x0000800000000000  exclusive limit

0xffff800000000000
  |
  |  PML4[256]: 前 4 GiB 物理窗口，supervisor-only，2 MiB 页
  v
0xffff800100000000
```

### 4.1 为什么既有 identity map 又有物理窗口

identity map 让 UEFI 装载的当前镜像在切 CR3 后继续以原地址执行。高半物理窗口让内核在任意
进程 CR3 中稳定访问页表页、task 页和受管物理数据页：

```c
phys_to_virt(page)
```

如果只保留 identity map，用户区和将来内核布局很容易冲突；如果只保留高半映射，切页表瞬间
还需要先把当前执行代码和栈重定位过去。现在的双映射是明确的过渡设计。

### 4.2 读早期页表构造

按 `boot/head.S:83-136` 阅读：

1. 检查当前镜像关键地址在 4 GiB 以下；
2. 清空 6 页早期页表；
3. 生成 2048 个 2 MiB PDE，覆盖 4 GiB；
4. 4 个 PD 接到一个 PDPT；
5. 同一 PDPT 接到 PML4[0] 和 PML4[256]；
6. 加载 CR3；
7. 关闭 PGE，打开 CR0.WP。

阅读时手算：

```text
2048 * 2 MiB = 4 GiB
4 * 512 PDE = 2048 PDE
PML4[256] -> 0xffff800000000000
```

### 4.3 物理页分配器

读 `mm/memory.c:10-97`：

- `mem_map` 固定 3840 字节；
- `low_mem/high_mem` 让同一数组管理任意被选中的连续区；
- 分配器从高页向低页扫描；
- `get_free_page()` 返回物理地址，但用物理窗口清零；
- `free_page()` 递减引用计数，低于管理区的页忽略，非法高地址和双重释放 panic。

与原版对照：

```bash
git show master:mm/memory.c | sed -n '45,180p'
```

重点比较算法，不比较原版内联汇编与当前 C 循环的写法。

### 4.4 四级页表 API 的阅读顺序

不要按文件顺序读。按资源生命周期：

```text
new_pg_dir
-> put_user_page
-> switch_pg_dir
-> copy_pg_dir
-> do_wp_page
-> free_pg_dir
```

对每个函数记录三个问题：

1. 输入/输出是物理地址还是虚拟地址？
2. 新分配了哪些页，失败时谁释放？
3. 修改了当前 CR3 可见的页表后在哪里失效 TLB？

### 4.5 copy_pg_dir 的递归结构

```text
copy_pg_dir: 分配 PML4
  -> copy_pdpt: 分配 PDPT
     -> copy_pd: 分配 PD
        -> copy_pt: 分配 PT
           -> PTE 清 PAGE_WRITE
           -> 父 PTE 同样清 PAGE_WRITE
           -> mem_map[data_page]++
```

每一级失败都释放已经完成的下级结构。父 PTE 可能已经只读，但对应引用计数经回滚重新为 1；
父下一次写入时 COW handler 会直接恢复 PAGE_WRITE。这是可以接受的失败后状态。

## 5. 第三遍：读 CPU 栈帧

### 5.1 页异常帧

CPU 从 CPL3 进入 #PF 时，在 TSS.rsp0 指向的内核栈上建立：

```text
高地址
SS
RSP
RFLAGS
CS
RIP
error code
低地址 <- CPU 完成后的 RSP
```

`mm/page.S` 再压入 15 个 GPR。每项 8 字节，所以错误码位于新 RSP 的：

```text
15 * 8 = 120 bytes
```

这对应 `movq 120(%rsp), %rdi`。CR2 放入 RSI，满足 SysV C 参数约定：

```c
do_no_page(error, addr)
do_wp_page(error, addr)
```

调用 C 前把 RSP 向下对齐到 16 字节，返回后用保存在 RBX 中的值恢复原栈，再弹寄存器、跳过
error code 并 `iretq`。

### 5.2 int 0x80 的 pt_regs

CPU 从 CPL3 执行 `int $0x80` 时没有错误码，先压 5 项：

```text
SS RSP RFLAGS CS RIP
```

`kernel/system_call.S` 依次压入 15 个 GPR。最终布局必须逐项匹配
`include/asm/ptrace.h::struct pt_regs`：

```text
offset 0    r15
...
offset 104  rbx
offset 112  rax
offset 120  rip
offset 128  cs
offset 136  rflags
offset 144  rsp
offset 152  ss
sizeof      160
```

汇编中的 `.equ RAX, 112` 是硬不变量。修改 `pt_regs` 前必须同步检查汇编和子进程栈构造。

反汇编命令：

```bash
objdump -dr build/x86_64/kernel/system_call.o
objdump -dr build/x86_64/mm/page.o
```

### 5.3 为什么 syscall table 存 32 位相对偏移

位置无关 PE 镜像不能依赖绝对 32 位内核地址。表项保存：

```text
target - current_entry_address
```

入口加载有符号 32 位值，再加回表项地址得到函数地址。`make check` 明确拒绝
`R_X86_64_32/R_X86_64_32S`。

## 6. 第四遍：进程生命周期

### 6.1 task 0

读 `include/linux/sched.h:4-35` 和 `kernel/sched.c:19-39`：

```text
state=TASK_RUNNING
counter=15
priority=15
pid=0
father=-1
pg_dir=启动 CR3
rsp0=boot_stack_top
```

task 0 是调度器 fallback，不是原版 PID 1/init。没有普通任务可运行时，即使 task 0 state 不是
RUNNING，`schedule()` 仍会回到它。这解释了当前 task 0 阻塞 wait 为什么能够返回。

### 6.2 fork

读 `kernel/fork.c:25-93`，按资源顺序画图：

```text
find_empty_process
-> get_free_page: task_struct + kernel stack
-> copy current task fields
-> copy_pg_dir: 新 CR3 + COW
-> 在新内核栈顶复制 pt_regs
-> child_regs->rax = 0
-> 构造 switch_context 初始帧
-> task[nr] = child
-> state = TASK_RUNNING
```

父进程从 `sys_fork()` 返回 `last_pid`。子进程第一次被调度时，从
`ret_from_system_call` 开始弹出复制的现场，因此从同一用户 RIP 继续执行，但 RAX 为 0。

### 6.3 初始子内核栈

`switch_context()` 恢复顺序是：

```text
pop r15 r14 r13 r12 rbp rbx
ret
```

所以 `copy_process()` 在 `child_regs` 下方布置：

```text
r15 r14 r13 r12 rbp rbx ret_from_system_call
```

这里“下方”按地址递减理解。`p->rsp` 指向 r15。第一次切入子任务时，6 次 pop 和一次 ret 正好
把 RSP 移到 `child_regs`，随后系统调用返回汇编继续工作。

### 6.4 schedule 和 switch_to

读 `kernel/sched.c:41-85` 和完整 `kernel/switch.S`：

```text
schedule 选择 next
-> switch_to 保存 prev=current
-> current=next
-> 写 next CR3
-> 写 CPU TSS.rsp0
-> switch_context(&prev->rsp, next->rsp)
```

为什么能先切 CR3 再切栈？所有内核栈都通过每个地址空间共享的高半物理窗口访问，prev 栈在
next CR3 下仍然映射。

当前没有 timer decrement，因此 `counter` 只影响显式调度时的选择，不构成抢占式时间片。

### 6.5 exit 与 wait

读 `kernel/exit.c:16-77`：

```text
child sys_exit
-> exit_code=(status&0xff)<<8
-> state=TASK_ZOMBIE
-> schedule

parent sys_waitpid
-> 按 father 和 pid 过滤
-> running + WNOHANG: return 0
-> running + blocking: state=INTERRUPTIBLE, schedule
-> zombie: 保存 pid/code
-> 清 task slot
-> free_pg_dir
-> free task/kernel-stack page
-> 写用户 status
-> return child pid
```

阅读时必须记住当前限制：阻塞唤醒只对 task 0 fallback 成立；非 task 0 父进程没有 SIGCHLD 会
一直睡眠。详见 `../review/vm-review-2026-08-05.md` R-02。

## 7. 第五遍：和原版逐职责比较

### 7.1 推荐配对

| 当前文件/函数 | 原版参考 | 比较重点 |
| --- | --- | --- |
| `boot/uefi/main.c` | `boot/bootsect.s`, `boot/setup.s` | 固件退出和机器信息交接职责 |
| `boot/head.S` | `boot/head.s` | 自有描述符表和页表，不比较启动模式 |
| `mm/memory.c::get/free_page` | 同文件 master 版本 | mem_map 算法和引用计数 |
| `copy_pg_dir` | `copy_page_tables` | 只读共享、父 PTE 修改、失败回滚 |
| `do_wp_page` | `do_wp_page/un_wp_page` | COW 决策及原版分段前提 |
| `do_no_page` | 原版 `do_no_page/share_page` | 匿名边界、文件页、SIGSEGV/OOM |
| `kernel/switch.S` | `switch_to` 宏和 TSS | 保存职责，不比较 ljmp 指令 |
| `system_call.S` | `kernel/system_call.s` | ABI、保存现场、返回前调度/信号 |
| `fork.c` | 原版 `copy_process` | 资源顺序、父子返回、最后 RUNNING |
| `exit.c` | 原版 `do_exit/sys_waitpid` | 资源释放、SIGCHLD、孤儿和 wait 状态 |

### 7.2 只读查看原版

```bash
git show master:mm/memory.c | less
git show master:kernel/sched.c | less
git show master:kernel/system_call.s | less
git show master:kernel/fork.c | less
git show master:kernel/exit.c | less
git show master:include/linux/sched.h | less
```

不要切换分支或 checkout 单个文件。保持只读对照能避免无意改写当前工作树和学习资料。

### 7.3 每次比较时写四栏笔记

```text
原版职责
原版机制
x86-64 替换
仍未恢复的语义
```

例子：

```text
职责：切换任务地址空间和内核栈
原机制：每任务硬件 TSS + ljmp
替换：软件保存 RSP + CR3 + CPU TSS.rsp0
未恢复：FPU 状态、时钟触发、信号返回
```

## 8. 验证工具分别能证明什么

### `make -B check`

证明：

- 所有当前对象能从源码重新编译；
- 输出是 x86-64 PE32+ EFI application；
- 入口和 EFI subsystem 正确；
- 最终镜像没有未解析符号；
- 当前对象没有禁止的绝对 32 位重定位。

不能证明：页表内容、CPL3 权限、COW、调度或回收运行正确。

### `objdump -dr`

证明特定汇编布局和重定位符合设计。优先看：

```bash
objdump -dr build/x86_64/boot/head.o
objdump -dr build/x86_64/mm/page.o
objdump -dr build/x86_64/kernel/switch.o
objdump -dr build/x86_64/kernel/system_call.o
objdump -dr build/x86_64/kernel/fork.o
```

### 普通 QEMU 启动

当前能证明：

```text
OVMF 找到 EFI 镜像
ExitBootServices 成功
新栈/CR3/GDT/IDT 可用
进入内核 C
mem_init 可访问选中 RAM
空闲页统计符合交接范围
```

它不能证明进程闭环，因为正式入口在此后 `hlt`。

### 专项 CPL3 探针

适合证明 #PF、int80、fork 返回、上下文切换和回收。探针必须：

- 只验证一个明确不变量；
- 记录预期串口输出和空闲页基线；
- 失败时有唯一可识别输出；
- 验证完成后从正式代码删除；
- 删除后再跑普通启动和构建检查。

大阶段归档前还需要一个与生产镜像分离、可以重复构建的测试入口，否则历史探针不能承担回归
测试职责。

## 9. 当前最重要的不变量

修改代码前逐项检查：

### 页表

```text
PML4[0] 和 PML4[256] 在所有任务中共享
只有 PML4[1..255] 由 free_pg_dir 释放
页表地址是物理地址，解引用前必须 phys_to_virt
用户页各级表项都必须有 PAGE_USER
修改当前可见只读 PTE 后必须失效 TLB
任何 refcount++ 都必须有失败路径 refcount--
```

### 任务与栈

```text
动态子任务的 task_struct 位于任务页底部
动态子任务的 rsp0 指向同一页顶部
task 0 是例外：静态 init_task + 独立 16 KiB boot_stack
task[nr] 只在全部资源成功后发布
state 最后才切 TASK_RUNNING
切换 CR3 后 prev/next 栈仍由共享内核映射覆盖
```

### 系统调用

```text
pt_regs 必须保持 160 字节及汇编偏移一致
调用 C 前栈满足 SysV 16 字节约定
父 fork 返回 PID，子现场 RAX=0
exit status 普通退出码位于高字节
iretq 前寄存器和 CPU frame 完整
```

### 回收

```text
不能释放当前正在使用的 CR3 根
不能释放当前正在使用的内核栈页
task slot 清除、页表释放和 task 页释放顺序必须明确
重复 wait 不得再次释放资源
完整生命周期后 free pages 回到基线
```

## 10. Review 暴露的学习重点

### 10.1 “照搬分支”不等于语义忠实

原版 #PF 按 Present 位二分，是因为 LDT 已经约束用户地址。x86-64 删除 LDT 地址隔离后，如果
仍只看 Present 位，代码看起来最像原版，语义反而最不忠实。

### 10.2 task 0 是特殊调度实体

调度器无可运行普通任务时总回 task 0。因此 task 0 的 `TASK_INTERRUPTIBLE` 不是普通睡眠。
任何只用 task 0 做等待测试的结果，都不能外推到普通父进程。

### 10.3 僵尸不是“什么都不释放”

原版 zombie 保留身份、退出码和少量记账，但地址空间已经释放。当前因独立 CR3 把整个地址空间
留到 wait，是可解释的过渡实现，不是最终忠实语义。

### 10.4 编译成功不是权限证明

`make check` 能证明镜像和重定位，却看不到页表 U/S、W、PS 位组合。涉及 CPL3、CR3、TSS.rsp0
和 #PF 的结论必须来自硬件执行场景。

## 11. 建议学习练习

### 练习 1：手算地址索引

对以下地址计算 PML4/PDPT/PD/PT 索引：

```text
USER_ADDRESS_START
USER_ADDRESS_START + 0x12345000
PHYSICAL_MEMORY_WINDOW_START + 0x1780000
```

再用 `resolve_addr()` 的位移核对。

### 练习 2：画完整子进程内核栈

从 task 页顶向下画：CPU frame、15 个 `pt_regs` GPR、6 个 callee-saved 值和
`ret_from_system_call`。确认第一次 `switch_context` 后每个 pop 对应哪个位置。

### 练习 3：跟踪一个 COW 页引用计数

记录：

```text
父分配后 1
fork 后 2
子写后：父旧页 1，子新页 1
子 exit/wait 后：子新页 0
父最终释放后：父旧页 0
```

在每一步指出是哪一行代码改变 `mem_map`。

### 练习 4：解释 nested wait 死锁

用 task 0、task 1、task 2 三行表格记录 state 和 counter，模拟 task 1 阻塞等待 task 2、task 2
退出后的每次 `schedule()`。再与原版 SIGCHLD 唤醒扫描比较。

### 练习 5：证明非法 supervisor 访问不能进入 COW

这是下一提交的核心验收题。列出 #PF error code、四级表项权限、大页位和当前任务状态，说明每个
分支为何不会修改 supervisor 页表。

## 12. 一次完整阅读后的自检问题

能够不看文档回答以下问题，才算掌握当前主线：

1. UEFI memory map buffer 为什么不能在 ExitBootServices 成功前释放？
2. 为什么当前内核需要同时保留 identity map 和物理窗口？
3. `get_free_page()` 返回的数值为什么不能直接当 C 指针解引用？
4. `copy_pg_dir()` 失败后父 PTE 可能只读，为什么仍然正确？
5. 为什么 `switch_to()` 可以在换栈前先写 CR3？
6. `pt_regs` 的 RAX 为什么位于 112 字节偏移？
7. 子进程第一次运行为什么直接进入 `ret_from_system_call`？
8. task 0 的 interruptible 状态为什么仍可能继续执行？
9. 当前 waitpid 为什么不能支持普通父进程阻塞？
10. 原版 P=1 -> COW 分流为什么不能原样成为 long mode 的完整权限策略？
11. 为什么当前 zombie 保留地址空间，怎样才能更接近原版释放时机？
12. 普通 QEMU 启动通过为什么不能证明 fork/COW 没有回归？

## 13. 常用只读命令

```bash
git status --short --branch
git log --oneline --decorate -20
git show --stat <commit>
git show master:<path>
git diff master..HEAD -- <path>
rg -n '<symbol>' Makefile boot init mm kernel include
nm -u build/x86_64/BOOTX64.EFI
objdump -dr build/x86_64/kernel/system_call.o
readelf -r build/x86_64/kernel/system_call.o
```

验证命令：

```bash
make -B check
make image
timeout 10s make run
timeout 10s make run QEMU='qemu-system-x86_64 -m 64M'
```

预期的 `timeout` 退出码是 124，因为当前内核最终永久 `hlt`。判断启动成功应看串口是否到达
`free memory pages`，而不是把 timeout 当作测试失败。
