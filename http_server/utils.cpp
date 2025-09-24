#include "utils.h"
#include <cstring>
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
LINE_STATUS line_status(char* buffer, int& read_idx, int& checked_idx, int& start_line) {
    for (; checked_idx < read_idx; ++checked_idx) {
        char c = buffer[checked_idx];
        if (c == '') {
            if ((checked_idx + 1) == read_idx) return LINE_OPEN;
            else if (buffer[checked_idx + 1] == '\n') {
                buffer[checked_idx++] = '\0';
                buffer[checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (c == '\n') {
            if (checked_idx > 1 && buffer[checked_idx - 1] == '\r') {
                buffer[checked_idx - 1] = '\0';
                buffer[checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}