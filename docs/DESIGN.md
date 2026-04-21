# Shine Proxy — 需求与设计

## 一、原始需求

> 使用 C++20 和 Boost 库、Abseil 库（hash 表等）实现一个 shine proxy protocol
> 网络代理程序，整体架构采用多输入端、路由策略、多输出端；输入端经过路由策略转到
> 输出端。Conan 管理依赖。

逐条：

| # | 需求 | 承载层 |
|---|------|--------|
| 1 | TCP → RESP2 → Shine Proxy Protocol 三层；RESP2 双向对等、非标准语义，允许多路复用 | `src/proto/` |
| 2 | 多 inbound / 多 outbound（类似 xray-core） | `src/inbound/`、`src/outbound/` |
| 3 | 路由：按 inbound → outbound，按目标 IP/域名 → outbound；可扩展轮询/随机；必须有 direct outbound | `src/route/router.*` + 配置校验 |
| 4 | YAML 配置 | `src/config/yaml_loader.cpp`（yaml-cpp） |
| 5 | RESP2 链接带 ping/pong 心跳；连接超时就断 | `Link::heartbeatLoop` |
| 6 | 多路复用；C++20 协程 + Boost.Asio awaitable | 贯穿 |
| 7 | Prometheus 主动拉取指标 | `src/metrics/exporter.cpp`（prometheus-cpp） |
| 8 | 类 Redis 的密码鉴权 | `HELLO` + `AUTH` 帧 |
| 9 | 优雅关闭 | signal_set + `graceful_timeout` |
| 10 | Shine 协议简洁完整；RESP2 中 session key 可随机或上层下传 | 客户端生成 64-bit 随机 sid |
| 11 | v1 支持自定义 RESP2 Proxy / Direct / SOCKS5 / HTTP CONNECT | 全部 4 种实现；后补 Block |
| 12 | outbound 慢、inbound 快时用类 TCP 窗口做 flow control，阻塞对应 session | `Session::write` 信用机制 + `WINDOW` 帧 |
| 13 | 最大 payload 与 Redis 一致 | 512 MB，`Resp2Parser::kMaxBulkLen` |
| 14 | 目标地址支持域名+端口、IPv4/IPv6+端口 | `class Address` + `NEW` 帧中的二进制 addr blob |
| 15 | 高性能数据结构优先用 Abseil | `absl::flat_hash_map/set`、`InlinedVector`、`StatusOr` |
| 16 | 最高效数据结构、注重高速缓存 | Session 热字段对齐 64B；单趟零拷贝 RESP2 decoder；ring buffer；shared_ptr 引用计数替代拷贝 |

后续增量需求：

- **Block outbound**：屏蔽某些流量（广告、tracking 之类） — `BlockOutbound`
- **减少异常**：自己代码里全部走 `Status`/`error_code`；仅在第三方边界（yaml-cpp、prometheus-cpp）保留 try/catch
- **Dockerfile + GitHub Actions**：多架构（linux amd64/arm64、macos arm64/x86_64）的 CI 构建

---

## 二、项目目录结构

```
shine/
├── conanfile.py                 # Conan 2.x recipe
├── CMakeLists.txt               # 顶层 cmake（7 个静态库 + exe + tests）
├── Dockerfile                   # 多阶段镜像（build → runtime）
├── .dockerignore
├── .github/workflows/build.yml  # native 矩阵 + docker buildx + release
├── configs/example.yaml         # 示例配置
├── docs/DESIGN.md               # 本文
├── include/shine/               # 预留的对外头目录（暂空）
├── src/
│   ├── core/        # scheduler, logging, signal, buffer, common
│   ├── proto/       # resp2_parser/writer, shine_frame
│   ├── transport/   # address, link (多路复用), session (流控), pipe, tcp_stream
│   ├── inbound/     # socks5, http-connect, shine + factory
│   ├── outbound/    # direct, block, shine-client, socks5, http-connect + factory
│   ├── route/       # router (规则编译/匹配)
│   ├── config/      # schema + yaml_loader
│   ├── metrics/     # registry + prometheus exporter
│   └── main.cpp
└── tests/           # gtest: resp2, shine_frame, address, route, flow_control
```

---

## 三、协议设计（Shine over RESP2）

### 3.1 承载层 RESP2（非标准语义）

- 顶层只接受两种 RESP2 类型：
  - `*`（Array of BulkStrings）→ **控制帧**
  - `$`（BulkString）→ **DATA 帧**（内部打包紧凑二进制）
- 其他类型视为协议错误。
- 单趟状态机解码，零拷贝，流式读取：
  ```
  [头 '*N\r\n'] [重复 N 次 '$len\r\n<bytes>\r\n']
  [头 '$M\r\n'] [<bytes(M)> '\r\n']
  ```
- BulkString 最大长度 **512 MB**（与 Redis `proto-max-bulk-len` 一致）。
- 与标准 Redis 的差别：**双向对等，无请求/响应绑定**；任意一方可随时发帧。

### 3.2 控制帧（RESP2 Array）

| 命令 | 参数（按顺序） | 语义 |
|------|--------------|------|
| `HELLO` | `version` `features` | 握手（双方都发） |
| `AUTH` | `password` | 客户端鉴权 |
| `OK` | — | 通用确认 |
| `ERR` | `code` `message` | 错误 |
| `NEW` | `sid(8B BE)` `addr_blob` `init_window(4B BE)` | 开一个逻辑会话 |
| `ACK` | `sid(8B BE)` | 服务端确认 NEW |
| `CLOSE` | `sid` `reason` | 正常半关闭 |
| `RESET` | `sid` | 异常强制关闭 |
| `WINDOW` | `sid` `delta(4B BE)` | 授权对端 delta 字节发送信用（反压） |
| `PING` | `nonce(8B BE)` | 心跳 |
| `PONG` | `nonce(8B BE)` | 心跳回复 |

### 3.3 DATA 帧（紧凑 BulkString）

```
wire: $<N>\r\n <magic=0xD1:1B> <sid:8B BE> <payload:N-9 B> \r\n
```

- 解码器识别 `$` 顶层且首字节 `0xD1` → DataFrame。
- 热路径无需 Array 开销；仍复用同一个 RESP2 解码状态机。
- 单条 DATA 上限 `max_data_frame`（默认 256 KiB），避免队头阻塞；大 payload 由发送端自动切片。

### 3.4 Session 目标地址 `addr_blob`

```
<type:1B> <len:1B> <bytes:len> <port:2B BE>
type = 0x01 IPv4  (len=4)
type = 0x02 IPv6  (len=16)
type = 0x03 Domain (len=1..255 UTF-8)
```

`class Address` 统一封装解析/格式化/resolve（boost::asio::ip::tcp::resolver）。

### 3.5 握手流程

```
client                          server
  |-- HELLO "1" "mux,flow" ----->|
  |<----- HELLO "1" "..." -------|
  |-- AUTH "password" ---------->|   (只在设置 password 时)
  |<----- OK / ERR --------------|
  |--- (enter mux phase) --------|
```

- `handshake_timeout`（默认 5s）未完成 → 关链。
- 客户端必须等待 `OK`/`ERR` 才能开 session（`Link::waitHandshake`）。

### 3.6 心跳与超时

- 空闲 `ping_interval`（默认 15s）→ 发 `PING nonce`。
- 对端 `pong_timeout`（默认 10s）未回 → 关链，所有 session 被 RESET。
- PING nonce 随机 u64；回 PONG 必须匹配。

### 3.7 Session 级流控

类 TCP 滑动窗口：

- 每个 session：`send_credit`（我可以再发多少字节）、`recv_unacked`（已接收并消费、待发 WINDOW）。
- 初始 `init_window`（默认 256 KiB）随 `NEW` 声明。
- 发送端：DATA 发送前扣信用；如 `send_credit <= 0`，`Session::write` 在 `credit_signal_` channel 上挂起。
- 接收端：payload 被应用层消费后累计 `recv_unacked`；到达 `init_window/2` 就发一次 `WINDOW delta` 并清零。
- 对端收到 `WINDOW` → `Session::onWindow` 增加 `send_credit` 并 `try_send` 唤醒写协程。

这样**上游慢、下游快**时，发送端协程被自然挂起，反压沿链路回传到 TCP 读。

---

## 四、传输层架构

### 4.1 `Link`（一条 TCP 上的复用通道）

- 三个协程绑定在同一 strand：
  1. `readerLoop` — 从 socket 读字节 → RESP2 decode → dispatch
  2. `writerLoop` — 从 `write_chan_` (channel<string>) 取 wire bytes → `async_write`
  3. `heartbeatLoop` — 每秒 tick，管 PING/PONG
- Session 表：`absl::flat_hash_map<u64, SessionPtr>`，受 `absl::Mutex` 保护（写少读多）
- `handshake_signal_` — `experimental::channel<void(error_code)>`，让 `waitHandshake()` 挂起等待。
- **关键设计修正**：`dispatchFrame(NEW)` 处理必须用 `co_spawn(..., detached)` 调起 `on_new_session_`，否则 reader 被 session 生命周期阻塞，DATA 帧堆在 kernel TCP buffer 不被消费。

### 4.2 `Session`（单会话）

```cpp
struct alignas(64) Session {         // 热字段塞进 1 条 cache line
    uint64_t sid;
    State    state;
    uint8_t  flags;
    int32_t  send_credit;
    uint32_t recv_unacked;
    uint64_t bytes_in, bytes_out, last_activity_ns;
    std::unique_ptr<SessionCold> cold;  // 冷字段放堆
};
```

- 实现 `ISessionStream`，对上层（outbound handle 函数）就是一个字节流。
- 收数据：`inbox_`（experimental::channel，1024 元素背压）。
- 发数据：`Session::write` 内部分块 + 信用管理。
- 通道 executor 全部绑定到 Link 的 strand → 免锁。

### 4.3 `ISessionStream`

统一抽象：

- `TcpSocketStream` — 包一个 `tcp::socket`（socks5/http inbound 的客户端连接、direct outbound 的上游连接）
- `Session` — 作为 shine 协议中的逻辑流

outbound `handle(SessionRequest)` 只看到 `client_stream` 是 `ISessionStream`，用 `bidiCopy(a, b)` 双向桥接，不需要关心底下是 TCP 还是 shine session。

---

## 五、Inbound / Outbound 抽象

### 5.1 `SessionRequest`

```cpp
struct SessionRequest {
    std::string inbound_tag;
    std::string inbound_protocol;
    Address     target;
    std::shared_ptr<ISessionStream> client_stream;
};
```

inbound 的职责：握完所在协议的手，产出 `SessionRequest` 交给 dispatch。

### 5.2 Inbound 实现

| 类 | 说明 |
|----|------|
| `Socks5Inbound` | RFC 1928 + 可选用户名密码 (RFC 1929)；仅 CONNECT |
| `HttpConnectInbound` | 只支持 `CONNECT host:port` |
| `ShineInbound` | 接 shine 协议（被动侧 handshake = server），每条 NEW 转出 `SessionRequest` |

### 5.3 Outbound 实现

| 类 | 说明 |
|----|------|
| `DirectOutbound` | 直接 `tcp::socket::async_connect` + bidiCopy |
| `BlockOutbound` | 立即 `shutdownWrite` + `close`，记 debug 日志 |
| `ShineOutbound` | 维护到上游的 shine link 连接池（`pool_size`，RR 选一条），`openSession` 开逻辑会话 |
| `Socks5Outbound` | 作为 socks5 客户端握手 |
| `HttpConnectOutbound` | 作为 HTTP CONNECT 客户端握手 |

---

## 六、路由引擎

```cpp
struct CompiledRule {
    std::optional<std::string>       inbound_tag;    // 精确
    absl::flat_hash_set<std::string> domain_exact;
    std::vector<std::string>         domain_suffix;  // endsWith 带点号归一
    std::vector<IpRange>             cidrs;          // v4/v6 统一
    std::string                      outbound_tag;
};
```

- `Router::pick(req)` 顺序匹配规则，没中命中走 `default_outbound`。
- 配置校验（`Config::validate`）：
  - inbound、outbound tag 非空且唯一
  - 至少一个 `direct` outbound
  - 规则与 default 引用的 outbound 必须存在

后续扩展轮询/随机：把 outbound 变成 group（预留 `IOutboundSelector` 接口）。

---

## 七、配置（YAML）

```yaml
log:
  level: info

server:
  io_threads: 0             # 0 = hardware_concurrency
  graceful_timeout: 30s

metrics:
  listen: 0.0.0.0:9100
  path: /metrics

inbounds:
  - tag: s5-local
    protocol: socks5
    listen: 127.0.0.1:1080
  - tag: http-local
    protocol: http-connect
    listen: 127.0.0.1:1081
  - tag: shine-in
    protocol: shine
    listen: 0.0.0.0:7011
    password: "s3cret"
    max_payload: 512MB
    ping_interval: 15s
    pong_timeout: 10s
    init_window: 256KB

outbounds:
  - tag: direct
    protocol: direct
  - tag: deny
    protocol: block
  - tag: us-shine
    protocol: shine-client
    server: 127.0.0.1:7011
    password: "s3cret"
    pool_size: 2
    ping_interval: 15s
    init_window: 256KB

route:
  rules:
    - inbound: shine-in
      outbound: direct
    - domain_suffix: [".ads.example", "tracking.example"]
      outbound: deny
    - domain_suffix: [".cn", ".local"]
      outbound: direct
    - cidr: [127.0.0.0/8, ::1/128, 10.0.0.0/8, 192.168.0.0/16]
      outbound: direct
  default: us-shine
```

时长用 `15s / 1m / 500ms`，大小用 `256KB / 512MB` 等单位后缀（见 `yaml_loader.cpp::parseDuration / parseSize`）。

---

## 八、并发与性能

- **单 io_context + N workers**（N = `hardware_concurrency` 或配置）
- **每条 Link 一个 strand**，所有状态变更串行化 → 写路径无锁
- **协程**：`boost::asio::awaitable<T>` + `co_spawn(..., detached)` + `use_awaitable`
- **I/O**：`async_read_some`/`async_write` 搭 `ReadBuffer`（线性可增长 + 懒 compact，而非 `read_until`）
- **Hash 结构**：`absl::flat_hash_map/set`（session 表、路由 domain 集、待扩展 DNS 缓存）
- **小容器**：`absl::InlinedVector` 可替代短 vector
- **字符串**：热路径用 `absl::string_view`
- **DATA 帧**：上行由 `std::shared_ptr<std::string>` 持有 payload，inbox channel 传递指针而非拷贝
- **错误传播**：整个代码库用 `Status`/`StatusOr`/`error_code`，不用异常（见下一节）

---

## 九、错误处理策略

**原则**：我们自己代码里不抛异常、不 try/catch。

- 所有 asio 异步调用都用 `asio::redirect_error(use_awaitable, ec)`，返回 `error_code` 而非抛异常。
- 返回值一律 `absl::Status` / `absl::StatusOr<T>`。
- `try/catch` 只出现在**第三方抛异常的边界**：
  - `src/config/yaml_loader.cpp`：`YAML::Exception` → 转 `InvalidArgumentError`
  - `src/metrics/exporter.cpp`：`prometheus::Exposer` ctor 可能抛（HTTP bind 失败）→ 转 `UnavailableError`
  - `src/core/scheduler.cpp`：`io_context::run()` 外层兜底，防止 worker 线程吞异常静默挂掉
- 编译器层：不关闭 `-fexceptions`（asio 自己用异常传递 cancellation，yaml-cpp 也需要），但我们自己写的代码遵循上述约束。

---

## 十、优雅关闭

1. `signal_set{SIGINT, SIGTERM}` 在 main 线程注册。
2. 收到信号：
   a. 遍历所有 `IInbound::stop()`，关闭 acceptor（不再接新连接）。
   b. 对所有 outbound 调 `stop()`（ShineOutbound 关连接池）。
   c. 给 grace `server.graceful_timeout`（默认 30s）让在途 session 自然结束。
3. `metrics::Exporter::stop()` 停 HTTP server。
4. `Scheduler::stop()` → `work_guard.reset() + io_.stop()`。
5. `Scheduler::join()` 等所有 worker 线程退出。

测试输出：SIGTERM 到收工日志 `shine exited cleanly` 在毫秒级（当前例子约 ~100ms，完全 drain 则最多 graceful_timeout）。

---

## 十一、Metrics（Prometheus pull）

独立 HTTP server（`metrics.listen`，默认 `0.0.0.0:9100`），`prometheus-cpp` 的 `pull` exposer。

关键指标（全 `shine_` 前缀）：

| 指标 | 类型 | labels |
|------|------|--------|
| `shine_inbound_connections_total` | counter | inbound, protocol |
| `shine_sessions_active` | gauge | inbound, outbound |
| `shine_sessions_opened_total` | counter | inbound, outbound |
| `shine_sessions_closed_total` | counter | inbound, outbound, reason |
| `shine_bytes_total` | counter | direction, inbound, outbound |
| `shine_connect_latency_seconds` | histogram | outbound |
| `shine_errors_total` | counter | kind |
| `shine_resp2_decode_errors_total` | counter | — |

当前 main.cpp 已经在开/关 session 时更新 active gauge + opened counter + errors counter；bytes/latency 留给后续接入。

---

## 十二、构建

### 本地

```bash
conan install . --build=missing -s build_type=Release
cmake --preset conan-release
cmake --build --preset conan-release -j
ctest --preset conan-release
./build/Release/shine -c configs/example.yaml
```

### Docker（多阶段）

```bash
docker build -t shine .
docker run --rm -p 1080:1080 -p 1081:1081 -p 9100:9100 \
  -v "$PWD/configs/example.yaml:/app/example.yaml:ro" shine
```

Dockerfile 关键点：

- `ubuntu:24.04` 构建，Conan 安装在 `/opt/conan-venv` 隔离 venv。
- 构建阶段先 `COPY conanfile.py CMakeLists.txt cmake/`，再 `conan install` → 依赖图进入独立层，代码变更不重跑依赖。
- 运行阶段只保留 `shine` 可执行 + tini + ca-certificates + 非 root 用户 `shine`。

### CI（GitHub Actions `.github/workflows/build.yml`）

三段：

1. **native** 矩阵（4 个）：
   - `ubuntu-24.04` (linux amd64)
   - `ubuntu-24.04-arm` (linux arm64)
   - `macos-14` (macos arm64)
   - `macos-13` (macos x86_64)
   - 各自 conan cache、build、`ctest`、打包 `shine-<arch>.tar.gz` 上传 artifact
2. **docker-multiarch**：
   - QEMU + buildx，构建 `linux/amd64` + `linux/arm64`
   - PR 只 build 不 push；push/tag 推到 GHCR（`ghcr.io/<repo>`），用 `metadata-action` 自动生成 tag（branch / sha / semver）
3. **release**（只在 `v*` tag）：
   - 下载所有 native artifact
   - `softprops/action-gh-release` 发 GitHub Release，附带所有压缩包

触发：`push` 到 main/master、`pull_request`、tag `v*`、`workflow_dispatch`。

---

## 十三、v1 范围外（后续）

- TLS（预留 `ssl::stream` 接口，上层协议不变即可接入）
- UDP / QUIC inbound/outbound
- 路由策略扩展：轮询/随机/权重/健康检查/优先级组
- GeoIP / GeoSite 数据库
- 热更新配置（SIGHUP）
- 更多流量指标（bytes/latency histogram 的实际挂点）
