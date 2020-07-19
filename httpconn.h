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
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
    };
    enum LINE_STATUS { LINE_OK, LINE_BAD, LINE_OPEN };

    static int m_epollfd;
    static int m_user_count;
    int timer_flag{};
    int improv{};
    MYSQL *m_mysql{};
    int m_state{};  //读为0, 写为1
public:
    http_conn() {}
    ~http_conn() = default;

    void init(int sockfd, const sockaddr_in &addr, char *, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address() {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() {
        return m_read_buf + m_start_line;
    };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

private:
    int m_sockfd{};
    sockaddr_in m_address{};

    char m_read_buf[READ_BUFFER_SIZE]{};
    char m_write_buf[WRITE_BUFFER_SIZE]{};
    char m_real_file[FILENAME_LEN]{};

    int m_read_idx{};
    int m_checked_idx{};
    int m_start_line{};
    int m_write_idx{};

    CHECK_STATE m_check_state;
    METHOD m_method;

    char *m_url{};
    char *m_version{};
    char *m_host{};
    char *m_file_address{};
    char *m_string{};  //存储请求头数据
    char *doc_root{};

    int m_content_length{};
    int bytes_to_send{};
    int bytes_have_send{};
    bool m_linger{};
    struct stat m_file_stat {};
    struct iovec m_iv[2]{};
    int m_iv_count{};
    int cgi{};  //是否启用的POST

    map<string, string> m_users;
    int m_TRIGMode{};

    char sql_user[100]{};
    char sql_passwd[100]{};
    char sql_name[100]{};
};

#endif