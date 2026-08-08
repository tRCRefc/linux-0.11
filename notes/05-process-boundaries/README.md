# 阶段五：exit、wait 与页异常失败边界

提交区间：`93defc6..9c6e5cb`，日期：2026-08-05 至 2026-08-06。

这一阶段把“任务可以运行”推进到“任务可以退出、被等待、在用户错误时只结束自己”。它仍不是
完整 Linux 进程模型，但已经形成最小可失败、可通知、可回收的主线。

## 提交线索

| 提交 | 建立或修复的能力 |
| --- | --- |
| `93defc6 kernel: exit tasks` | `sys_exit` 保存状态，任务进入 zombie 并让出 CPU |
| `5d0f848 kernel: wait for child tasks` | `waitpid` 查找子任务、读取状态、释放 CR3 根和 task 页 |
| `4ff2d83 mm: reject protection faults outside user memory` | COW walker 解引用页表前先拒绝用户区外地址 |
| `1844cf6 kernel: wake waiting parents` | 子退出设置 SIGCHLD，调度器唤醒普通 `TASK_INTERRUPTIBLE` 父进程 |
| `462db18 kernel: terminate faulting user tasks` | 用户非法缺页和用户路径 OOM 形成 SIGSEGV，内核来源仍 panic |
| `b438110 mm: release exiting user mappings` | 任务成为 zombie 前释放用户映射，wait 时再释放空 CR3 根和 task 页 |
| `9c6e5cb test: add process lifecycle image` | 独立 EFI 固化 CPL3、COW、失败回滚和 zombie 生命周期回归 |

## 为什么需要连续三个边界修复

原版 i386 依赖 LDT 基址和限长约束每任务线性地址。long mode 用户段是平坦的，用户可以直接
构造 identity map 或物理窗口附近的地址。因此原版“P=1 就走写保护/COW”的表面分流不能独立
保证安全，必须显式检查页错误码和用户地址范围。

当前保护异常路径是：

```text
#PF P=1
-> 必须是写访问
-> 地址必须在用户范围
-> 才进入普通 4 KiB COW walker
```

不存在页和分配失败则按页错误码 U/S 位决定结果：

```text
用户来源 -> do_exit(SIGSEGV) -> zombie -> SIGCHLD -> parent waitpid
内核来源 -> panic
```

## `462db18` 的运行证据

2026-08-06 使用一次性 CPL3 探针验证了以下场景，探针随后从正式入口删除：

```text
R03 PASS: anonymous page fault
R03 PASS: copy on write
R03 PASS: supervisor access
R03 PASS: outside user memory
R03 PASS: user OOM rollback
R03 PROBE COMPLETE
```

OOM 场景在 fork 完成后耗尽空闲页，只留下一个页面。子任务匿名缺页先拿到数据页，随后页表页
分配失败；代码释放数据页，再以 SIGSEGV 退出。父任务继续运行、回收子任务，空闲页数恢复。
这同时证明部分页表分配失败没有泄漏。

独立 CPL0 探针访问同类非法地址，得到：

```text
Kernel panic: page fault outside user memory
```

恢复正式入口后，`make -B check` 和普通 QEMU 启动通过，并重新报告：

```text
main memory pages: 3840
free memory pages: 3840
```

## `b438110`：退出时释放用户映射

状态：已实现、已运行验证并提交。父提交：`462db18`。

正式改动涉及：

- `include/linux/mm.h`：声明 `free_user_pages()`；
- `mm/memory.c`：把用户页表子树回收与 CR3 根页回收拆开；
- `kernel/exit.c`：当前任务在自己的 CR3 和内核栈仍有效时释放用户映射，再进入 zombie。

新生命周期是：

```text
do_exit
-> 清空用户 PML4 项
-> 递归释放用户页、PT、PD、PDPT
-> 刷新 TLB
-> TASK_ZOMBIE + SIGCHLD + schedule

waitpid
-> 释放已经为空的 CR3 根页
-> 释放 task 页和任务槽
```

2026-08-06 的一次性 CPL3 探针得到：

```text
R04 PASS: exit releases user mappings
R04 PASS: eight zombies retain two pages each
R04 PASS: wait releases zombie roots and tasks
R04 PASS: SIGSEGV releases user mappings
R04 PASS: nested wait lifecycle
R04 PROBE COMPLETE
```

单个子任务在退出前实际触碰 24 个匿名页并执行一次 COW。父任务在 wait 之前检查到子 PML4 的
用户槽全部为空，空闲页数只比基线少 2，分别对应空 CR3 根页和 task 页。8 个未 wait 的 zombie
精确占 16 页，逐个 wait 后页数逐步恢复。SIGSEGV 和 task 1 阻塞等待 task 2 的路径也满足相同
回收不变量。

独立失败探针得到：

```text
R04 PASS: fork failure rollback
```

该探针把父地址空间布置到两个用户 PT，耗尽内存后让 fork 在复制第二个 PT 时失败。返回
`-EAGAIN` 后，task 槽为空，空闲页数恢复，已经复制的页表和叶页引用全部回滚；父任务随后通过
只读 PTE 写入，验证 TLB 已刷新且单引用页可原地恢复写权限，没有额外分配页面。

探针已经删除，`init/main.c` 恢复正式入口；最终 `make -B check` 与普通 QEMU 启动通过，空闲页
重新为 `3840/3840`。探针没有进入正式入口或提交。提交前再次执行 `git diff --check` 和
`make -B check`，均通过。

## `9c6e5cb`：可重复进程生命周期镜像

状态：已实现、已运行验证并提交。父提交：`b438110`。

`Makefile` 新增独立的 `test-process` 目标；`tests/x86_64/process.c` 作为 CPL0 控制器构造用户
寄存器帧，`tests/x86_64/process_user.S` 提供复制到用户页的最小 CPL3 程序。测试 EFI、ESP、
OVMF vars 和串口日志全部位于 `build/x86_64/test/`，正式 `init/main.c` 与生产对象列表不变。

运行脚本为每次 QEMU 启动复制新的 OVMF vars，通过 `isa-debug-exit` 确定性结束，并同时要求
QEMU 返回成功码、全部 PASS 标记存在且 FAIL/panic 标记不存在。测试目标还检查正式 EFI 不含
`PROCESS TEST` 字符串。

2026-08-06 执行 `make test-process` 得到：

```text
PROCESS PASS: anonymous fault
PROCESS PASS: copy on write
PROCESS PASS: supervisor access
PROCESS PASS: user OOM rollback
PROCESS PASS: fork failure rollback
PROCESS PASS: nested wait
PROCESS PASS: repeated lifecycle
PROCESS TEST COMPLETE
```

其中重复生命周期场景确认 8 个 zombie 精确保留 16 页；深层 fork 失败场景在复制第二个 PT
时返回 `-EAGAIN`，并验证任务槽、空闲页数和叶页引用恢复。用户 OOM 场景只保留一页后触发
数据页成功但新 PT 分配失败，子任务以 SIGSEGV 退出，释放暂存页后空闲页数恢复。

同一改动还通过 `git diff --check`、脚本语法检查和 `make -B check`。正式镜像普通 QEMU 启动
继续报告 `3840/3840`，且不含测试标记。最终暂存区只包含 `Makefile` 和三个测试文件；本地笔记、
规划和构建产物没有进入提交。

## 当前仍然存在的限制

1. 父进程先退出时，孤儿没有 PID 1 接管；
2. 匿名用户范围还没有代码、数据、堆和栈边界；
3. 可重复进程测试镜像还没有覆盖未来的 PID 1、exec 和文件页语义；
4. 尚无 exec、文件页、用户信号处理器和抢占。

紧接着的开发目标见根目录 [`NEXT_STEP.md`](../../NEXT_STEP.md)。下一阶段应建立 exec 所需的用户
映像边界和失败原子性，不在没有真实 init 生命周期时提前实现孤儿重父。
