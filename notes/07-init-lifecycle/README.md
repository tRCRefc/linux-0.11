# 阶段七：init 生命周期

当前提交区间：`7859c26..771ca3a`。本阶段从 2026-08-08 开始。

## `7859c26`：建立真实 PID 1

状态：已实现、已运行验证并提交。父提交：`bcd694a`。

当前行为：

- task 0 保持内核 controller/idle；正式启动挂载 Minix 根并注册 exec 输入后，由
  `create_init()` 从 `/bin/init` 的候选 CR3 创建第一个用户任务，PID 固定为 1；
- 内核构造完整 `iretq` 返回帧，使 init 直接从独立 ELF 的入口和参数栈进入 CPL3；
- init 校验 `argc/argv/envp`、数据和 BSS 后执行一次 `fork -> waitpid(-1)`，确认子进程退出状态，
  生产变体随后持续在 CPL3 调用 `getpid()`；
- PID 1 调用 exit 或因用户异常进入 `do_exit()` 时不成为 zombie：零退出码触发
  `panic("init exited")`，非零退出码触发 `panic("init failed")`；
- `test-init` 使用同一 init 源码的退出变体，在成功 fork/wait 后正常退出，专门验证 PID 1
  的零码退出策略。现有进程和 exec 测试把普通子进程 PID 起点移到 2，避免与系统 init 语义冲突。

验证证据：

```text
git diff --check                         PASS
sh -n tests/x86_64/run-init.sh          PASS
make -B check                           PASS
make test-minix                         PASS
make test-fs                            PASS
make test-process                       PASS
make test-exec                          PASS
make test-init                          PASS
timeout 10s make run                    预期超时 124
nm -u build/x86_64/BOOTX64.EFI          无输出
```

`test-init` 的关键运行输出为：

```text
main memory pages: 3840
free memory pages: 3824
Minix root mounted; /bin/init bytes: 5272
PID 1 created from /bin/init
Kernel panic: init exited
```

该退出只发生在 `INIT_TEST_EXIT` 根镜像。生产根镜像中的 5288 字节 init 在 10 秒 QEMU 运行中创建
PID 1 后保持运行，没有输出 `Kernel panic`。生产 EFI 不包含 PROCESS、EXEC、FS 测试标记或 PASS/
COMPLETE 字符串，根镜像测试前后校验和不变。

本次验证曾发现文件作用域 argv/envp 指针数组会给 PE 产物引入未定义 CRT/TLS 边界符号；改为
内核入口局部数组后，强制重建的 `make -B check` 和独立 `nm -u` 均通过。

当前限制：

1. init 只回收它直接创建的一个测试子进程，尚未实现父进程退出时的孤儿重父；
2. 没有 PIT/PIC 驱动的抢占，生产 init 以系统调用循环保持 CPL3；
3. PID 1 失败策略是立即 panic，尚无重启或恢复机制；
4. 完整 signal、文件描述符、控制终端和用户空间启动脚本仍未实现。

E-03 之后的原子能力是 E-04：父任务退出时把存活或 zombie 子任务重父给 PID 1，并使 init
能回收已退出孤儿。

## `771ca3a`：孤儿重父

状态：已实现、已运行验证并提交。父提交：`7859c26`。

当前行为：

- 非 PID 1 任务进入 `do_exit()` 时，所有仍以它为父的任务先改归 PID 1；
- 如果被改归的任务已经是 zombie，退出路径向 PID 1 设置 SIGCHLD；仍运行的孤儿在以后退出时通过
  既有 `tell_father()` 通知新的父任务；
- 生产 init 在完成首次 fork/wait 自检后持续调用 `waitpid(-1)`，成功时继续回收，无子任务返回
  `-ECHILD` 时通过 `getpid()` 确认仍是 PID 1 并保持 CPL3；
- `test-process` 新增父先退出场景：测试 controller 临时承担 PID 1，父任务 fork 后立即退出，子任务
  被重父后以状态 7 退出；init 观察 SIGCHLD，并用两次 `waitpid(-1)` 先后回收孤儿和父任务。

已通过：

```text
git diff --check                         PASS
make -B check                           PASS
make test-process                       PASS，包含 PROCESS PASS: orphan reparent
make test-exec                          PASS
make test-init                          PASS，退出变体仍命中 init exited
timeout 10s make run                    预期超时 124，无 Kernel panic
nm -u build/x86_64/BOOTX64.EFI          无输出
```

生产启动仍报告 3840/3824 页、65536 字节 ramdisk 和 `PID 1 created from /bin/init`。生产 EFI 不含
测试 PASS/COMPLETE 标记。

验证边界：当前没有时钟抢占或 yield，fork 后父任务会一直运行到 exit/wait，因此运行场景覆盖的是
“父退出时子任务仍存活，重父后子任务退出并通知 init”。父退出瞬间子任务已经是 zombie 的补发
SIGCHLD 分支已实现，但当前调度模型下没有自然的用户态时序能够动态构造；不能把该分支记为运行
验证完成。
