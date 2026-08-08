# Linux 0.11 x86-64 学习笔记

本目录保存学习笔记。当前内容临时纳入一次性迁移快照；迁移后恢复本地维护的步骤见根目录
[`MIGRATION.md`](../MIGRATION.md)。当前代码能力基线是：

```text
771ca3a kernel: reparent orphaned tasks
```

当前进程退出路径会在任务仍持有自身 CR3 时释放用户映射；空 CR3 根页和 task 页仍由
`waitpid()` 回收。该能力已经通过构建、退出前页计数、重复 zombie、SIGSEGV、嵌套 wait、
fork 深层失败回滚和普通启动验证。

`9c6e5cb` 增加了与正式镜像隔离的进程生命周期测试 EFI，覆盖匿名缺页、COW、非法
supervisor 访问、用户 OOM、fork 深层失败、嵌套 wait 和重复 zombie 生命周期。

`9c93071` 建立了 x86-64 只读 Minix v1 格式层，能够从同步块回调挂载文件系统、读取 32 字节
inode、遍历 14 字节目录项并解析直接/单双间接数据块。ramdisk 块来源仍是下一提交，不能把
格式解析器描述为完整文件系统移植。

`c5069fd` 增加了 x86-64 只读 ramdisk 后端和独立 Minix 根镜像。QEMU 文件系统测试已经从
独立 `root.img` 挂载并读取 `/bin/init`；测试使用 loader 放置镜像，正式启动尚无生产根盘
装载器。

`db599d3` 增加了 x86-64 ATA PIO 读取、按 Minix 超级块装载 ramdisk，以及正式启动挂载根
文件系统并查找 `/bin/init`。测试已不再使用 QEMU loader 固定物理地址。

`bcd694a` 增加了受限静态 ELF64 exec：syscall 11 在候选 CR3 中建立程序段、BSS 和最小参数栈，
成功后一次替换用户 RIP/RSP 和地址空间，失败时保留旧映像。独立 exec EFI 已运行覆盖成功替换、
坏映像、非法段、缺失文件和 OOM 回滚。

`7859c26` 从正式根盘 ELF 创建真实 PID 1。init 在 CPL3 校验初始映像后 fork 并 waitpid 回收一个
子进程，生产变体保持运行；独立退出变体验证 PID 1 正常退出会进入明确的内核 panic 策略。

`771ca3a` 在父任务退出时把其子任务改归 PID 1，并让生产 init 持续使用 `waitpid(-1)` 回收。
进程回归已运行验证活孤儿重父、SIGCHLD、退出状态和页面回收。

发展规划仍放在仓库根目录的 [`NEXT_STEP.md`](../NEXT_STEP.md)。这里记录已经发生的提交、
代码语义、验证证据和阶段限制，不把计划中的能力写成已经完成。

每个开发 session 必须遵循根目录 `AGENTS.md` 和
[session-checklist.md](session-checklist.md)。源码能力、验证结果或 commit 基线发生变化时，
更新本目录是完成开发工作的必要步骤，不是可选的事后整理。

## 按阶段阅读

| 阶段 | 提交区间 | 建立的能力 | 阶段笔记 |
| --- | --- | --- | --- |
| 1. 启动与交接 | `057ec45..3690495` | UEFI、Boot Services 退出、自有栈/GDT/IDT/CR3 | [01-boot-and-handoff](01-boot-and-handoff/README.md) |
| 2. 物理内存基础 | `81d6844..9db1e84` | 简单内存区间、`mem_map`、页分配与释放 | [02-memory-foundation](02-memory-foundation/README.md) |
| 3. 虚拟内存与 COW | `55e5a39..52e6b50` | 四级页表、用户映射、页异常、COW | [03-virtual-memory](03-virtual-memory/README.md) |
| 4. 最小进程主线 | `5510af1..0945248` | task 0、切换、`int 0x80`、fork、调度 | [04-process-core](04-process-core/README.md) |
| 5. 退出与异常边界 | `93defc6..9c6e5cb` | exit/wait、用户错误隔离、退出映射回收、独立回归镜像 | [05-process-boundaries](05-process-boundaries/README.md) |
| 6. 文件系统与 exec | `9c93071..bcd694a` | 只读 Minix v1 格式、ramdisk、用户映像替换 | [06-filesystem-and-exec](06-filesystem-and-exec/README.md) |
| 7. init 生命周期 | `7859c26..771ca3a` | 真实 PID 1、init 回收与孤儿重父 | [07-init-lifecycle](07-init-lifecycle/README.md) |

阶段笔记以提交为骨架。遇到不熟悉的机制时，再进入原有专题资料：

- [DAY 1 启动记录](01-boot-and-handoff/day-01.md)
- [UEFI 内存图与 ExitBootServices](01-boot-and-handoff/uefi-memory-map.md)
- [内核自有四级页表](01-boot-and-handoff/kernel-page-tables.md)
- [DAY 2 物理内存记录](02-memory-foundation/day-02.md)
- [当前代码阅读指南](reference/code-reading-guide.md)
- [完整移植历史学习指南](reference/porting-study-guide.md)
- [与原版 Linux 0.11 的职责对照](reference/linux-0.11-comparison.md)

审查和阶段性路线保留为历史快照：

- [2026-08-05 虚拟内存综合 Review](review/vm-review-2026-08-05.md)
- [2026-08-05 虚拟内存大阶段路线](review/vm-roadmap-2026-08-05.md)

## 阅读规则

每个阶段都区分三种状态：

```text
implemented 代码已经进入提交
verified    有静态、反汇编或 QEMU 证据
planned     只存在于 NEXT_STEP.md 或阶段末尾限制中
```

编译成功只能证明 C/汇编接口和链接关系成立。涉及 CPL3、CR3、页异常、COW、调度和回收时，
必须找到对应的运行探针证据，不能从普通启动输出反推这些路径已经工作。

## 后续续写方式

后续每完成一个原子提交，在对应阶段末尾追加四项：

1. 提交号与标题；
2. 新建立的行为能力；
3. 静态和运行验证分别证明了什么；
4. 仍然存在的限制。

当一个提交开始新的系统责任，例如 exec、文件页或 PID 1 生命周期，应建立新阶段文件，
不要继续把所有内容堆进同一篇笔记。

详细的 session 开始、开发中、提交后和结束检查见
[开发 Session 笔记检查清单](session-checklist.md)。
