# libremessage

`libremessage` is a C++23 messenger prototype with a hand-written TCP/TLS stack.
It uses epoll for I/O, OpenSSL for TLS, protobuf for messages, and spdlog for
logging. The repository currently builds one executable, `//:application`, which
can both listen and connect.

> Status: work in progress. M0 (test safety net) and M1 (disconnect cleanup) are
> complete. M2 (transport hardening: non-blocking connect/TLS, send/flush thread
> safety, timeouts, shutdown) is in progress. The protocol is still a
> development handshake plus text messages, not protocol v1.

## What works

- IPv4/IPv6 endpoints, TCP listener/connector, epoll event loop with eventfd
  wakeup.
- Plain TCP and TLS transports; non-blocking sockets and non-blocking TLS
  handshake.
- Length-prefixed protobuf framing: `uint32` network-order length + payload.
- Registration handshake: each peer sends its 16-byte hex `user_id` after
  connecting.
- `Manager` maps `recipient_id` to a local `ConnectionId` and routes text
  messages.
- Disconnect cleanup, stale fd/event protection, recipient mapping cleanup.
- Persistent `user_id` in the current working directory.
- Unit and e2e tests, ASAN/UBSAN configs.

## Not there yet

- Protocol versioning/negotiation, authentication, delivery acks, and explicit
  ordering semantics.
- Connect, TLS-handshake, and idle timeouts.
- Graceful TLS/TCP shutdown and final send-buffer drain.
- Production TLS verification: the current server uses `SSL_VERIFY_NONE` and a
  local self-signed certificate.
- TSAN cannot run in this environment: it fails to start with
  `ThreadSanitizer: unexpected memory mapping ...`.

## Architecture

```
app/              Manager, session/user_id, recipient -> connection mapping
models/           protobuf envelope, framing, message codec
net/event_loop/   epoll + eventfd event loop
net/transport/    plain/TLS transports, send/receive buffers
net/network/      sockets, endpoints, listener, connections, peer pool
tests/            end-to-end tests that launch real application processes
main.cpp          stdin command loop and application entry point
```

Send path:

```
Manager::send
  -> MessageCodec::encode
  -> Framer::frame
  -> SendBuffer
  -> TCPTransport (plain or TLS)
```

Receive path:

```
epoll EPOLLIN
  -> TCPPeer::on_read
  -> ReceiveBuffer
  -> Framer
  -> MessageCodec::decode
  -> Manager handler
```

### Bazel targets

Binary:

- `//:application`

Libraries:

- `//app:business`
- `//net/network:network`
- `//net/event_loop:event_loop`
- `//net/transport:transport`
- `//models:models`
- `//models/proto:message_cc_proto`

## Requirements

- Linux with epoll.
- Bazelisk and Bazel 9 with bzlmod.
- A C++23 compiler.
- OpenSSL headers and libraries (`-lssl -lcrypto`).
- The `openssl` CLI for e2e test certificate generation.

Dependencies (`spdlog`, `protobuf`, `googletest`, `rules_cc`, `rules_proto`) are
resolved through `MODULE.bazel`. OpenSSL is linked from the system and is not a
declared Bazel dependency.

## Build

```sh
bazelisk build //...
```

The repository's `.bazelrc` sets `--spawn_strategy=local`. This is required in
the current environment because the default sandbox conflicts with ccache.

## Tests

```sh
bazelisk test //...
bazelisk test //... --config=asan
bazelisk test //... --config=ubsan
```

Test targets:

- `//models:message_codec_test`
- `//models:framer_test`
- `//net/transport:transport_test`
- `//net/network:handlers_test`
- `//net/network:connection_test`
- `//app:connection_manager_test`
- `//tests:e2e_test`

`//tests:e2e_test` starts real `//:application` processes with TLS enabled,
separate temporary working directories, and dynamically allocated ports. It
generates certificates with the `openssl` CLI at runtime.

## Running

```sh
bazel-bin/application
```

The program first reads a listen port from stdin, then accepts commands:

```text
9000
connect 9001
send <recipient_id> <text>
```

It binds `127.0.0.1` with TLS enabled. `server.crt` and `server.key` must exist in
the working directory; both are gitignored. Each instance writes `user_id` to its
current working directory, so run peers from separate directories.

Example:

```sh
mkdir -p /tmp/peer-a /tmp/peer-b
cp server.crt server.key /tmp/peer-a/
cp server.crt server.key /tmp/peer-b/

(cd /tmp/peer-a && /absolute/path/to/bazel-bin/application)
(cd /tmp/peer-b && /absolute/path/to/bazel-bin/application)
```

Use the 16-character lowercase hex `user_id` from peer B as the recipient id in
peer A's `send` command.

## Current limitations

- The TLS setup is development-only: certificate verification is disabled and
  the server identity is a local self-signed certificate.
- The application is a single-process demo controlled through stdin.
- Message delivery is best-effort over TCP; there are no application-level acks
  or retry semantics yet.
- The protocol has no version field or negotiation yet.

## Roadmap

- **M2.1** Connection lifecycle and state machine: connect pending -> connected
  -> TLS handshake -> established -> closing.
- **M2.2** Send/flush thread safety between the application thread and the event
  loop.
- **M2.3** Timeouts and graceful shutdown.
- **M3** Protocol v1: versioning, registration, message ids, acknowledgements,
  and error handling.
