# 发展规划：用户映像与 exec 边界

更新日期：2026-08-08

当前已提交基线：`771ca3a kernel: reparent orphaned tasks`

当前进程主线已经具备：

```text
CPL3 / int 0x80
-> fork + 独立 CR3 + COW
-> 用户缺页失败只终止当前任务
-> exit 时释放用户映射
-> zombie 只保留空 CR3 根和 task 页
-> waitpid 回收并唤醒父任务
-> 独立 test-process 镜像持续验证上述行为
```

本文档与 `notes/` 当前临时纳入一次性迁移快照，后续恢复本地维护的步骤见根目录
`MIGRATION.md`。本文只保存尚未完成的开发边界。

## 1. 下一阶段目标

下一阶段进入“可替换的用户程序映像”，最终目标是让真正的 PID 1 从可执行文件建立代码、数据和
用户栈，而不是继续依赖一次性或内嵌机器码。

路线已经选择：先移植只读 ramdisk/Minix 文件读取，再由 exec 从 inode 读取映像。UEFI 阶段
直接加载 init 文件不作为正式方案。

不得把永久内嵌在内核 `.text` 中的用户机器码描述为 exec，也不得让测试镜像对象进入生产 EFI。

## 2. 建议原子提交顺序

### E-01：建立 ramdisk/Minix 可执行映像输入

提交必须建立一个真实可验证的读取能力，而不只是增加结构字段：

- 完成只读 Minix v1 磁盘格式层；
- 增加独立 ramdisk 块来源，并让 Minix 层从真实构建镜像读取 `/bin/init`；
- 输入长度、所有权和物理页保留关系明确；
- 错误输入只返回错误，不破坏当前地址空间；
- 正式启动在没有 init 映像时仍有明确、可诊断的结果；
- 为独立测试镜像增加截断、坏 magic 和越界长度场景。

Minix v1 格式层已经由 `9c93071 fs: read Minix v1 images` 完成；ramdisk 块后端和独立镜像接入
已经由 `c5069fd fs: read Minix root from ramdisk` 完成。

真实只读块输入已经由 `db599d3 block: load Minix root through ATA` 完成：x86-64 内核通过
ATA PIO 从第二块 IDE 盘把 root image 复制到预留的 64 KiB ramdisk，再由正式启动路径挂载和
查找 `/bin/init`。原有 QEMU `loader` 固定物理地址已经从测试路径删除。

该路径仍是启动期同步轮询输入，不等于原版请求队列、buffer cache 或完整硬盘驱动；下一原子
能力转入 E-02，把当前文本 `/bin/init` 替换为真实可校验的可执行映像并建立候选地址空间。

### E-02：`exec: replace user image`

状态：已由 `bcd694a exec: replace user image` 完成并通过运行验证。

在候选地址空间中完成：

- 校验 x86-64 当前支持的可执行头和段范围；
- 建立代码、数据、BSS 和用户栈映射；
- 构造最小 `argc/argv/envp` 栈布局；
- 文件末页未使用部分和 BSS 清零；
- 成功后一次性切换 CR3 和用户 RIP/RSP；
- 任一失败路径释放候选页表并保留旧映像。

不要边拆旧地址空间边加载新映像。失败原子性必须由“候选 CR3 -> 完整验证 -> 最终交换”保证。

`bcd694a` 实现了受限的静态 x86-64 ELF64 `ET_EXEC` 装载、最小 `argc/argv/envp` 栈、syscall 11
和候选 CR3 原子替换。独立 `test-exec` 已运行覆盖成功替换、截断头、非法段地址、缺失文件和 OOM
回滚；正式启动从 ATA 根盘找到真实 ELF `/bin/init`，但尚未创建 PID 1 或执行它。

### E-03：建立真实 PID 1/init 生命周期

状态：已由 `7859c26 init: run PID 1 from root image` 完成，并通过 `make -B check`、完整专项回归、
`make test-init` 和生产 QEMU 运行验证。

- task 0 保持 idle/内核控制任务；
- 第一个用户任务获得 PID 1 并执行 init 映像；
- init 能循环 `waitpid(-1, ...)` 回收退出的后代；
- init 自身意外退出进入明确的内核策略，不能静默留下无人回收的 zombie。

### E-04：`kernel: reparent orphaned tasks`

状态：已由 `771ca3a kernel: reparent orphaned tasks` 完成；活孤儿重父、SIGCHLD、init 的
`waitpid(-1)` 回收和页面恢复已由 `test-process` 运行验证，生产 init 循环也已通过 QEMU 常驻
验证。

- 父任务退出时把仍存活或已 zombie 的子任务改归 PID 1；
- 已 zombie 的孤儿向 init 设置 SIGCHLD；
- `test-process` 增加父先退出、init 回收孙任务的持久场景。

E 系列到此完成。下一阶段建议另立调度与中断规划，优先恢复 PIT/PIC 时钟中断和基于 tick 的
counter 递减/重新调度；在此之前，不能把当前只在显式 `schedule()` 点切换的路径描述为抢占调度。

## 3. 持续回归门槛

每个后续提交至少执行：

```bash
git diff --check
make -B check
make test-process
timeout 10s make run
```

`make test-process` 必须继续看到：

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

正式启动必须继续报告 `3840` 页总内存和预留 ramdisk 后 `3824` 页可分配内存，生产 EFI 必须不含
`PROCESS TEST`、`EXEC TEST` 标记或测试用户程序。

## 4. 暂缓事项

- 没有真实 PID 1 前的孤儿重父；
- 完整 signal handler、mask 和信号返回帧；
- PIT/PIC、抢占和 FPU 状态；
- 完整 waitpid 进程组语义；
- 文件页共享、写时复制文件页和 mmap；
- SMP、线程组和现代 Linux API。

## 5. 完成记录规则

每个原子提交完成后更新对应阶段笔记，记录真实 hash、行为、静态/运行证据和限制。未提交工作
必须明确标记为进行中。迁移快照清理前按 `MIGRATION.md` 跟踪文档；清理后 `notes/` 和
`NEXT_STEP.md` 恢复本地维护。构建产物始终不暂存、不提交。
