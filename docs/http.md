# Nyota OS HTTP Subsystem (`httpget` & `httpd`)

**Target Release**: Nyota OS v1.3.0  
**Architecture**: x86_64 Long Mode (SMP-aware)  
**Status**: Production / Tested  

---

## 1. Overview

Nyota OS v1.3.0 introduces native userland HTTP/1.1 utilities:
* **`httpget`**: A resilient HTTP/1.1 client supporting URL parsing, DNS/hosts resolution, multi-packet streaming reads, and header parsing.
* **`httpd`**: A secure, minimal HTTP/1.1 web server daemon supporting static file delivery, MIME typing, and strict directory traversal defenses.

---

## 2. HTTP Client (`httpget`)

Located in [user/httpget/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/httpget/main.c):

### 2.1 Design & Capabilities
* **URL Parsing**: Decomposes URLs into scheme (`http://`), hostname, optional custom port (`:8080`), and path.
* **Name Resolution**:
  - Checks `/etc/hosts` for local overrides.
  - Falls back to DNS queries to the resolver configured in `/etc/resolv.conf`.
* **Request Construction**: Sends standards-compliant HTTP/1.1 request:
  ```http
  GET /index.html HTTP/1.1\r\n
  Host: example.com\r\n
  User-Agent: NyotaOS-httpget/1.3.0\r\n
  Connection: close\r\n
  Accept: */*\r\n
  \r\n
  ```
* **Streaming Multi-Packet Handling**: Does not assume the entire HTTP response arrives in a single TCP read buffer. Iteratively accumulates chunks until connection close or end delimiter (`</html>` / EOF).
* **Header & Status Parsing**: Extracts the numeric HTTP response code (`200`, `404`, etc.) and separates headers from the response body.

### 2.2 CLI Usage
```bash
# Fetch from a local or remote web server
$ httpget http://10.0.2.2:8080/

# Fetch a specific file
$ httpget http://127.0.0.1/index.html
```

---

## 3. HTTP Server Daemon (`httpd`)

Located in [user/httpd/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/httpd/main.c):

### 3.1 Server Architecture
* **Listening Socket**: Binds to `0.0.0.0` on port 80 (or custom port via `httpd -p <port>`).
* **Connection Loop**: Accepts incoming TCP client connections, parses the request line, serves the matching document, and closes the socket.
* **Document Root**: Restricted to `/var/www` by default.

### 3.2 Directory Traversal Protection
To prevent arbitrary file disclosure (such as requests for `/../../etc/passwd` or `/..`):
1. **Path Substring Validation**: Any request containing `..` is immediately blocked.
2. **Normalized Prefix Enforce**: The resolved file path is verified to strictly begin with the configured document root `/var/www`.
3. **Response**: Traversal attempts receive `HTTP/1.1 403 Forbidden` with a safety violation notice.

```c
/* Traversal defense excerpt from user/httpd/main.c */
if (strstr(req_path, "..") != NULL) {
    send_response(client_fd, 403, "Forbidden", "text/plain", 
                  "403 Forbidden: Directory traversal denied.\n");
    return;
}
```

### 3.3 Supported MIME Types
Based on file extension:
* `.html`, `.htm` $\to$ `text/html`
* `.txt`, `.c`, `.h` $\to$ `text/plain`
* `.css` $\to$ `text/css`
* `.png` $\to$ `image/png`
* `.jpg`, `.jpeg` $\to$ `image/jpeg`
* Default fallback $\to$ `application/octet-stream`

### 3.4 Supported Response Codes
* `200 OK`: File exists and was transmitted successfully.
* `400 Bad Request`: Malformed HTTP request line.
* `403 Forbidden`: Directory traversal attempt or file read permission error.
* `404 Not Found`: File does not exist under document root.
* `405 Method Not Allowed`: HTTP methods other than `GET`.

### 3.5 CLI Usage
```bash
# Start HTTP daemon on port 80
$ httpd -p 80 -d /var/www

# In background or daemon test:
# Run httpd & and test with httpget or host browser
```

---

## 4. QEMU Host Integration

When running Nyota OS in QEMU with host port forwarding:
```bash
-netdev user,id=net0,hostfwd=tcp::8080-:80
```
A browser or `curl` on the host operating system can interact directly with Nyota OS:
```bash
# On host machine:
$ curl http://localhost:8080/
<html><body><h1>Welcome to Nyota OS v1.3.0</h1>...</body></html>
```
