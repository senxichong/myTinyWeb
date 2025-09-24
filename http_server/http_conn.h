#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <map>
class HttpConn {
public:
    HttpConn() {}
    ~HttpConn() {}
    void init(int sockfd, const sockaddr_in& addr);
    void close_conn();
    bool read();
    bool write();
    void process();
    static int m_epollfd;
    static HttpConn* get_conn(int fd);
private:
    enum METHOD { GET = 0, POST, UNSUPPORTED };
    enum CHECK_STATE { CHECK_REQUESTLINE = 0, CHECK_HEADER, CHECK_CONTENT };
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    char* get_line() { return m_read_buf + m_start_line; }
    HTTP_CODE do_request();
    void init();
private:
    int m_sockfd;
    sockaddr_in m_address;
    static const int READ_BUFFER_SIZE = 2048;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    static const int WRITE_BUFFER_SIZE = 1024;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    std::string m_url;
    std::string m_version;
    std::map<std::string, std::string> m_headers;
    std::string m_body;
    int m_content_length;
    static std::map<int, HttpConn*> m_users;
};
#endif