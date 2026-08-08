# Linux 0.11 x86-64 虚拟内存大阶段计划

更新日期：2026-08-05

历史基线：`5d0f848 kernel: wait for child tasks`

本文档原为本地资料，当前仅通过一次性迁移快照纳入 Git。

> 状态补记（2026-08-06）：当前基线已推进到
> `462db18 kernel: terminate faulting user tasks`。A1、A2 的任务级失败边界和 A3 已完成；
> A4 的退出映射及时释放是下一项。详细提交和验证见
> [`阶段五`](../05-process-boundaries/README.md)，当前发展规划仍以 `../../NEXT_STEP.md` 为准。

## 1. 大阶段定义

当前大阶段不是“让镜像能够启动”，也不是“把某几个原版函数编译成 64 位”。它的归档目标是
恢复一条可运行、可失败、可回收的虚拟内存生命周期：

```text
独立地址空间
-> CPL3 可恢复页异常
-> 真实 fork 写时复制
-> exit/wait 生命周期
-> exec 原子替换
-> 文件页按需装入和共享
-> 权限、失败回滚和重复压力验证
```

完成时必须能够证明：

- 不同任务可以在同一用户虚拟地址保存不同内容；
- 合法匿名访问可以缺页分配并返回原指令；
- fork 后父子先共享，任一方写入后内容隔离；
- 非法用户访问只终止当前任务，不损坏页表或停止内核；
- exec 成功时原子替换地址空间，失败时保留可用旧状态；
- 文件末页清零，未修改程序页可以共享；
- exit、wait 和重复生命周期不会持续减少空闲页；
- OOM 和部分分配失败不会留下引用计数或页表泄漏。

PIT/PIC、完整 TTY、完整文件系统生态和全部系统调用不是本大阶段的完成条件。只移植 VM 闭环
实际依赖的最小跨子系统路径。

## 2. 2026-08-05 快照状态

### 已完成并进入正式代码

- UEFI PE32+ 启动、内存图和 ExitBootServices；
- 自有内核栈、GDT、IDT、64 位 TSS.rsp0；
- 低 4 GiB identity map 和共享高半物理窗口；
- 最多 15 MiB 的 `mem_map[3840]` 物理页分配和引用计数；
- 四级页表创建、用户映射、复制、切换、销毁和分配失败回滚；
- CPL3 匿名不存在页异常和 `iretq` 返回；
- 父子用户页只读共享以及写时复制；
- `task[64]`、task 0、动态子任务的一页内核栈和独立 CR3；
- 软件上下文切换，更新 RSP、CR3、`current` 和 TSS.rsp0；
- DPL3 `int 0x80` 保存/恢复现场；
- syscall 1/2/7/20：exit、fork、waitpid、getpid；
- 受控的 task 0 父子 `fork -> COW -> exit -> waitpid -> 回收` 闭环。

### 已运行验证但不在正式启动路径中

历史一次性 CPL3 探针证明：

```text
父 fork 返回 PID 1
子 fork 返回 0
WNOHANG 在子退出前返回 0
子写共享用户栈触发 COW
父栈原值保持不变
子 _exit(37) 形成 wait status 0x2500
父 waitpid 返回 PID 1
再次等待返回 -ECHILD
task[1] 清空
空闲页恢复到 fork 前基线
```

探针已经删除，没有进入提交。当前正式 `init/main.c` 只初始化内存并 `hlt`，所以普通启动不能
重新证明上述进程闭环。

### Review 后确认的阻断问题

详见 `vm-review-2026-08-05.md`：

1. P=1 的页异常被无条件当作 COW，访问 supervisor 大页可能错误解释页表；
2. 阻塞 wait 只依赖 task 0 特例，普通父进程没有 SIGCHLD 唤醒；
3. 非法用户缺页和 OOM 会 panic 整个内核；
4. 僵尸保留完整地址空间，孤儿进程也不会重新归属；
5. 进程闭环没有可重复的仓库内 QEMU 回归测试。

因此，最小单父单子里程碑已经完成，但“大阶段方向三”应标记为“主路径完成，边界收尾中”，
不能直接跳过这些问题去声明完整进程内存生命周期完成。

## 3. 总体完成度

| 方向 | 状态 | 估计完成度 | 说明 |
| --- | --- | ---: | --- |
| 1. 正式地址空间 | 完成 | 100% | 创建、映射、隔离、切换、销毁已验证 |
| 2. 异常驱动匿名分页 | 主路径完成 | 80% | 合法匿名页和 COW 已通过；非法/权限/OOM 待补 |
| 3. 进程内存生命周期 | 主路径完成，边界收尾 | 75% | task 0 单层父子闭环通过；普通等待、孤儿、退出释放待补 |
| 4. exec 与文件支持页面 | 未开始 | 0% | 原版 fs/exec 尚未进入 x86-64 构建 |
| 5. 权限、失败和归档验证 | 未开始 | 10% | 已有构建检查和零散探针，尚无系统化回归 |

按工作量而不是简单平均，目前大阶段约完成 55% 到 65%。文件页和 exec 会引入 inode、块读取、
a.out 布局和原子失败回滚，是剩余工作中最大的不确定项。

## 4. 已完成提交链

### 启动与内存基础

```text
057ec45 boot: add minimal x86-64 UEFI application
5d335df boot: make x86-64 UEFI image position independent
5426611 boot: exit UEFI boot services
3cbfc26 x86_64: enter kernel on an owned stack
6f1c797 x86_64: install early descriptor tables
2838ea5 x86_64: install early page tables
81d6844 boot: pass a simple memory range
bba83d4 mm: add early memory map
360d98a mm: initialize physical pages
f58e624 mm: allocate physical pages
3c3f811 mm: release physical pages
```

### 地址空间、异常和 COW

```text
55e5a39 mm: resolve virtual addresses
cf20a6f mm: split early large pages
4d806c7 mm: map physical pages
4c3b138 mm: allocate mapped pages
e3a83ca mm: define x86-64 virtual layout
f36acb0 mm: map physical memory window
cf5798a mm: access physical memory through kernel mapping
9b5021c mm: create empty page tables
27af9e7 mm: map user pages
3d93ac0 mm: free user page tables
2cd4f65 mm: switch page tables
0422fcd mm: handle anonymous page faults
1b0a07a x86_64: install a task state segment
baadfc4 x86_64: enter user mode
ec78684 mm: handle user page faults
8cadab4 mm: share user pages
52e6b50 mm: copy shared pages on write
```

### 任务和最小进程生命周期

```text
5510af1 sched: define task state
a04ab87 sched: add task zero
02033d8 sched: initialize task zero
f114775 x86_64: switch tasks
fa06a35 x86_64: enter system calls
101eb3c x86_64: fork a task
0945248 sched: run forked tasks
93defc6 kernel: exit tasks
5d0f848 kernel: wait for child tasks
```

### 后续边界修复

```text
4ff2d83 mm: reject protection faults outside user memory
1844cf6 kernel: wake waiting parents
462db18 kernel: terminate faulting user tasks
```

审查时的最新 6 个提交已经分别在独立快照中通过 `make check`。多文件修改围绕单一行为能力展开，符合
最小原子提交原则。

## 5. 下一阶段 A：关闭 review 阻断问题

### A1. 正确分类页异常

目标：只有来自用户区、确实由写只读 4 KiB COW 页引起的保护异常才能进入 `do_wp_page()`。

退出标准：

```text
合法 COW 写仍成功
用户读写 supervisor identity/direct-map 不修改任何页表
非法用户异常形成 SIGSEGV wait status
内核异常进入明确 panic
空闲页和引用计数不变
```

建议提交：`mm: reject invalid user page faults`

### A2. 用户错误和 OOM 的进程级结果

目标：恢复原版 `oom() -> do_exit(SIGSEGV)` 的责任边界，并为当前 task 增加足够的匿名内存边界，
不再把整个 PML4 用户半区视为无限合法堆栈。

建议根据实际接口拆成不超过两个提交：

```text
kernel: terminate faulting user tasks
mm: bound anonymous task memory
```

### A3. 恢复普通父进程等待唤醒

目标：加入最小 `signal`/`blocked` 字段、`SIGCHLD` 通知和 scheduler 唤醒扫描，使任意非 task 0
父进程都能阻塞等待自己的子进程。

退出标准：

```text
task 1 fork task 2
task 1 blocking waitpid
task 2 exit
task 1 被 SIGCHLD 唤醒并回收 task 2
task 0 最终回收 task 1
```

建议提交：`kernel: wake waiting parents`

### A4. 收紧退出资源语义

目标：

- 退出任务先释放用户映射子树，只让空 CR3 根和 task 页留给 wait；
- 明确 PID 1/init 可用后再恢复孤儿转交；
- 先验证状态地址可写，再做不可逆回收；
- 保持 wait status 和 `-ECHILD` 语义。

建议提交边界：

```text
mm: release exiting user mappings
kernel: reparent orphaned tasks
```

### A5. 建立可重复测试入口

目标：测试代码与正式启动入口分离，通过显式目标构建测试镜像，串口输出可由脚本判断；正式
`make image` 不包含探针。

最少场景：

```text
valid-cow
invalid-supervisor-access
nested-wait
fork-failure-rollback
repeated-lifecycle
normal-boot
```

## 6. 下一阶段 B：exec 与文件支持页面

先读原版 `fs/exec.c`、`mm/memory.c::do_no_page()`、inode/buffer/block read 路径，再决定最小接入面。
不允许用永久内嵌机器码冒充文件页加载。

建议能力顺序：

1. 为 task 增加 `start_code/end_code/end_data/brk/start_stack` 和 executable 归属；
2. 定义 x86-64 用户代码、数据、堆、栈布局；
3. 建立只读的最小块读取来源或测试镜像接口；
4. 读取并验证 Linux 0.11 a.out 头；
5. 在新地址空间中构造 argv/envp 和初始用户栈；
6. exec 成功时原子交换 CR3，失败时销毁候选地址空间；
7. `do_no_page()` 从 executable 按需读取代码/数据页；
8. 清零文件末页未覆盖部分；
9. 通过 executable identity 共享未修改程序页；
10. exec/exit 后引用计数和页数恢复。

这一方向预计需要 5 到 8 个原子提交，实际数量取决于最小块读取路径能否与原版 inode/buffer
接口自然衔接。

## 7. 下一阶段 C：权限、失败和归档验证

功能停止扩张后集中验证：

- 用户不能读写 identity map、物理窗口、内核代码、内核栈和页表；
- 代码页、只读数据、可写数据和栈权限符合布局；
- 空指针、非 canonical 地址、越界、保留位异常有确定结果；
- 页数据分配失败和每一级页表分配失败都能回滚；
- fork、COW、exec、exit、wait 重复多轮后页数回到基线；
- 父子多层退出顺序不会遗留 task 槽、僵尸或孤儿；
- CR3 切换和 TLB 失效在所有路径上成立；
- 默认内存和较小 QEMU 内存配置均可重复执行。

归档输出：最终设计说明、原版差异、已知限制、测试矩阵和串口证据。

## 8. 时间与提交估计

Review 增加了必须在 exec 前完成的安全和生命周期收尾。以当前“小提交 + 每提交运行验证”的节奏：

```text
阶段 A：4 到 6 个提交
阶段 B：5 到 8 个提交
阶段 C：2 到 4 个提交
总计：约 11 到 18 个提交
```

若每轮稳定完成并验证 1 到 2 个能力，大约还需要 6 到 10 轮连续开发。这里的“轮”是完成代码、
反汇编、QEMU 场景、移除正式路径探针、检查暂存区并提交的完整循环，不是单纯编码时间。

最大风险不是代码行数，而是最小文件读取路径、exec 失败原子性和可重复 OOM 注入。遇到这些边界
时不应为了压缩提交数牺牲 Linux 0.11 语义。

## 9. 每提交纪律

每个提交必须满足：

```text
一个独立行为能力
-> git diff --check
-> 强制重新编译相关对象
-> make check
-> 必要的 objdump/nm/readelf
-> 对 CPL3/CR3/#PF/回收执行专项 QEMU 场景
-> 删除正式启动路径中的临时探针
-> 只暂存预定路径
-> git diff --cached --check
-> 人工审查完整 staged diff
-> commit
```

不要按文件数量拆提交。一个 syscall 能力通常需要 C 实现、汇编表项和 Makefile 依赖同时改变；
只要它们共同构成一个可验证行为，就是最小原子提交。

## 10. 当前归档检查表

```text
[x] UEFI 交接与自有 x86-64 执行环境
[x] 正式四级地址空间和共享内核映射
[x] CR3 软件切换和 TSS.rsp0 更新
[x] CPL3 匿名不存在页恢复
[x] 页表级父子只读共享和 COW
[x] int 0x80、fork、schedule、exit、wait 的受控单层闭环
[x] 最近 6 个能力提交独立构建
[x] 保护异常只能进入合法 COW
[x] 非法用户缺页不会停止或损坏内核
[x] 普通父进程可由 SIGCHLD 唤醒
[ ] 退出时及时释放用户映射
[ ] 孤儿进程有确定归属和回收者
[ ] 进程生命周期测试可重复运行
[ ] exec 原子替换地址空间
[ ] 文件页按需装入、末页清零和共享
[ ] 用户不能访问任何 supervisor 映射
[ ] OOM 和部分失败无泄漏
[ ] 重复生命周期后页数恢复
[ ] 最终证据和限制归档
```
