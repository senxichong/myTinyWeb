#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class TcpClient {
public:
  TcpClient(const std::string &server_ip, const int &server_port) {
    // socket
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd == -1) {
      throw std::runtime_error("create fail" + std::string(strerror(errno)));
    }
    // conncet
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) == -1) {
      close(cfd);
      throw std::runtime_error("ip fail" + std::string(strerror(errno)));
    }
    int ret = connect(cfd, (sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1) {
      close(cfd);
      throw std::runtime_error("connect fail" + std::string(strerror(errno)));
    }
  }
  ssize_t sendData(const std::string &msg) {
    return send(cfd, msg.c_str(), sizeof(msg), 0);
  }

  std::string receiveData(ssize_t maxSize = 1024) {
    char buf[1024];

    ssize_t ret =
        recv(cfd, buf, std::min<ssize_t>(maxSize, sizeof(buf) - 1), 0);
    if (ret > 0) {
      buf[ret] = '\0';
      return std::string(buf);
    }
    return "";
  }
  int getFD() const { return cfd; }
  ~TcpClient() {
    if (cfd == -1) {
      close(cfd);
    }
  }

private:
  int cfd = -1;
};