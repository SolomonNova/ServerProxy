# Structure of an HTTP Request

An HTTP request is a message sent by a client to a server. It is composed of four main sections:

---

### 1. Request Line
Identifies **what** is being requested. It includes the method (e.g., `GET`, `POST`), the path (URL), and the HTTP version. Each line in the request (including this one) must end with a **CRLF** (`\r\n`).

### 2. Headers
Provides **metadata** about the request (e.g., Host, User-Agent). These follow the request line and also end with **CRLF**.

### 3. Empty Line
A mandatory blank line (a standalone **CRLF**) that acts as a **separator**, signaling the end of the headers and the beginning of the body.

### 4. Body (Optional)
Contains the **actual data** being sent. 
* **Trailers:** In specific cases (like "Chunked Transfer Encoding"), headers can be sent *after* the body. These are called **Trailers** and allow the sender to include metadata that wasn't known when the request started.



---

## Example Request
The following shows the exact structure, including the invisible line endings:

```text
POST /login HTTP/1.1\r\n           <-- Request Line
Host: [www.example.com](https://www.example.com)\r\n          <-- Header
Content-Type: application/json\r\n <-- Header
\r\n                               <-- Empty Line (CRLF)
{"user": "alice", "pass": "123"}   <-- Body