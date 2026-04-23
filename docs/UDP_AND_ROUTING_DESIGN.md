# Shine Proxy: UDP Relay & Routing Engine Design

This document details the architectural additions and design decisions for the SOCKS5 UDP Relay and the GeoSite/GeoIP Routing Engine within the Shine Proxy project.

## 1. SOCKS5 UDP Relay Architecture

### 1.1 Overview
The original SOCKS5 inbound only supported TCP CONNECT commands. To fully support modern network traffic (e.g., DNS, QUIC, gaming), we implemented the SOCKS5 UDP ASSOCIATE command (`0x03`).

### 1.2 Data Flow & Multiplexing
- **UDP ASSOCIATE**: When a client requests UDP relay, `Socks5Inbound` binds a local ephemeral UDP port and returns the IP/Port to the client.
- **Session Lifecycle**: The SOCKS5 specification mandates that the UDP relay session is bound to the lifecycle of the initial TCP connection. We spawn an asynchronous monitor coroutine that reads on the TCP socket; if the socket closes or errors, the UDP socket and the associated multiplexed stream are immediately terminated.
- **Protocol Extension**: `ShineFrame` was extended to include `NEW_UDP` (session initialization) and `UDP_DATA` (payload transport). This allows seamless multiplexing of UDP datagrams over the same single Shine TCP tunnel used for TCP streams.
- **Head-of-Line (HoL) Blocking Prevention**: In multiplexed environments, a congested UDP session could block the entire TCP tunnel's reader loop. We mitigated this by utilizing `try_send` into a lock-free queue (`boost::asio::experimental::concurrent_channel`). If the UDP processing queue is full, the datagram is dropped, preserving the reliability of the multiplexed link.

### 1.3 Routing Injection (InjectedStream)
Standard SOCKS5 UDP encapsulates the target destination inside each UDP packet, rather than in the initial handshake. 
To route UDP traffic correctly, `Socks5Inbound` intercepts the *first* UDP datagram using an `InjectedStream` wrapper. This wrapper extracts the target address and passes it to the `Router`, ensuring UDP traffic is dynamically routed (e.g., to Direct or Proxy outbounds) based on the destination of the very first packet.

## 2. GeoSite and GeoIP Routing Engine

### 2.1 Standard V2Ray Rules Integration
The `Router` component was heavily upgraded to support complex domain and IP matching using the industry-standard `geosite.dat` and `geoip.dat` files (from the `v2ray-rules-dat` project).

### 2.2 Protobuf Parsing
Instead of reinventing the wheel, the project uses the official protobuf schemas (`router.proto`) to parse the `.dat` files.
- `GeoSiteMatcher`: Parses the domain lists and supports categorical matching (e.g., `geosite:openai`, `geosite:cn`).
- `GeoIpMatcher`: Parses CIDR blocks and utilizes an optimized Radix/Trie lookup tree for lightning-fast IP matching.

### 2.3 YAML Configuration Enhancements
The `yaml_loader` was upgraded to support standard V2Ray rule syntax directly within the `domain` and `ip` arrays. 

Supported prefixes:
- `geosite:category` (e.g., `geosite:cn`)
- `domain:suffix.com`
- `full:exact.domain.com`
- `geoip:country` (e.g., `geoip:cn`)

```yaml
route:
  geoip_path: "/app/geoip.dat"
  geosite_path: "/app/geosite.dat"
  rules:
    - domain:
        - "geosite:cn"
        - "domain:bilibili.com"
      ip:
        - "geoip:cn"
      outbound: direct
```

## 3. Concurrency and Memory Safety

### 3.1 Lock-Free Channel Upgrade
A critical design review revealed that `boost::asio::experimental::channel` is not thread-safe. Since `Link` I/O multiplexing runs on a dedicated `strand` while `Session` business logic runs on a multi-threaded `asio::any_io_executor` thread pool, cross-thread data races were imminent.
All inter-coroutine channels (`WriteChan`, `HandshakeSignal`, `PayloadChan`, `UdpPayloadChan`, `CreditChan`) were upgraded to `boost::asio::experimental::concurrent_channel`. This guarantees atomic queue operations without introducing deadlocks.

### 3.2 DNS Caching and Resource Leaks
- **Local DNS Cache**: A TTL-based DNS cache was implemented in `DirectOutbound` specifically for UDP proxying (e.g., resolving domains within SOCKS5 UDP packets). To prevent memory exhaustion, the cache is strictly limited to `1024` entries per session.
- **Session Closure Propagation**: `Session::close()` now explicitly emits a `CloseFrame` across the network. This ensures that when one end of a tunnel terminates unexpectedly, the remote proxy node immediately tears down the session, freeing memory and ephemeral UDP ports.
