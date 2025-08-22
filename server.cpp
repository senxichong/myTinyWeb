#include "TcpServer.hpp"
#include <iostream>
#include <thread>
int main() {
  try {
    TcpServer server("127.0.0.1", 8080);
    std::cout << "listening port 8080" << std::endl;

    // while (1) {
    //   std::string client_ip;
    //   int client_port;
    //   int client_fd = server.acceptclient(&client_ip, &client_port);
    //   std::cout << "new client :" << client_ip << "port: " << client_port
    //             << std::endl;
    //   std::thread([client_fd]() {
    //     char buf[1024];
    //     while (1) {
    //       ssize_t size = read(client_fd, buf, sizeof(buf) - 1);
    //       if (size > 0) {
    //         buf[size] = '\0';
    //         std::cout << "read " << size << "bytes" << std::endl;

    //         write(client_fd, buf, size);
    //       }
    //       if (size == 0)
    //         break;
    //     }

    //     close(client_fd);
    //   }).detach();
    // }

    std::cout << "Server started. Press Ctrl+C to stop." << std::endl;
    server.run();
    std::cout << "Server stopped." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "error" << e.what() << std::endl;
    return 1;
  }

  return 0;
}