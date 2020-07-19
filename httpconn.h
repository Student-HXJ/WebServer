#ifndef HTTPCONN_H
#define HTTPCONN_H

#include "locker.h"
#include "sqlconn.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mysql/mysql.h>
#include <string>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <wait.h>

using std::endl;
using std::ios;
using std::map;
using std::ofstream;
using std::pair;
using std::string;
class http_conn {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD { GET, POST };
    enum CHECK_STATE { REQUESTLINE, HEADER, CONTENT };
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        INTERNAL_ERROR,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
    };
    enum LINE_STATUS { LINE_OK, LINE_BAD, LINE_OPEN };

public:
    http_conn() = default;
    ~http_conn() = default;

    void init(int sockfd, const sockaddr_in &addr, char *, int, string user, string passwd, string dbname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();

    sockaddr_in *get_address() {
        return &m_address;
    }

private:
    void init();
    HTTP_CODE process_read();
    LINE_STATUS parse_line();
    char *get_line() {
        return m_read_buf + m_start_line;
    }

    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();

    bool process_write(HTTP_CODE ret);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content(const char *content);
    bool add_response(const char *format, ...);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    void unmap();

public:
    static int m_epollfd;
    static int m_user_cnt;
    MYSQL *m_mysql{};
    int m_state{};
    int timer_flag{};
    int improv{};

    void initmysql_result(connection_pool *connpool);

private:
    int m_sockfd{};
    sockaddr_in m_address{};
    char *doc_root{};
    int m_TRIGMode{};

    char sql_user[100]{};
    char sql_passwd[100]{};
    char sql_dbname[100]{};

    int bytes_to_send{};
    int bytes_have_send{};

    CHECK_STATE m_check_state;
    struct stat m_file_stat {};
    struct iovec m_iv[2]{};
    int m_iv_cnt{};

    int m_read_idx{};
    int m_check_idx{};
    int m_start_line{};
    int m_write_idx{};
    char m_read_buf[READ_BUFFER_SIZE]{};
    char m_write_buf[WRITE_BUFFER_SIZE]{};
    char m_real_file[FILENAME_LEN]{};
    char *m_url{};
    char *m_version{};
    char *m_header{};
    char *m_file_address{};
    char *m_host{};
    bool m_linger{};
    int m_content_length{};
    METHOD m_method;
    int cgi{};
};

#endif
