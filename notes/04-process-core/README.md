# 阶段四：task、系统调用、fork 与调度

提交区间：`5510af1..0945248`，日期：2026-08-04 至 2026-08-05。

这一阶段把阶段三的地址空间能力连接成最小进程主线。重点不是移植完整调度器，而是先让一个
fork 出来的任务拥有独立 CR3、内核栈、系统调用现场，并能实际被调度运行。

## 提交线索

| 提交 | 建立的能力 |
| --- | --- |
| `5510af1` | 定义精简的 x86-64 `task_struct` 状态和字段 |
| `a04ab87` | 建立静态 task 0 |
| `02033d8` | 初始化 `current`、task 表、CR3、`rsp0` 和系统调用门 |
| `f114775` | 保存 callee-saved 寄存器并切换 RSP、CR3、`current`、TSS.rsp0 |
| `fa06a35` | 建立 DPL3 `int 0x80` 入口、`pt_regs` 和相对 syscall table |
| `101eb3c` | 分配 task 页、复制地址空间并构造子任务首次返回栈 |
| `0945248` | 让新 fork 的任务进入可运行状态并被调度 |

## task 页为什么同时是内核栈

每个动态任务占一个物理页：低地址放 `task_struct`，页顶向下作为内核栈。`rsp0` 指向页顶，
系统调用或异常从 CPL3 进入时，CPU 使用 TSS.rsp0 切到这张内核栈。

fork 并不是复制当前内核栈的所有临时内容，而是在子页顶构造两层状态：

```text
pt_regs
callee-saved register frame
ret_from_system_call return address
```

子任务第一次被 `switch_context` 选中后，像从一个从未真正调用过的函数中返回，最终由
`iretq` 回到用户 RIP，且 `RAX=0`。父任务从 `sys_fork` 得到子 PID。

## 系统调用 ABI

当前保留 Linux 0.11 风格的最小寄存器约定：

```text
RAX = syscall number
RBX = argument 1
RCX = argument 2
RDX = argument 3
int 0x80
```

汇编必须保存与 `struct pt_regs` 完全一致的顺序。系统调用表使用相对偏移，避免 PE32+ 镜像因
绝对函数地址引入不允许的重定位。

## 调度范围

调度器仍然是单 CPU、无时钟抢占的最小实现。它按 `counter` 选择 `TASK_RUNNING` 任务，必要时
重算 counter。当前任务只有主动调用 `schedule()` 或沿 exit/wait 路径才会切换。

## 这一阶段的证据和限制

历史一次性探针证明父子 fork 返回值、独立 CR3、COW 写隔离和实际上下文切换成立。普通启动
入口本身不执行这些路径，因此 `make run` 只能作为启动回归，不能替代进程探针。

阶段结束时还没有 zombie、wait、普通父进程唤醒或用户错误隔离。这些由阶段五逐项补齐。
