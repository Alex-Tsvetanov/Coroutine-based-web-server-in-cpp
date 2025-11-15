# Build Instructions

## Requirements
- macOS 11+
- clang++ 15+
- CMake 3.20+

## Build
```bash
mkdir build
cd build
cmake ..
make -j
```

## Run
```bash
./server
```

Server will listen on port 8080.

## Test
```bash
# Simple test
curl http://localhost:8080/

# Load test with wrk
wrk -t4 -c100 -d30s http://localhost:8080/

# Load test with bombardier
bombardier -c 200 -n 1000000 http://localhost:8080/
```
