// server.cpp
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;

/******************************
 * ThreadPool (simple)
 ******************************/
class ThreadPool {
public:
    ThreadPool(size_t n): stop(false) {
        for (size_t i=0;i<n;i++){
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool(){
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &t: workers) if (t.joinable()) t.join();
    }
    void enqueue(function<void()> f) {
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.push(move(f));
        }
        condition.notify_one();
    }
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
};

/******************************
 * Utilities
 ******************************/
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}

string url_decode(const string &s) {
    string out; out.reserve(s.size());
    for (size_t i=0;i<s.size();++i){
        if (s[i]=='%'){
            if (i+2 < s.size()){
                string hex = s.substr(i+1,2);
                char ch = (char) strtol(hex.c_str(), nullptr, 16);
                out.push_back(ch);
                i+=2;
            }
        } else if (s[i]=='+') out.push_back(' ');
        else out.push_back(s[i]);
    }
    return out;
}

/******************************
 * Basic HTTP parser (state machine)
 * Supports: Request-Line, headers, Content-Length body (POST)
 ******************************/
struct HttpRequest {
    string method;
    string path;
    string version;
    unordered_map<string,string> headers;
    string body;
};

enum ParseState { REQUEST_LINE, HEADERS, BODY, DONE, ERROR };

class HttpParser {
public:
    HttpParser(): state(REQUEST_LINE), content_length(0) {}

    // feed data chunk, returns true if request fully parsed
    bool feed(const string &data) {
        buffer += data;
        while (true) {
            if (state == REQUEST_LINE) {
                size_t pos = buffer.find("\r\n");
                if (pos == string::npos) return false;
                string line = buffer.substr(0, pos);
                buffer.erase(0, pos+2);
                if (!parse_request_line(line)) { state = ERROR; return false; }
                state = HEADERS;
            } else if (state == HEADERS) {
                size_t pos = buffer.find("\r\n");
                if (pos == string::npos) return false;
                if (pos == 0) {
                    // empty line -> headers done
                    buffer.erase(0,2);
                    auto it = req.headers.find("Content-Length");
                    if (it != req.headers.end()) {
                        content_length = stoi(it->second);
                        if (content_length > 0) state = BODY;
                        else state = DONE;
                    } else {
                        state = DONE;
                    }
                } else {
                    string line = buffer.substr(0,pos);
                    buffer.erase(0,pos+2);
                    parse_header(line);
                }
            } else if (state == BODY) {
                if ((int)buffer.size() < content_length) return false;
                req.body = buffer.substr(0, content_length);
                buffer.erase(0, content_length);
                state = DONE;
            } else {
                break;
            }
        }
        return state == DONE;
    }

    HttpRequest get_request() { return req; }
    void reset() { state = REQUEST_LINE; content_length = 0; buffer.clear(); req = HttpRequest(); }

    bool has_error() const { return state == ERROR; }
private:
    bool parse_request_line(const string &line) {
        // METHOD PATH VERSION
        istringstream iss(line);
        if (!(iss >> req.method >> req.path >> req.version)) return false;
        // decode path
        size_t qpos = req.path.find('?');
        if (qpos != string::npos) req.path = req.path.substr(0, qpos);
        req.path = url_decode(req.path);
        return true;
    }
    void parse_header(const string &line) {
        size_t colon = line.find(':');
        if (colon == string::npos) return;
        string name = line.substr(0, colon);
        string val = line.substr(colon+1);
        // trim
        while (!val.empty() && (val.front()==' ' || val.front()=='\t')) val.erase(0,1);
        while (!name.empty() && name.back()==' ') name.pop_back();
        // normalize header name
        for (auto &c: name) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        req.headers[name] = val;
    }

    ParseState state;
    string buffer;
    HttpRequest req;
    int content_length;
};

/******************************
 * Simple HTTP response helpers
 ******************************/
string http_response(int code, const string &reason, const string &body, const string &content_type="text/html") {
    ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << reason << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Connection: keep-alive\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

string get_mime(const string &path) {
    if (path.size()>=5 && path.substr(path.size()-5)==".html") return "text/html";
    if (path.size()>=4 && path.substr(path.size()-4)==".css") return "text/css";
    if (path.size()>=3 && path.substr(path.size()-3)==".js") return "application/javascript";
    if (path.size()>=4 && path.substr(path.size()-4)==".png") return "image/png";
    if (path.size()>=4 && path.substr(path.size()-4)==".jpg") return "image/jpeg";
    return "text/plain";
}

/******************************
 * Connection structure
 ******************************/
struct Connection {
    int fd;
    HttpParser parser;
    string write_buffer;
    bool closed = false;
};

/******************************
 * Server core (epoll + reactor/proactor)
 ******************************/
class HttpServer {
public:
    HttpServer(int port, const string& doc_root, bool use_et, bool proactor, int threads)
    : port(port), doc_root(doc_root), use_et(use_et), proactor(proactor),
      pool(max(1, threads)), running(false)
    {}

    bool start() {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) { perror("socket"); return false; }

        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }
        if (listen(listen_fd, SOMAXCONN) < 0) { perror("listen"); return false; }

        if (set_nonblocking(listen_fd) < 0) { perror("set_nonblocking"); return false; }

        epfd = epoll_create1(0);
        if (epfd < 0) { perror("epoll_create1"); return false; }

        epoll_event ev{};
        ev.data.fd = listen_fd;
        ev.events = EPOLLIN;
        if (use_et) ev.events |= EPOLLET;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) { perror("epoll_ctl listen"); return false; }

        running = true;
        main_thread = thread([this]{ this->loop(); });
        return true;
    }

    void stop() {
        running = false;
        if (main_thread.joinable()) main_thread.join();
        close(epfd);
        close(listen_fd);
    }

private:
    void loop() {
        const int MAX_EVENTS = 1024;
        vector<epoll_event> events(MAX_EVENTS);

        while (running) {
            int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait"); break;
            }
            for (int i=0;i<n;i++){
                int fd = events[i].data.fd;
                uint32_t evs = events[i].events;

                if (fd == listen_fd) {
                    // accept loop (non-blocking)
                    while (true) {
                        sockaddr_in cli{};
                        socklen_t clen = sizeof(cli);
                        int connfd = accept(listen_fd, (sockaddr*)&cli, &clen);
                        if (connfd < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            perror("accept");
                            break;
                        }
                        if (set_nonblocking(connfd) < 0) { close(connfd); continue; }
                        add_connection(connfd);
                    }
                } else {
                    // client event
                    if (proactor) {
                        // Proactor: main thread does the I/O (read) and then hands parsed/partial data to worker
                        if (evs & EPOLLIN) {
                            if (!handle_read_in_main(fd, use_et)) {
                                remove_connection(fd);
                            }
                        }
                        if (evs & EPOLLOUT) {
                            // write pending data in main loop (attempt)
                            handle_write_in_main(fd, use_et);
                        }
                        if (evs & (EPOLLHUP|EPOLLERR)) {
                            remove_connection(fd);
                        }
                    } else {
                        // Reactor: main thread just notifies worker; worker performs I/O
                        // we push a task to threadpool that will perform read/write
                        auto task_fd = fd;
                        pool.enqueue([this, task_fd, evs]{
                            handle_connection_reactor(task_fd, evs);
                        });
                    }
                }
            }
        }
    }

    void add_connection(int fd) {
        Connection *c = new Connection();
        c->fd = fd;
        {
            lock_guard<mutex> lk(conn_mutex);
            conns[fd] = c;
        }
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLRDHUP;
        if (use_et) ev.events |= EPOLLET;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl add");
            close(fd);
            delete c;
            lock_guard<mutex> lk(conn_mutex);
            conns.erase(fd);
            return;
        }
    }

    void remove_connection(int fd) {
        {
            lock_guard<mutex> lk(conn_mutex);
            auto it = conns.find(fd);
            if (it != conns.end()) {
                close(it->second->fd);
                delete it->second;
                conns.erase(it);
            }
        }
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    }

    // ========== Proactor helper: main thread reads and optionally writes ==========
    bool handle_read_in_main(int fd, bool et) {
        string data;
        char buf[4096];
        while (true) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                data.append(buf, buf + n);
                if (!et) break; // LT: one read is enough
            } else if (n == 0) {
                // peer closed
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("read");
                return false;
            }
        }
        if (data.empty()) return true;

        Connection *c = nullptr;
        {
            lock_guard<mutex> lk(conn_mutex);
            auto it = conns.find(fd);
            if (it == conns.end()) return false;
            c = it->second;
        }

        // feed parser in main thread. If parsed fully, dispatch to worker for handling.
        bool done = c->parser.feed(data);
        if (c->parser.has_error()) {
            string resp = http_response(400, "Bad Request", "<h1>400 Bad Request</h1>");
            queue_write(fd, resp);
            return true;
        }
        if (done) {
            HttpRequest req = c->parser.get_request();
            c->parser.reset();
            // hand over request to worker with already-read body (proactor)
            pool.enqueue([this, fd, req]() { this->process_request_proactor(fd, req); });
        }
        return true;
    }

    void handle_write_in_main(int fd, bool et) {
        Connection *c = nullptr;
        {
            lock_guard<mutex> lk(conn_mutex);
            auto it = conns.find(fd);
            if (it == conns.end()) return;
            c = it->second;
        }
        if (c->write_buffer.empty()) {
            // remove EPOLLOUT interest
            mod_epoll_out(fd, false);
            return;
        }
        // try to write
        while (!c->write_buffer.empty()) {
            ssize_t n = ::write(fd, c->write_buffer.data(), c->write_buffer.size());
            if (n > 0) {
                c->write_buffer.erase(0, n);
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("write");
                remove_connection(fd);
                return;
            }
        }
        if (c->write_buffer.empty()) {
            mod_epoll_out(fd, false);
        } else {
            mod_epoll_out(fd, true);
        }
    }

    // Helper to queue write from any thread (thread safe)
    void queue_write(int fd, const string &data) {
        Connection *c = nullptr;
        {
            lock_guard<mutex> lk(conn_mutex);
            auto it = conns.find(fd);
            if (it == conns.end()) return;
            c = it->second;
            c->write_buffer += data;
        }
        // ensure EPOLLOUT is set
        mod_epoll_out(fd, true);
    }

    void mod_epoll_out(int fd, bool enable) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLRDHUP;
        if (enable) ev.events |= EPOLLOUT;
        if (use_et) ev.events |= EPOLLET;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }

    // ========== Reactor mode: workers do I/O ==========
    void handle_connection_reactor(int fd, uint32_t evs) {
        if (evs & (EPOLLHUP|EPOLLERR)) {
            remove_connection(fd);
            return;
        }
        if (evs & EPOLLIN) {
            // read loop in worker
            string data;
            char buf[4096];
            while (true) {
                ssize_t n = ::read(fd, buf, sizeof(buf));
                if (n > 0) {
                    data.append(buf, buf + n);
                    if (!use_et) break;
                } else if (n == 0) {
                    remove_connection(fd);
                    return;
                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    perror("read");
                    remove_connection(fd);
                    return;
                }
            }
            if (!data.empty()) {
                Connection *c = nullptr;
                {
                    lock_guard<mutex> lk(conn_mutex);
                    auto it = conns.find(fd);
                    if (it == conns.end()) return;
                    c = it->second;
                }
                bool done = c->parser.feed(data);
                if (c->parser.has_error()) {
                    string resp = http_response(400, "Bad Request", "<h1>400 Bad Request</h1>");
                    queue_write(fd, resp);
                    return;
                }
                if (done) {
                    HttpRequest req = c->parser.get_request();
                    c->parser.reset();
                    process_request_reactor(fd, req);
                }
            }
        }
        if (evs & EPOLLOUT) {
            // write in worker
            Connection *c = nullptr;
            {
                lock_guard<mutex> lk(conn_mutex);
                auto it = conns.find(fd);
                if (it == conns.end()) return;
                c = it->second;
            }
            while (!c->write_buffer.empty()) {
                ssize_t n = ::write(fd, c->write_buffer.data(), c->write_buffer.size());
                if (n > 0) {
                    c->write_buffer.erase(0, n);
                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        mod_epoll_out(fd, true);
                        break;
                    } else {
                        perror("write");
                        remove_connection(fd);
                        break;
                    }
                }
            }
            if (c && c->write_buffer.empty()) mod_epoll_out(fd, false);
        }
    }

    // ========== Request processing (two modes) ==========
    // Proactor: already parsed request is passed (IO done in main)
    void process_request_proactor(int fd, const HttpRequest &req) {
        // handle GET/POST
        if (req.method == "GET") {
            handle_get(fd, req.path);
        } else if (req.method == "POST") {
            handle_post(fd, req.path, req.body);
        } else {
            string resp = http_response(501, "Not Implemented", "<h1>501 Not Implemented</h1>");
            queue_write(fd, resp);
        }
    }

    // Reactor: worker parsed request after reading
    void process_request_reactor(int fd, const HttpRequest &req) {
        process_request_proactor(fd, req);
    }

    // handle GET: serve static files under doc_root
    void handle_get(int fd, string path) {
        if (path == "/") path = "/index.html";
        string full = doc_root + path;
        // prevent path traversal: very basic normalization
        if (full.find("..") != string::npos) {
            string resp = http_response(403, "Forbidden", "<h1>403 Forbidden</h1>");
            queue_write(fd, resp);
            return;
        }
        struct stat st{};
        if (stat(full.c_str(), &st) < 0) {
            string resp = http_response(404, "Not Found", "<h1>404 Not Found</h1>");
            queue_write(fd, resp);
            return;
        }
        if (!S_ISREG(st.st_mode)) {
            string resp = http_response(403, "Forbidden", "<h1>403 Forbidden</h1>");
            queue_write(fd, resp);
            return;
        }
        // read file
        FILE *f = fopen(full.c_str(), "rb");
        if (!f) {
            string resp = http_response(500, "Internal Server Error", "<h1>500 Error</h1>");
            queue_write(fd, resp);
            return;
        }
        string body;
        body.resize(st.st_size);
        size_t got = fread(&body[0],1,st.st_size,f);
        fclose(f);
        if ((int)got != st.st_size) {
            string resp = http_response(500, "Internal Server Error", "<h1>500 Error</h1>");
            queue_write(fd, resp);
            return;
        }
        string resp = http_response(200, "OK", body, get_mime(full));
        queue_write(fd, resp);
    }

    // handle POST: simple echo of body
    void handle_post(int fd, string path, const string &body) {
        string out = "<h1>POST Echo</h1><pre>" + body + "</pre>";
        string resp = http_response(200, "OK", out);
        queue_write(fd, resp);
    }

private:
    int port;
    string doc_root;
    bool use_et;
    bool proactor;
    int epfd;
    int listen_fd;
    ThreadPool pool;
    thread main_thread;
    mutex conn_mutex;
    unordered_map<int, Connection*> conns;
    atomic<bool> running;
    ThreadPool dummy_pool{1}; // placeholder
};

/******************************
 * main - parse args, start server
 ******************************/
int main(int argc, char** argv) {
    if (argc < 6) {
        cerr << "Usage: " << argv[0] << " <port> <doc_root> <et|lt> <reactor|proactor> <threads>\n";
        return 1;
    }
    int port = atoi(argv[1]);
    string doc_root = argv[2];
    string mode_et = argv[3];
    string model = argv[4];
    int threads = atoi(argv[5]);
    bool use_et = (mode_et == "et");
    bool proactor = (model == "proactor");

    HttpServer server(port, doc_root, use_et, proactor, threads);
    if (!server.start()) {
        cerr << "Failed to start server\n";
        return 1;
    }
    cout << "Server started on port " << port << " (ET=" << use_et << ", " << (proactor ? "Proactor" : "Reactor") << ")\n";
    // wait until user interrupts
    while (true) {
        this_thread::sleep_for(chrono::seconds(3600));
    }
    server.stop();
    return 0;
}
