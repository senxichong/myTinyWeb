#ifndef UTILS_H
#define UTILS_H
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
int setnonblocking(int fd);
void addfd(int epollfd, int fd, bool one_shot);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);
enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
LINE_STATUS line_status(char* buffer, int& read_idx, int& checked_idx, int& start_line);
#endif