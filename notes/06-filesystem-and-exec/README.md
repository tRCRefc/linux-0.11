# 阶段六：文件系统、exec 与 init 映像

当前提交区间：`9c93071..bcd694a`，开始日期：2026-08-06。

本阶段目标是沿接近原版 Linux 0.11 的路径，从 Minix 文件系统读取独立可执行文件，再建立
exec 和真实 PID 1。UEFI 直接加载 init 不作为正式路线。

## `9c93071`：只读 Minix v1 格式层

状态：已实现、已验证并提交。父提交：`9c6e5cb`。

改动边界：

- `include/linux/minix.h`：定义与主机字长无关的运行时 inode 和同步块读取接口；
- `fs/minix.c`：解析 Minix v1 超级块、32 字节磁盘 inode、14 字节目录项以及直接、一级间接、
  二级间接块；
- `tests/minix.c`：构造带 `/bin/init` 和跨直接/间接边界文件的 Minix v1 镜像；
- `Makefile`：把只读格式层加入正式 EFI，并增加独立 `make test-minix` 主机测试。

原版职责与当前脚手架的界线：

```text
保留：Minix v1 磁盘布局、根 inode、目录遍历、7+512+512*512 块映射
过渡：同步 read_block 回调，没有 buffer cache、请求队列或 inode cache
未实现：ramdisk 块来源、ROOT_DEV、mount_root、写路径和 exec
```

选择同步只读边界的原因不是规避原版结构，而是原 `buffer.c/inode.c/namei.c` 当前依赖尚未移植的
睡眠队列、完整 task 字段、i386 `fs` 段和中断块设备。先固定磁盘格式能让下一步 ramdisk 和后续
buffer cache 使用同一份外部输入验证。

已通过：

```text
MINIX TEST PASS: superblock and inode
MINIX TEST PASS: directory lookup
MINIX TEST PASS: direct and indirect reads
MINIX TEST PASS: malformed images
```

数据场景分别跨越直接块到一级间接块边界，并通过稀疏大文件命中二级间接块。错误场景覆盖坏
magic、截断镜像、缺失路径、超长目录分量和 inode 把元数据块伪装成文件数据块。
同一改动通过 `git diff --check`、`make -B check` 和完整 `make test-process`；正式 EFI 无未定义
符号和不允许的 32 位绝对重定位。普通 QEMU 启动继续报告 `3840/3840`。

提交前暂存区只包含 `Makefile`、格式层头文件和实现、结构化主机测试；本地笔记、规划和构建
产物没有进入 `9c93071`。

## `c5069fd`：只读 ramdisk 与独立根镜像

状态：已实现、已运行验证并提交。父提交：`9c93071`。

实现边界：

- `kernel/blk_drv/ramdisk.c`：保留原版 i386 路径，在 x86-64 分支增加定长内存区的同步只读
  1 KiB 块访问；
- `include/linux/ramdisk.h`：定义 ramdisk 所有权和长度边界，并把 PE/PIC 回调符号声明为 hidden；
- `tests/minix.c`：除结构化主机测试外，还能生成独立 64 KiB `root.img`；
- `tests/x86_64/fs.c` 与 `run-fs.sh`：使用独立测试 EFI，在 QEMU 物理内存中挂载 root image；
- `Makefile`：新增 `make test-fs`，生产 EFI 链接 ramdisk 后端但不包含根镜像或测试入口。

根镜像具有正确的 inode/zone 位图和目录链接计数。`fsck.minix -f -s -l` 验证得到 32 个 inode、
64 个 block，并列出：

```text
/bin
/bin/init
/large
/huge
```

内核 QEMU 测试得到：

```text
FS PASS: ramdisk bounds
FS PASS: mount Minix root
FS PASS: read /bin/init
FS TEST COMPLETE
```

首次运行在把 `rd_read` 作为函数指针传递时停住；直接块读取已证明 magic 为 `0x137f`。原因是
PE/PIC 外部函数地址生成 GOT 重定位，运行时不能得到直接代码地址。hidden 声明让链接器使用
RIP 相对地址，修复后全部测试通过，临时 trace 已删除。

同一改动通过 `git diff --check`、脚本语法检查、`make test-minix`、`make -B check`、完整
`make test-process` 和普通 QEMU `3840/3840` 启动。测试目标还确认生产 EFI 不含 `FS TEST` 或
`minix init image` 字符串。

当前仍然只是独立镜像到内存块后端的运行证明。QEMU `loader` 是测试输入手段，不是原版根盘
加载器，也没有进入生产启动命令；后续需要从真实块设备把镜像装入受内核管理的 ramdisk 页。

## `db599d3`：从 ATA 根盘装载 ramdisk

状态：已实现、已运行验证并提交。父提交：`c5069fd`。

实现边界：

- `kernel/blk_drv/hd.c`：在保留 i386 原实现的同时，为 x86-64 增加 LBA28、PIO、轮询式同步
  读取，每个 Minix block 对应两个 512 字节扇区；
- `kernel/blk_drv/ramdisk.c`：恢复接近原版 `rd_load()` 的职责，先读取超级块决定镜像 block 数，
  只有完整复制成功后才开放 ramdisk 读取；
- `init/main.c`：从主内存开头预留 64 KiB，不把这些页交给 `mem_init()`，再从 Primary Slave
  装载根镜像、挂载 Minix 并查找 `/bin/init`；
- `Makefile` 与 `tests/x86_64/run-fs.sh`：把独立 `root.img` 作为第二块 IDE 盘，不再使用 QEMU
  `loader` 固定物理地址。

已验证：主机测试覆盖成功复制、坏 magic、容量溢出和截断来源，失败时 `rd.length == 0`；
QEMU 文件系统测试得到 `ATA root input`、ramdisk bounds、mount 和 `/bin/init` 四项 PASS，且运行
前后根镜像校验和一致。正式启动报告 `3840` 页总内存、预留后 `3824` 页可分配、65536 字节
ramdisk 和 17 字节 `/bin/init`；不附加根盘时明确 panic。完整进程生命周期测试仍全部通过。

这仍不是原版中断驱动的请求队列和 buffer cache：ATA 输入只在早期启动期轮询读取，设备号、
分区表、并发 I/O 和写路径尚未恢复。

## `bcd694a`：替换用户 ELF64 映像

状态：已实现、已运行验证并提交。父提交：`db599d3`。

当前实现：

- `fs/exec.c` 的 x86-64 分支从当前用户页表有界复制路径、`argv` 和 `envp`，只接受静态
  x86-64 ELF64 `ET_EXEC`、页对齐 `PT_LOAD` 和位于当前用户范围内的入口及段；
- 装载器先用 `new_pg_dir()` 建立候选 CR3，把文件内容、零填充 BSS 和单页用户栈全部写入候选
  地址空间，再一次性替换 `current->pg_dir` 和系统调用返回帧的 RIP/RSP；
- ELF、Minix 读取或内存分配失败时释放候选页表，旧 CR3、旧 RIP/RSP 和用户映像保持不变；
- x86-64 syscall 11 已接入 `sys_execve`，正式根文件系统挂载后注册为 exec 输入；
- 测试根镜像中的 `/bin/init` 已由文本替换为独立构建的 5032 字节 ELF，init 对
  `argc/argv/envp`、已初始化数据和 BSS 零值进行运行检查后通过 `exit` 返回；
- 新增隔离的 `make test-exec` EFI，生产 EFI 不链接测试调用桩或 init 对象。

本次已验证：

```text
EXEC PASS: replace user image
EXEC PASS: malformed image rollback
EXEC PASS: invalid segment rollback
EXEC PASS: missing image rollback
EXEC PASS: OOM rollback
EXEC TEST COMPLETE
```

其中成功场景真实经过 CPL3 `int 0x80`、候选 CR3 交换和 `iretq` 到新 ELF；截断头、非法段地址、
缺失 inode 和只剩一页物理内存的场景都由旧用户调用桩继续执行并正常退出。每轮结束后的空闲页数
恢复证明候选页表和进程生命周期没有遗留页。

同一工作树通过 `git diff --check`、`make -B check`、`make test-minix`、`make test-fs`、完整
`make test-process` 和 `make test-exec`。正式 QEMU 启动报告 `3840` 页总内存、`3824` 页可分配、
65536 字节 ramdisk，并从根盘找到 5032 字节 `/bin/init`；正式入口尚未创建 PID 1 或执行 init。

当前限制：页表映射接口尚未表达 ELF 的 RX/RW/NX 权限，所有用户叶页仍沿用现有可写、可执行
过渡属性；exec 只支持最多 8 个页对齐 `PT_LOAD`、16 个参数/环境字符串和 512 字节字符串数据，
没有解释器、动态链接、重定位、文件权限或多页参数栈。

## 下一原子能力

建立真实 PID 1：task 0 保持内核 idle/controller，第一个用户任务获得 PID 1 并从正式根盘执行
`/bin/init`；同时明确 init 正常或意外退出时的内核策略。

## 当前限制

1. ATA 根盘装载只支持启动期 Primary Slave 和 LBA28 PIO；
2. 同步读取没有缓存、等待队列、中断驱动调度或并发语义；
3. 只支持 Minix v1 `0x137f`、1 KiB block 和 `s_log_zone_size == 0`；
4. 尚无写路径、权限模型、文件描述符或 PID 1。
