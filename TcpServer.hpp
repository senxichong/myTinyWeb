#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

class TcpServer {
public:
  TcpServer(const std::string &ip, int port, int backlog = SOMAXCONN)
      : server_fd(-1), stop_flag(false) {
    setupHandler();

    // 1. 创建 socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
      throw std::runtime_error("server sock " + std::string(strerror(errno)));
    // 2. 设置地址
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
      close(server_fd);
      throw std::runtime_error("invaild ip" + ip);
    }
    // 3. bind
    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
      close(server_fd);
      throw std::runtime_error("bild fail" + std::string(strerror(errno)));
    }
    // 4. listen
    if (listen(server_fd, SOMAXCONN) == -1) {
      close(server_fd);
      throw std::runtime_error("listen error" + std::string(strerror(errno)));
    }
  }
  // 接受一个客户端连接，返回 client 的 socket fd
  int acceptclient(std::string *ip_ptr = nullptr, int *port_ptr = nullptr) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int cfd = accept(server_fd, (sockaddr *)&client_addr, &len);
    if (cfd == -1) {

      throw std::runtime_error("accepy fail" + std::string(strerror(errno)));
    }
    // 可选：获取客户端 IP 和端口
    if (ip_ptr) {
      char buf[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, buf, sizeof(buf));
      *ip_ptr = buf;
    }
    if (port_ptr) {
      *port_ptr = ntohs(client_addr.sin_port);
    }
    return cfd;
  }

  // 获取服务端 socket fd
  int getFD() const { return server_fd; }

  // 析构函数：关闭 socket
  ~TcpServer() {
    if (server_fd != -1) {
      close(server_fd);
    }
  }

  // 不允许拷贝
  void run(std::string *ip_ptr = nullptr, int *port_ptr = nullptr) {
    while (!stop_flag) {
      /* code */

      sockaddr_in client_addr{};
      socklen_t len = sizeof(client_addr);
      int cfd = accept(server_fd, (sockaddr *)&client_addr, &len);
      if (cfd == -1) {

        throw std::runtime_error("accepy fail" + std::string(strerror(errno)));
      }

      pid_t pid = fork();

      if (pid == 0) {
        close(server_fd);
        handleClient(cfd);
        exit(0);
      } else if (pid > 0) {
        // listen as usual
        close(cfd);

      } else {
        close(server_fd);
        throw std::runtime_error("fork fail: " + std::string(strerror(errno)));
      }
    }
  }

private:
  int server_fd = -1;
  bool stop_flag;

  static TcpServer *instance;
  static void sighandleint(int) {
    if (instance)
      instance->stop_flag = 1;
  }

  static void sighandlechild(int) {

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
      /* code */
    }
  }
  void setupHandler() {
    instance = this;
    struct sigaction sig_chid {};
    sig_chid.sa_flags = 0;
    sigemptyset(&sig_chid.sa_mask);
    sig_chid.sa_handler = sighandlechild;
    if (sigaction(SIGCHLD, &sig_chid, nullptr) == -1) {
      throw std::runtime_error("sigaction SIGCHILD failed");
    }

    struct sigaction sig_int {};
    sig_int.sa_flags = 0;
    sigemptyset(&sig_int.sa_mask);
    sig_int.sa_handler = sighandleint;

    if (sigaction(SIGINT, &sig_int, nullptr) == -1) {
      throw std::runtime_error("sigaction SIGINT failed");
    }
  }

  void handleClient(int client_fd) {
    char buf[BUFSIZ];

    ssize_t bytes;

    while ((bytes = read(client_fd, buf, sizeof(buf) - 1)) > 0) {
      /* code */
      buf[bytes] = '\0';
      std::cout << "client says: " << buf << std::endl;
      write(STDOUT_FILENO, buf, bytes);
      send(client_fd, buf, bytes, 0);
    }
    close(client_fd);
    std::cout << "client close" << std::endl;
  }
};
TcpServer *TcpServer::instance = nullptr;