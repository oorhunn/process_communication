# AGENTS.md

Orientation for AI agents. Read this before editing; it should save you from opening every file.

## What this is

A header-only **C++23 process-communication toolkit**. A single pub/sub surface
(`subscribe(key, handler)` + `publish(key, bytes)`) backed by two interchangeable
transports that satisfy the `Transport` concept:

- **`InprocTransport`** — same-process bus. `publish` runs subscribers *inline, synchronously*.
- **`TcpTransport`** — across processes/machines over a non-blocking TCP socket. `publish` *queues* a frame; `poll()` drives all socket I/O. Point-to-point (one peer at a time), not a broker.

Everything lives in namespace `lib::process_communications` (note: plural). This began as
`health_check_template`; that name is retired — call it "Process Communication".

## Layout

```
src/                       header-only library, include dir = src/
  ErrorTypes.hpp           enum class TransportError (the failure contract)
  Heartbeat.hpp            Heartbeat liveness message + "heartbeat" key, namespace ...::heartbeat
  HeartbeatSender.hpp      poll-driven periodic heartbeat sender (compose into your class)
  InprocTransport.hpp      namespace ...::inproc_transport
  TCPTransport.hpp         Transport concept + FdHandle + namespace ...::tcp_transport
examples/                  pubsub_example.cpp, subscriber.cpp, publisher.cpp
tests/common/process_communications/   GoogleTest suite (one *Test.cpp per header)
process_communication.cmake   defines INTERFACE target process_communications / lib::process_communications
CMakeLists.txt             includes the .cmake module, adds examples + tests
```

Includes are **flat**: `#include "TCPTransport.hpp"` (not `"process_communications/..."`).
There is **no `main.cpp`** and no main executable — only example and test binaries.

## Build & test

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build cmake-build-debug
cd cmake-build-debug && ctest --output-on-failure
```

Needs: C++23 compiler (AppleClang/Clang), CMake ≥ 3.31, GoogleTest (tests only,
`brew install googletest`), a POSIX platform (`TcpTransport` uses BSD sockets).

## API

Every `publish`/`start_listening`/`connect_to_peer`/`poll` result is `[[nodiscard]]`.
Failures come back as `std::expected<void, TransportError>` — never exceptions.

```cpp
// InprocTransport  (lib::process_communications::inproc_transport)
explicit InprocTransport(const InprocTransportConfig&);   // no default ctor; non-copyable/movable
void     subscribe(std::string_view key, Handler);        // Handler = void(std::span<const std::byte>)
std::expected<void,TransportError> publish(std::string_view key, std::span<const std::byte>);

// TcpTransport  (lib::process_communications::tcp_transport)
struct TcpTransportConfig { uint16_t m_port{0}; std::string m_host{"127.0.0.1"}; size_t m_max_frame_bytes{1<<24}; };
explicit TcpTransport(TcpTransportConfig);                // no default ctor; non-copyable/movable
void     subscribe(std::string_view key, Handler);
std::expected<void,TransportError> start_listening();     // server side: bind + listen (non-blocking)
std::expected<void,TransportError> connect_to_peer();     // client side: uses m_host/m_port
std::expected<void,TransportError> publish(std::string_view key, std::span<const std::byte>); // queues, then flushes
size_t   poll();                                          // accept+recv+dispatch+send; returns # dispatched. Call every loop tick.
bool     has_peer() const noexcept;
```

`TransportError`: `PUBLISH_FAILED, NOT_CONNECTED, QUEUE_FULL, SOCKET_CREATE_FAILED,
SET_NONBLOCKING_FAILED, BIND_FAILED, LISTEN_FAILED, CONNECT_FAILED, SEND_FAILED`.

```cpp
// Heartbeat  (lib::process_communications::heartbeat) — a serializable liveness message.
inline constexpr std::string_view k_heartbeat_key{"heartbeat"};   // well-known topic
struct Heartbeat { uint32_t m_source_id; uint64_t m_sequence; uint64_t m_timestamp_ms;
                   static constexpr size_t k_wire_size{20}; };     // [source_id u32][seq u64][ts_ms u64] LE
static  Heartbeat            Heartbeat::make(uint32_t source_id, uint64_t sequence);  // stamps system_clock now
std::array<std::byte,20>     Heartbeat::encode() const;            // hand to Transport::publish
std::optional<Heartbeat>     Heartbeat::decode(std::span<const std::byte>);  // nullopt on wrong size; never throws
```

Heartbeat is transport-agnostic (no socket includes): `publish(k_heartbeat_key, hb.encode())`
on either transport; in the subscriber, `Heartbeat::decode(bytes)`.

```cpp
// HeartbeatSender<Transport>  (lib::process_communications::heartbeat) — periodic send utility.
// Transport only needs .publish(key, bytes) (concept HeartbeatPublisher). Non-owning ref; must outlive sender.
struct HeartbeatSenderConfig { uint32_t m_source_id{0}; std::chrono::milliseconds m_interval{1000}; };
HeartbeatSender(Transport& t, HeartbeatSenderConfig cfg);  // CTAD deduces Transport
bool tick();                     // sends if interval elapsed (first tick always sends); call every loop iteration
bool tick(Clock::time_point);    // same, with injectable steady_clock time (tests)
void send_now();                 // force a beat now, resets the interval
uint64_t sequence() const;       // next sequence number
```

Compose it as a member: `HeartbeatSender<TcpTransport> m_hb{m_transport, {.m_source_id = 1}};`
then call `m_hb.tick()` alongside `m_transport.poll()` in your loop.

`FdHandle` (in `TCPTransport.hpp`): move-only RAII wrapper owning a socket fd, `close()`s on destruction.

## Wire format (TcpTransport)

Length-prefixed frame: `[key_size u32 LE][payload_size u32 LE][key bytes][payload bytes]`.
`m_max_frame_bytes` (default 16 MiB) caps both sizes: oversized outbound → `SEND_FAILED`;
a corrupt inbound length drops the connection instead of allocating.

## Gotchas

- Start the listener **before** the client connects (server must be bound first).
- Sockets are non-blocking: nothing moves without `poll()`. Partial sends stay in an outbox, partial frames in an inbox.
- Namespace is `lib::process_communications` (plural); the cmake *module* file is `process_communication` (singular).
- When adding a header to `src/`, add a matching `*Test.cpp` under `tests/common/process_communications/` and list it in `tests/CMakeLists.txt`.
