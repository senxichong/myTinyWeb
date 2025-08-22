#include "TcpClient.hpp"
#include <iostream>

int main() {
  try {
    TcpClient client("127.0.0.1", 8080);
    std::cout << "connecting to the server" << std::endl;

    std::string msg;

    while (std::cout << "You", std::getline(std::cin, msg)) {
      /* code */
      client.sendData(msg + '\n');

      std::string resp = client.receiveData();
      if (resp.empty()) {
        std::cout << "server connection closed!" << std::endl;
        break;
      }
      std::cout << "[DEBUG] Sent: " << msg << std::endl;
      std::cout << "[DEBUG] Received: " << resp << std::endl;
      std::cout << "server response" << resp << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}