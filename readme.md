# HTTP-Server

A simple HTTP server implemented in C. It can handle multiple clients without using fork() for each request.
It forks() a certain number of workers each with their own `epoll` instance and they respond to the requests.
For parsing, it parses the `recv()` buffer in-place and no memomry allocation is used.

### Prerequisites

* **OS:** Linux (`for POSIX system calls`)
* **Compiler:** `gcc` or `clang`
* **Tools:** `CMake` `make`

### Compilation

Clone the repository and build the project using the provided Makefile:

```bash
git clone [https://github.com/SolomonNova/HTTP-Server.git](https://github.com/SolomonNova/HTTP-Server.git)
mkdir build
cd build
cmake ..
make
./server

