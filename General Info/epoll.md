# Epoll System Calls Reference

`epoll` is the high-performance I/O event notification facility in Linux. It allows a process to monitor multiple file descriptors (FDs) to see if I/O is possible on any of them.

## 1. epoll_create1()

### Purpose
Creates an `epoll` instance (a kernel object) and returns a file descriptor that represents it. This FD serves as the handle for your event manager.

### Prototype
int epoll_create1(int flags);

### Common Flags
* **EPOLL_CLOEXEC**: Automatically closes the epoll file descriptor when `exec()` is called.

### Example
int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd == -1) perror("epoll_create1");

### What happens internally?
1. The kernel allocates an internal **event table**.
2. It prepares to store all monitored file descriptors inside this table.
3. It returns a handle (FD) pointing to that table.

## 2. epoll_ctl()

### Purpose
Used to add, modify, or remove file descriptors from the epoll instance. This is the mechanism used to register sockets, pipes, timers, etc., for monitoring.

### Prototype
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

### Operations (op)
* **EPOLL_CTL_ADD**: Add a new FD to the interest list.
* **EPOLL_CTL_MOD**: Change the settings of an existing FD.
* **EPOLL_CTL_DEL**: Remove an FD from the interest list.

### Event Structure
struct epoll_event {
    uint32_t events;   // Bitmask of events to monitor
    epoll_data_t data; // User data (usually the fd, a pointer, or an id)
};

### Common Event Flags
* **EPOLLIN**: Data available to read.
* **EPOLLOUT**: Ready to write.
* **EPOLLERR**: Error condition.
* **EPOLLHUP**: Hang up.
* **EPOLLRDHUP**: Stream socket peer closed connection.
* **EPOLLET**: **Edge-Triggered** mode (notifies only on state changes).
* **EPOLLONESHOT**: Monitor once; the FD must be re-armed to trigger again.

### Example (Registering a socket)
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET; // Read + Edge-Triggered
ev.data.fd = client_fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);

### What happens internally?
1. The kernel links `client_fd` into the epoll instance's watch list.
2. The kernel actively starts tracking the readiness state of that FD using callbacks.
3. No active polling happens yet; this just sets up the monitoring.

## 3. epoll_wait()

### Purpose
Waits until one or more registered file descriptors become ready. This is the blocking call where your server sits until there is work to do.

### Prototype
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

### Parameters
* **events**: An output array allocated by the user, which the kernel fills with ready events.
* **maxevents**: The size of the `events` array.
* **timeout**:
    * `-1`: Block forever (until an event occurs).
    * `0`: Non-blocking poll (return immediately).
    * `>0`: Timeout in milliseconds.

### Return Value
* `> 0`: Number of ready events.
* `0`: Timeout occurred (no events ready).
* `-1`: Error.

### Example
struct epoll_event events[64];
int n = epoll_wait(epfd, events, 64, -1);
for (int i = 0; i < n; i++) {
    int fd = events[i].data.fd;
    if (events[i].events & EPOLLIN) {
        handle_read(fd);
    }
}

### What happens internally?
1. The kernel puts the calling thread to sleep (if timeout allows).
2. When a monitored FD changes state (e.g., data arrives), the kernel wakes the thread.
3. The kernel copies **only the ready events** into the user's `events[]` array.
4. The user iterates only over active FDs; there is no need to scan the entire list of connections.

## 4. close()

### Purpose
Destroys the epoll instance when you are finished with it.

### Usage
close(epfd);

### What happens internally?
1. The kernel removes all monitored FDs from the interest list.
2. The kernel frees the memory associated with the epoll instance.

## Deprecated (Do Not Use)
* `int epoll_create(int size);` (Old API, size is ignored. Use `epoll_create1`.)

## Workflow Summary
1. **Initialize**: `epoll_create1()`
2. **Register**: `epoll_ctl(EPOLL_CTL_ADD, listen_sock)`
3. **Event Loop**:
    * Call `epoll_wait()`
    * Iterate through returned events
    * Handle data or new connections
4. **Cleanup**: `close(epfd)`