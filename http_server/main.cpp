#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "http_conn.h"
#include "utils.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("Usage: %s port\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    listen(listenfd, 5);
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    HttpConn::m_epollfd = epollfd;
    while (1) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) break;
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0) continue;
                HttpConn* conn = new HttpConn();
                conn->init(connfd, client_address);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                HttpConn* conn = HttpConn::get_conn(sockfd);
                if (conn) conn->close_conn();
            } else if (events[i].events & EPOLLIN) {
                HttpConn* conn = HttpConn::get_conn(sockfd);
                if (conn) {
                    if (conn->read()) conn->process();
                    else conn->close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                HttpConn* conn = HttpConn::get_conn(sockfd);
                if (conn) {
                    if (!conn->write()) conn->close_conn();
                }
            }
        }
    }
    close(listenfd);
    return 0;
}