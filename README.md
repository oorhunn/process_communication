# Process Communication

A small **C++23 process-communication toolkit** built around a single
publish/subscribe abstraction. It ships two interchangeable transports behind a
common `Transport` concept:

| Transport        | Scope                          | Delivery                                   |
| ---------------- | ------------------------------ | ------------------------------------------ |
| `InprocTransport`| Same process / address space   | Synchronous — handlers run inside `publish` |
| `TcpTransport`   | Across processes / machines    | Asynchronous over a non-blocking TCP socket |

Both expose the same minimal surface — `subscribe(key, handler)` and
`publish(key, bytes)` — so code written against the `Transport` concept can swap
between an in-memory bus and a real network socket without changing call sites.

> It's a clean starting point for services that need a health-check / telemetry
> channel between components.

---

## Layout

```
.
├── src/                             # the library (header-only)
│   ├── ErrorTypes.hpp               #   TransportError enum (the failure contract)
│   ├── Heartbeat.hpp                #   liveness message + the "heartbeat" topic key
│   ├── HeartbeatSender.hpp          #   periodic heartbeat send utility (poll-driven)
│   ├── InprocTransport.hpp          #   in-process pub/sub
│   └── TCPTransport.hpp             #   TCP transport, framing, the Transport concept
├── examples/                        # runnable demos
│   ├── pubsub_example.cpp           #   both transports in one process
│   ├── subscriber.cpp               #   standalone listener process
│   └── publisher.cpp                #   standalone sender process
├── tests/                           # GoogleTest suite
│   └── common/process_communications/
│       ├── ErrorTypesTest.cpp
│       ├── InprocTransportTest.cpp
│       └── TCPTransportTest.cpp
├── process_communication.cmake      # defines the lib::process_communications target
└── CMakeLists.txt
```

---

## Requirements

- A C++23 compiler (developed with AppleClang 21 / Clang)
- CMake ≥ 3.31
- [GoogleTest](https://github.com/google/googletest) — for the test suite only
  (e.g. `brew install googletest`)
- A POSIX platform — `TcpTransport` uses BSD sockets (`<sys/socket.h>`, `fcntl`, …)

---

## Build

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build cmake-build-debug
```

This produces:

- `cmake-build-debug/examples/{pubsub_example,subscriber,publisher}`
- `cmake-build-debug/tests/process_communications_tests`

---

## Run the tests

```bash
cd cmake-build-debug && ctest --output-on-failure
```

The suite covers the error contract, in-process delivery semantics, the
`FdHandle` RAII wrapper, the transport's error paths, and **end-to-end framing
over loopback sockets** (accept → publish → decode → dispatch).

---

## Quick start

### In-process (synchronous)

```cpp
#include "InprocTransport.hpp"
using lib::process_communications::inproc_transport::InprocTransport;
using lib::process_communications::inproc_transport::InprocTransportConfig;

InprocTransport bus{InprocTransportConfig{}};

bus.subscribe("orders", [](std::span<const std::byte> bytes) {
    // handle the message
});

bus.publish("orders", payload);   // subscribers run inline, right here
```

### Across processes (TCP)

The TCP transport is non-blocking: `publish()` *queues* a frame, and `poll()`
drives the actual socket I/O (accept, recv, dispatch, send). Call `poll()` from
your event loop.

```cpp
// --- subscriber side ---
TcpTransport server{{.m_port = 9100}};
server.start_listening();
server.subscribe("telemetry", handler);
while (running) { server.poll(); /* sleep a little */ }

// --- publisher side ---
TcpTransport client{{.m_port = 9100, .m_host = "127.0.0.1"}};
client.connect_to_peer();
client.publish("telemetry", payload);
client.poll();   // flushes the queued bytes onto the socket
```

### Heartbeats (liveness)

`Heartbeat` is a small serializable message so components can announce they're
still alive. It works over either transport — publish it under the well-known
`heartbeat` key, and decode it in the subscriber. It carries a source id, a
per-source sequence number, and a timestamp; `encode()`/`decode()` never throw.

```cpp
#include "Heartbeat.hpp"
using lib::process_communications::heartbeat::Heartbeat;
using lib::process_communications::heartbeat::k_heartbeat_key;

// sender: stamp one and publish it (call periodically from your loop)
const auto beat = Heartbeat::make(/*source_id=*/1, /*sequence=*/seq++);
transport.publish(k_heartbeat_key, beat.encode());

// receiver: decode inside the subscriber
transport.subscribe(k_heartbeat_key, [](std::span<const std::byte> bytes) {
    if (const auto hb = Heartbeat::decode(bytes)) {
        // hb->m_source_id, hb->m_sequence, hb->m_timestamp_ms
    }
});
```

**Periodic sending.** `HeartbeatSender` handles the "beat every N ms" bookkeeping
for you — drop it into your own class as a member and call `tick()` from the same
loop that drives `poll()`. It sends once up front, then throttles to the interval,
auto-incrementing the sequence. It works with any transport exposing `publish()`
(the `HeartbeatPublisher` concept) and holds a non-owning reference, so the
transport must outlive it.

```cpp
#include "HeartbeatSender.hpp"
using lib::process_communications::heartbeat::HeartbeatSender;
using lib::process_communications::heartbeat::HeartbeatSenderConfig;
using namespace std::chrono_literals;

struct MyService {
    TcpTransport m_transport{TcpTransport::TcpTransportConfig{.m_port = 9100}};
    HeartbeatSender<TcpTransport> m_heartbeat{
        m_transport, HeartbeatSenderConfig{.m_source_id = 1, .m_interval = 500ms}};

    void run_once() {
        m_transport.poll();   // drive socket I/O
        m_heartbeat.tick();   // beats when 500ms have elapsed
    }
};
```

---

## Demo: two processes talking

Build the example targets, then run the listener and sender in separate terminals:

```bash
cmake --build cmake-build-debug --target subscriber publisher

# terminal 1 — start the listener FIRST (it must be bound before the client connects)
./cmake-build-debug/examples/subscriber 9123

# terminal 2 — then the sender
./cmake-build-debug/examples/publisher 9123
```

Expected subscriber output:

```
[subscriber] listening on port 9123
[subscriber] received: telemetry message #1
[subscriber] received: telemetry message #2
[subscriber] received: telemetry message #3
[subscriber] received: telemetry message #4
[subscriber] received: telemetry message #5
[subscriber] received: bye
[subscriber] exiting
```

Both take an optional port argument (default `9100`).

---

## How the TCP transport works

**Wire format.** Each message is a length-prefixed frame:

```
┌────────────┬──────────────┬───────────────┬───────────────────┐
│ key_size   │ payload_size │ key bytes     │ payload bytes     │
│ u32 (LE)   │ u32 (LE)     │ (key_size)    │ (payload_size)    │
└────────────┴──────────────┴───────────────┴───────────────────┘
   4 bytes       4 bytes
```

- **`FdHandle`** is a move-only RAII wrapper that owns a file descriptor and
  `close()`s it on destruction, so sockets never leak.
- **Non-blocking sockets.** `start_listening`, `connect_to_peer`, sends and
  receives never block; progress happens on `poll()`. Partial sends stay queued
  in an outbox; partial frames stay buffered in an inbox until complete.
- **Safety bound.** `m_max_frame_bytes` (default 16 MiB) caps key/payload sizes;
  oversized outbound messages are rejected with `SEND_FAILED`, and a corrupt
  inbound length drops the connection rather than allocating wildly.
- **Point-to-point.** A `TcpTransport` tracks a single peer at a time — it is not
  a fan-out broker.

**Failure modes** are reported via `std::expected<…, TransportError>`
(`NOT_CONNECTED`, `BIND_FAILED`, `CONNECT_FAILED`, `SEND_FAILED`, …) — see
`ErrorTypes.hpp`.
