<video src="https://github.com/SolomonNova/HTTP-Server/blob/main/assets/demo.mp4?raw=true" controls="controls" style="max-width: 100%;">
  Your browser does not support the video tag.
</video>

# HTTP-Server

A simple HTTP server implemented in C. It handles multiple clients efficiently without spawning a new thread or process for every single request.

### Architecture
* **Pre-forking:** It `forks()` a set number of workers, each managing its own `epoll` instance to handle concurrent requests.
* **In-place Parsing:** For maximum efficiency, it parses the `recv()` buffer directly; **no additional memory allocation** is used during parsing.

### Prerequisites

* **OS:** Linux (Requires POSIX system calls and `epoll`)
* **Compiler:** `gcc` or `clang`
* **Tools:** `CMake`, `make`

### Compilation

Clone the repository and build the project:

```bash
git clone [https://github.com/SolomonNova/HTTP-Server.git](https://github.com/SolomonNova/HTTP-Server.git)
cd HTTP-Server
mkdir build && cd build
cmake ..
make
./server
