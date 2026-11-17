# AES67 / Dante SAP Discovery Daemon

![Build Status](https://github.com/mikcik91/aes67-daemon/actions/workflows/build.yml/badge.svg)

A high-performance, thread-safe C++20 microservice that listens for SAP (Session Announcement Protocol) packets on Multicast UDP to discover AES67/Dante audio streams on a local network. It automatically evicts stale devices and exposes a real-time REST API for monitoring.

**Why this matters:** mapping AES67/Dante devices in large corporate L3 networks usually requires paid vendor tools (Dante Controller, Q-SYS Designer). This daemon provides a free, lightweight, programmable topology map — ideal for CI/CD provisioning, monitoring stacks (Prometheus/Grafana via the JSON API) and automated AV system orchestration.

## Architecture

```mermaid
flowchart LR
    subgraph Network
        A[AES67/Dante Devices] -- Multicast UDP --> B(SapListener)
    end
    B -- Parse SDP --> C[(DeviceRegistry)]
    C -- Thread-safe Eviction --> D((Evict Timer))
    E[HTTP Client] -- GET /api/devices --> F(REST Server)
    F -- Read Lock --> C
```

## License

MIT — see [LICENSE](LICENSE).

## Tech Stack
* **Language:** Modern C++20 (`std::shared_mutex`, `std::optional`, structured bindings)
* **Networking:** Boost.Asio (Asynchronous UDP Multicast)
* **API:** `cpp-httplib` (HTTP Server) and `nlohmann/json` (Serialization)
* **Design:** Multi-threaded architecture with optimized reader-writer locks (Single Writer, Multiple Readers).

## Project structure

```
aes67-daemon/
├── src/                  # daemon sources (header-only components + main.cpp)
│   ├── main.cpp          # entry point: wires listener, registry, HTTP server
│   ├── SdpParser.hpp     # SAP/SDP payload parser (pure, unit-testable)
│   ├── DeviceRegistry.hpp# thread-safe device table with TTL eviction
│   ├── Logger.hpp        # timestamped logging
│   └── httplib.h         # vendored cpp-httplib (single header)
├── tests/                # GoogleTest unit tests
├── .github/workflows/    # CI (build + tests)
├── CMakeLists.txt
├── vcpkg.json            # dependency manifest
└── mock_sender.py        # helper: sends fake SAP announces for smoke tests
```

Runtime logs and ad-hoc scratch files live in `logs/` and `tmp/` — both are
git-ignored and never part of the repository.

## Build Instructions

Dependencies are managed with **vcpkg** (manifest mode). Install vcpkg and point
CMake at its toolchain file:

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$PWD/vcpkg

cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
./build/aes67_daemon
```

Alternatively, with system packages (no vcpkg):

```bash
sudo apt-get install cmake g++ libboost-system-dev nlohmann-json3-dev
cmake -B build -S . && cmake --build build
./build/aes67_daemon
```