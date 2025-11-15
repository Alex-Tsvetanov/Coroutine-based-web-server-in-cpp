# Build Instructions

## Requirements

- macOS (tested on macOS with Apple Clang 15+)
- CMake 3.20+
- C++20 compiler with coroutine support

## Building

```bash
cd /Users/Alex.Tsvetanov/GitHub/CoroutinesInWebServers
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

## Running

```bash
./server [port]
# Default port is 8080
```

Example:
```bash
./server 8080
```

## Testing

### Simple curl test:
```bash
curl http://localhost:8080/test
```

### Load test with Apache Bench:
```bash
ab -n 10000 -c 100 http://localhost:8080/
```

### Load test with wrk:
```bash
wrk -t4 -c100 -d30s http://localhost:8080/
```

## Architecture

This implementation uses:
- **3 coroutines per connection**: read_task, logic_task, write_task
- **User-space scheduler**: N worker threads with a global ready queue
- **Bounded channels**: For inter-coroutine communication (no kernel blocking)
- **Kqueue backend**: Single thread handling I/O readiness (edge-triggered)
- **Non-blocking I/O**: All sockets are non-blocking
- **Safe coroutine lifecycle**: Proper cleanup on errors/EOF

## Stopping the Server

Press Ctrl+C to gracefully shutdown the server.
