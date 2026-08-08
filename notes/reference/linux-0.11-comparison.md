# 当前 x86-64 移植与原版 Linux 0.11 对照

历史对照基线：`x86-64` 分支 `5d0f848` 与 `master` 中的原版 Linux 0.11。

本文档原为本地资料，当前仅通过一次性迁移快照纳入 Git。

> 状态补记（2026-08-06）：`4ff2d83`、`1844cf6`、`462db18` 已分别修复保护异常边界、
> 普通父进程唤醒和用户缺页失败结果。续写见
> [`阶段五`](../05-process-boundaries/README.md)。

## 1. 怎样判断“忠于原版”

忠实移植不等于逐条翻译 i386 汇编，也不等于保留已经被 long mode 删除的机制。这里使用四种
状态描述每项工作：

| 状态 | 含义 |
| --- | --- |
| 语义已恢复 | 调用者能观察到的 Linux 0.11 行为已经形成，并有运行证据 |
| 架构替换 | 原职责保留，但用 x86-64 必需的机制实现 |
| 阶段性脚手架 | 只服务于受控里程碑，不能当作完整子系统 |
| 尚未移植 | 原版代码仍在仓库，但没有进入当前镜像 |

判断顺序应当是：

```text
先问原版维护了什么不变量
-> 再问 long mode 是否仍提供原机制
-> 若机制失效，用目标架构恢复同一可观察语义
-> 最后用运行场景证明，不用函数名或编译成功代替
```

## 2. 总体对照

| 主题 | 原版 Linux 0.11 | 当前 x86-64 | 判断 |
| --- | --- | --- | --- |
| 固件入口 | BIOS、实模式、bootsect/setup | OVMF、UEFI、PE32+ | 架构替换 |
| 内存发现 | BIOS 参数区 | UEFI GetMemoryMap | 架构替换 |
| 物理内存上限 | 16 MiB，总分页区最多 15 MiB | 从低于 4 GiB 的 conventional 区选最多 15 MiB | 原版规模语义保留 |
| 页表 | i386 两级页表 | x86-64 四级页表 | 架构替换 |
| 内核映射 | 低端线性地址恒等映射 | 低 4 GiB identity 加高半物理窗口 | 架构替换 |
| 用户隔离 | 每任务 LDT 基址和 64 MiB 槽 | 每任务独立 CR3，用户 PML4 槽 1..255 | 架构替换 |
| 物理页记账 | `mem_map[3840]` 字节引用计数 | 动态 low_mem 上的 `mem_map[3840]` | 语义已恢复 |
| 缺页 | 匿名页、文件页、共享、COW | 匿名页和 COW | 部分恢复 |
| 任务表 | `task[64]`、`current`、task 0 | 同名模型和相同 task 0 初值 | 语义已恢复 |
| 内核栈 | task_struct 与内核栈共用一页 | 动态子任务共一页；task 0 使用独立 16 KiB boot stack | 部分恢复 |
| 任务切换 | 每任务 TSS/LDT，`ljmp` 硬切换 | 保存 callee-saved RSP，软件切 CR3/current/rsp0 | 架构替换 |
| 系统调用 | DPL3 `int 0x80`，EAX 号，EBX/ECX/EDX 参数 | 保留相同入口和寄存器约定 | 最小语义已恢复 |
| fork | 复制 task/TSS/LDT，复制页表为只读共享 | 复制 task/内核栈现场，复制四级页表为只读共享 | 最小语义已恢复 |
| 调度 | `counter` 最大者；全零时衰减加 priority | 保留公式和 task 0 fallback | 选择语义已恢复，无时钟 |
| exit | 先释放地址空间和引用，再变 zombie，通知父进程 | 直接变 zombie，地址空间等到 wait 再释放 | 阶段性脚手架 |
| waitpid | SIGCHLD 唤醒、PID/进程组、停止状态、回收 | 正 PID/-1、WNOHANG、僵尸回收；仅 task 0 可可靠阻塞 | 部分恢复 |
| exec | a.out、参数环境、替换 LDT/页表、按需文件页 | 未进入构建 | 尚未移植 |
| 文件系统 | buffer/inode/namei/block device/root fs | 原源码存在但未链接 | 尚未移植 |
| 中断与时钟 | 8259A、8253、100 Hz、CPL3 时间片抢占 | 中断关闭，除 #PF/int80 外统一停机 | 尚未移植 |
| 信号 | 32 位图、blocked、返回前投递 | 未移植 | 尚未移植 |

## 3. 启动路径

### 原版

```text
BIOS
-> boot/bootsect.s 读取 setup 和 system
-> boot/setup.s 读取参数、开 A20、进保护模式
-> boot/head.s 建 GDT/IDT/两级页表
-> init/main.c 初始化内存、陷阱、设备、调度、文件系统
-> sti
-> move_to_user_mode
-> task 0 fork task 1
```

### 当前

```text
OVMF
-> boot/uefi/main.c 获取内存图并 ExitBootServices
-> boot/head.S 切自有栈
-> 建 identity map 与物理窗口
-> 加载 GDT/IDT/TSS
-> sched_init 建 task 0 和 int 0x80 门
-> init/main.c 初始化 mem_map 并打印页数
-> cli 状态下永久 hlt
```

保留的职责是“脱离固件、建立内核自有执行环境并交接内存信息”。BIOS 中断、实模式和 A20
在 UEFI long mode 下没有继续保留的意义。

当前正式路径与原版最大的功能差异是：原版最终开启中断并创建 init；当前正式入口不进入
CPL3。进程能力只在一次性里程碑探针中组合验证过。

## 4. 地址空间与隔离

### 原版不变量

原版所有任务共享页目录，但通过 LDT 给每个任务安排不同线性基址。用户逻辑地址不能越过任务
段限长，因而不能直接构造内核线性地址。fork 把父任务线性区的页表复制到子任务的另一个 64 MiB
槽位。

### 当前替换

long mode 的 64 位代码不再使用 LDT 基址做普通地址转换。当前为每个任务保存独立 `pg_dir`：

```text
PML4[0]       低 4 GiB supervisor identity map，共享
PML4[1..255]  每进程用户映射，复制或释放
PML4[256]     高半物理窗口，共享
```

这比尝试模拟分段更符合 x86-64，同时保留“内核共享、用户隔离”的语义。

### 尚未补回的不变量

原版 LDT 不只是重定位，还阻止用户访问任务槽外的线性地址。当前 GDT 用户段是平坦的，用户可以
构造任意 canonical address，只能依靠页表 U/S 位和 #PF 策略隔离。当前 #PF 保护异常仍直接进入
COW，这是 `../review/vm-review-2026-08-05.md` 的 R-01，也是一个典型例子：原版表面上的分流代码可以保留，但原先由
分段提供的前置不变量必须在 long mode 中显式重建。

## 5. 物理页与四级页表

### 保留的 Linux 0.11 思想

- 最多管理 3840 个分页内存页；
- `mem_map[] == 0` 表示空闲，正数表示引用次数；
- 从高索引向低索引寻找空闲页；
- 分配后清零；
- 低于管理区的内核页不参与普通释放；
- fork 只复制页表结构，数据页增加引用计数。

### x86-64 必需变化

- `low_mem` 不再固定为 1 MiB，而来自 UEFI 选择的连续区；
- 用共享的高半物理窗口访问任意受管物理页；
- 两级 PDE/PTE 变成 PML4/PDPT/PD/PT；
- CR3 不再固定为 0；
- CR0.WP 被显式打开，让内核写用户只读页时也产生 COW 异常；
- CR4.PGE 被关闭，确保当前共享映射和 CR3 切换的 TLB 语义简单可控。

当前创建、映射、复制、切换、回滚和销毁的主算法是自洽的。它依赖一个明确限制：用户映射只由
当前 API 建立为 4 KiB 管理页，用户区内没有 1 GiB/2 MiB 大页或非受管物理页。

## 6. 页异常

### 原版路径

```text
#PF
-> page.s 保存寄存器、读取 CR2
-> P=0: do_no_page
-> P=1: do_wp_page
-> iret
```

`do_no_page()` 根据 `end_data`、栈位置和 `executable` 决定匿名页、文件页、共享或 SIGSEGV。
`do_wp_page()` 对 fork 后的只读共享页恢复写权限或复制。

### 当前路径

异常栈帧、CR2、错误码、寄存器保存和 `iretq` 都已正确改为 64 位。匿名缺页和理想 COW 写已经
由 CPL3 探针运行通过。

但当前只使用 P 位分类，且整个用户范围都视为合法匿名地址。原版依靠 LDT 和 task 内存边界
提供的保护尚未迁移。因此页异常模块应描述为“匿名页和受控 COW 路径已恢复”，不能描述为
“完整用户缺页语义已恢复”。

## 7. task_struct 与任务切换

### 原版

原版 `task_struct` 包含调度、信号、身份、内存边界、文件、inode、LDT 和完整硬件 TSS。
每个任务结构和内核栈共用一页。`switch_to(n)` 通过 TSS 描述符和 `ljmp` 让 CPU 保存、恢复
寄存器并切 CR3。

### 当前

当前只保留已经进入运行路径的字段：

```text
state counter priority
pid father exit_code
pg_dir rsp0 rsp
```

动态子任务仍把 task_struct 放在物理页底部，并把同一页顶部作为内核栈。task 0 是启动期例外：
`init_task` 是静态对象，`rsp0` 指向 `boot/head.S` 中独立的 16 KiB `boot_stack`。这比原版 task 0
的一页 union 更宽松，应视为尚未收紧的启动脚手架。

`switch_context()` 保存 SysV ABI 的 callee-saved 寄存器和 RSP；C 层同步更新 `current`、CR3 和
CPU TSS.rsp0。每任务完整 TSS 和 LDT 不再存在。

这是合理的架构替换。只保存 callee-saved 寄存器也不是遗漏：上下文切换发生在普通 C 调用边界，
caller-saved 寄存器本来就不要求跨调用保持；从系统调用新建的子进程则由 `pt_regs` 保存完整用户
现场。

尚未覆盖 FPU/SIMD 状态。当前编译器使用 `-mgeneral-regs-only`，所以在现阶段构造上不会由内核 C
代码产生这类状态；正式用户程序使用 FPU 前仍需恢复原版 lazy math 所承担的职责。

## 8. int 0x80

### 保留的 ABI

```text
RAX: 原版 EAX 系统调用号
RBX: 原版 EBX 参数 1
RCX: 原版 ECX 参数 2
RDX: 原版 EDX 参数 3
RAX: 返回值
```

目前注册：

```text
1  exit
2  fork
7  waitpid
20 getpid
```

入口是 DPL3 trap gate，保存 15 个通用寄存器，加上 CPU 的 RIP/CS/RFLAGS/RSP/SS，构成 160 字节
`pt_regs`。相对 32 位 syscall table 避免了位置无关 PE 镜像中的绝对 32 位地址重定位。

### 与原版的差异

原版返回路径还会：

- 当前任务非 RUNNING 或 counter 为 0 时重调度；
- 只在返回用户态时检查并投递信号；
- 恢复原版段寄存器约定。

当前这些职责尚未移植。long mode 中 DS/ES 基址被忽略，所以不需要照抄段切换，但未来的 FS/GS
用户基址、信号帧和中断打开后仍需单独设计。

## 9. fork

当前 `copy_process()` 与原版顺序基本一致：

```text
找到 PID 和任务槽
-> 分配 task/内核栈页
-> 复制父 task 的已支持字段
-> 暂设 TASK_UNINTERRUPTIBLE
-> 复制页表结构并共享用户页
-> 构造子系统调用返回现场，RAX=0
-> 安装 task[nr]
-> 最后设 TASK_RUNNING
-> 父返回 child PID
```

分配失败时释放 task 页；`copy_pg_dir()` 内部会释放已经建立的子页表并撤销相应引用计数。父页
可能暂时保持只读，但下一次写异常会在引用为 1 时恢复可写，这与原版失败后的可接受状态一致。

未恢复的字段包括 signal/sigaction、身份、运行时间、文件描述符、pwd/root/executable 和 FPU。
这些字段不存在于当前 x86-64 task_struct，因此不能把现在的 fork 称为完整 POSIX 进程复制。

## 10. schedule

当前保留原版核心算法：

```text
从 task[63] 向 task[1] 扫描
-> 选 TASK_RUNNING 且 counter 最大者
-> 所有 counter 为 0 时执行 counter=(counter>>1)+priority
-> 没有可运行普通任务时回到 task 0
```

没有 PIT 时不会周期性递减 counter，因此当前调度只由 exit/wait 等显式路径触发。算法已经恢复，
“时间片调度系统”尚未恢复。这两个表述不能混用。

## 11. exit 与 waitpid

### 已恢复

- `_exit(status)` 只取低 8 位并左移 8 位；
- 退出任务进入 `TASK_ZOMBIE`；
- 父进程可以等待指定 PID 或 `-1`；
- `WNOHANG` 在匹配子任务仍运行时返回 0；
- 僵尸状态和任务页由父进程回收；
- 重复等待返回 `-ECHILD`。

### 尚未恢复

- `SIGCHLD` 和任意父进程的阻塞唤醒；
- PID 0 和 PID < -1 的进程组选择；
- `WUNTRACED` 与停止状态；
- 子进程 CPU 时间累计；
- 文件、inode、会话、TTY、FPU 引用释放；
- 孤儿进程转交 PID 1；
- 退出阶段立即释放用户映射。

因此正确表述是：“最小 task 0 父子生命周期已运行验证”，不是“原版 exit/wait 子系统已完成”。

## 12. 提交边界与原版忠实度

多文件提交并不违背最小提交原则。这里的最小单位是一个可构建、可解释、可验证的行为变化：

```text
switch tasks
enter system calls
fork a task
run forked tasks
exit tasks
wait for child tasks
```

每个中间提交都独立通过当前构建检查。按文件进一步拆分会产生“声明存在但入口未注册”或“对象
进入构建但调用方不存在”的非能力提交，反而降低历史可读性。

忠实度的下一步不是扩大 syscall 数量，而是补回已经被 long mode 替代机制原先维护的不变量：

```text
页表权限承担原版 LDT 的隔离职责
SIGCHLD 承担原版父进程唤醒职责
退出阶段承担原版早期资源释放职责
task 内存边界承担原版 do_no_page 的合法性判断
```

## 13. 原版对照阅读命令

不要切换当前工作分支，直接只读查看 `master`：

```bash
git show master:boot/head.s
git show master:init/main.c
git show master:mm/page.s
git show master:mm/memory.c
git show master:kernel/sched.c
git show master:kernel/system_call.s
git show master:kernel/fork.c
git show master:kernel/exit.c
git show master:include/linux/sched.h
```

建议每次只比较一个职责。例如读 COW 时只沿下面路径：

```text
master:mm/page.s
-> master:mm/memory.c::do_wp_page/un_wp_page
-> current:mm/page.S
-> current:mm/memory.c::do_wp_page
```

不要用全仓库 diff 直接判断忠实度。启动协议、架构目录和注释规模的差异会淹没真正需要比较的
不变量。
