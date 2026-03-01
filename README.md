# SimpleFTP

一个基于 RFC 959 的完整 FTP 实现，包含多线程 C 服务端与 Python 客户端（CLI + GUI）。

## 项目结构

```
SimpleFTP/
├── server/          # FTP 服务端（C）
│   ├── src/         # 源文件
│   ├── include/     # 头文件
│   └── test/        # 测试套件
├── client/          # FTP 客户端（Python）
│   └── src/
│       ├── ftp/
│       │   ├── core/    # 核心逻辑
│       │   └── ui/      # CLI / GUI 界面
│       └── main.py
└── doc/             # 项目报告
```

## 功能特性

### 服务端

- 多客户端并发（线程池，可配置最大连接数）
- 主动模式（PORT）和被动模式（PASV）数据连接
- ASCII 与二进制传输模式
- 断点续传（REST 命令）
- 传输中断（ABOR 命令）
- 基于 `users.db` 的用户认证与权限管理
- 路径安全校验（防目录穿越）
- 并发文件锁（读写锁）

**支持的 FTP 命令：** USER、PASS、QUIT、REIN、PORT、PASV、TYPE、MODE、STRU、RETR、STOR、APPE、DELE、REST、ABOR、LIST、NLST、CWD、CDUP、PWD、MKD、RMD、RNFR、RNTO、SYST、FEAT、SIZE、MDTM、NOOP

### 客户端

- 交互式 CLI（支持历史记录）
- Tkinter 图形界面（文件浏览器 + 进度条）
- 批处理模式（`-c` 参数）
- 异步文件传输与断点续传

## 构建与运行

### 服务端

**依赖：** GCC（C99）、CMake 3.28+、pthread

```bash
cd server
mkdir build && cd build
cmake ..
make
```

运行：

```bash
./server [选项]
```

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `-p <port>` | 监听端口 | `2121` |
| `-r <dir>` | FTP 根目录 | `/tmp` |
| `-b <addr>` | 绑定地址 | `127.0.0.1` |
| `-c <n>` | 最大并发连接数（`-1` 不限） | `100` |
| `-l <level>` | 日志级别（DEBUG/INFO/WARN/ERROR） | `INFO` |
| `-a <family>` | 地址族（ipv4/ipv6/unspec） | `unspec` |

示例：

```bash
./server -p 2121 -r /tmp/ftp -b 127.0.0.1 -c 50
```

也可使用 `make` 直接编译（无需 CMake）：

```bash
cd server
make
./server
```

配置用户账号，参考 `server/users.db.example`。

### 客户端

**依赖：** Python 3、tkinter（GUI 模式）

**交互式 CLI：**

```bash
cd client/src
python main.py [host] [-p port] [-u user] [-P password]
```

**图形界面：**

```bash
python main.py --gui
```

**批处理模式：**

```bash
python main.py localhost -p 2121 -c "USER user" -c "PASS pass" -c "LIST"
```

**构建可执行文件（PyInstaller）：**

```bash
cd client
bash build_executable.sh
./dist/client          # CLI
./dist/client --gui    # GUI
```

## 测试

**C 单元测试**（`LoggerTest`、`FilesysTest`）不依赖运行中的服务端，可直接执行。

**Python 集成测试**需要预先在 `127.0.0.1:2121` 启动服务端，并开启匿名登录（默认已开启）。测试文件根目录需可读且存在（默认 `/tmp` 即可）：

```bash
# Step 1：在终端后台启动服务端（端口 2121，绑定本地回环）
cd server/build
./server -p 2121 -r /tmp -b 127.0.0.1 &

# Step 2：在 server/build 目录下运行测试
ctest -V                     # 运行全部测试
ctest -R FTPBasicTest -V     # 基本连通性测试
ctest -L integration -V      # 集成测试
```

> **注意：** 默认绑定地址为 `127.0.0.1`（仅本机回环），若需允许远程客户端连接，启动时需指定 `-b 0.0.0.0` 或具体网卡地址。

测试包含 C 单元测试（文件系统、日志模块）与 Python 集成测试（基本命令、ABOR、REST、重命名/删除等）。

## 技术架构

| 层次 | 模块 | 说明 |
|------|------|------|
| 网络层 | `network.c` | POSIX/Winsock 抽象 |
| 协议层 | `protocol.c` | RFC 959 报文解析与响应格式化 |
| 命令层 | `command.c` / `handler.c` | 命令注册表与处理器 |
| 会话层 | `session.c` | 每连接状态机（CONNECTED → AUTHENTICATED） |
| 传输层 | `transfer.c` | 异步数据传输引擎 |
| 认证层 | `auth.c` | 用户数据库读取与权限校验 |
| 文件系统 | `filesys.c` / `filelock.c` | 跨平台路径操作与并发文件锁 |

## 文档

一些设计和说明可见 [doc/report.pdf](doc/report.pdf)。
