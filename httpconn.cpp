#include "httpconn.h"

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

int http_conn::m_user_cnt = 0;
int http_conn::m_epollfd = -1;

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, string user, string passwd,
                     string dbname) {
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_cnt++;

    doc_root = root;
    m_TRIGMode = TRIGMode;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_dbname, dbname.c_str());

    init();
}

void http_conn::init() {
    m_mysql = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_cnt--;
    }
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;
    while ((m_check_state == CONTENT && lineStatus == LINE_OK) || ((lineStatus = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_check_idx;
        printf("%s \n", text);
        switch (m_check_state) {
        case REQUESTLINE: {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case HEADER: {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
                return do_request();
        }
        case CONTENT: {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            lineStatus = LINE_OPEN;
            break;
        }
        default: return INTERNAL_ERROR;
        }
    }
    return http_conn::NO_REQUEST;
}
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_check_idx < m_read_idx; ++m_check_idx) {
        temp = m_read_buf[m_check_idx];
        if (temp == '\r') {
            if ((m_check_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_check_idx + 1] == '\n') {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_check_idx > 1 && m_read_buf[m_check_idx - 1] == '\r') {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return http_conn::LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");
    if (!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = HEADER;

    return http_conn::NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        printf("-----oop! unknow header:-----\n %s", text);
    }
    return http_conn::NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_check_idx)) {
        text[m_content_length] = '\0';
        m_header = text;
        return GET_REQUEST;
    }
    return http_conn::NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strchr(m_url, '/');

    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        char flag = m_url[1];
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        char name[100], passwd[100];
        int i;
        for (i = 5; m_header[i] != '&'; ++i)
            name[i - 5] = m_header[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = 1 + 10; m_header[i] != '\0'; ++i, ++j)
            passwd[j] == m_header[i];
        passwd[j] = '\0';

        if (m_SQLVerify == 0) {
            if (*(p + 1) == '3') {
                char *sql_insert = (char *)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
                strcat(sql_insert, "'");
                strcat(sql_insert, name);
                strcat(sql_insert, "', '");
                strcat(sql_insert, passwd);
                strcat(sql_insert, "')");

                if (users.find(name) == users.end()) {
                    m_lock.lock();
                    int res = mysql_query(m_mysql, sql_insert);
                    users.insert(pair<string, string>(name, passwd));
                    m_lock.unlock();

                    if (!res)
                        strcpy(m_url, "/log.html");
                    else
                        strcpy(m_url, "/registerError.html");
                } else
                    strcpy(m_url, "/registerError.html");
            }

            else if (*(p + 1) == '2') {
                if (users.find(name) != users.end() && users[name] == passwd)
                    strcpy(m_url, "/welcome.html");
                else
                    strcpy(m_url, "/logError.html");
            }
        } else if (m_SQLVerify == 1) {
            // 注册
            if (*(p + 1) == '3') {
                char *sql_insert = (char *)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
                strcat(sql_insert, "'");
                strcat(sql_insert, name);
                strcat(sql_insert, "', '");
                strcat(sql_insert, passwd);
                strcat(sql_insert, "')");

                if (users.find(name) == users.end()) {
                    m_lock.lock();
                    int res = mysql_query(m_mysql, sql_insert);
                    users.insert(pair<string, string>(name, passwd));
                    m_lock.unlock();

                    if (!res) {
                        strcpy(m_url, "/log.html");
                        m_lock.lock();

                        ofstream out("./id_passwd.txt", ios::app);
                        out << name << " " << passwd << endl;
                        out.close();
                        m_lock.unlock();
                    } else
                        strcpy(m_url, "/registerError.html");
                } else
                    strcpy(m_url, "/registerError.html");
            } else if (*(p + 1) == '2') {
                pid_t pid;
                int pipefd[2];
                if (pipe(pipefd) < 0) {
                    printf("pipe() error: %d", 4);
                    return BAD_REQUEST;
                }
                if (pid == fork() < 0) {
                    printf("fork() error: %d", 3);
                    return BAD_REQUEST;
                }
                if (pid == 0) {
                    dup2(pipefd[1], 1);
                    close(pipefd[0]);
                    execl(m_real_file, name, passwd, "./id_passwd.txt", "1", nullptr);
                } else {
                    close(pipefd[1]);
                    char result;
                    int ret = read(pipefd[0], &result, 1);
                    if (ret != 1) {
                        printf("管道read error: %d", ret);
                        return BAD_REQUEST;
                    }

                    printf("登录检测");

                    if (result == '1')
                        strcpy(m_url, "/welcome.html");
                    else
                        strcpy(m_url, "/logError.html");

                    waitpid(pid, nullptr, 0);
                }
            }
        } else {
            pid_t pid;
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                printf("pipe() error: %d", 4);
                return BAD_REQUEST;
            }
            if (pid == fork() < 0) {
                printf("fork() error: %d", 3);
                return BAD_REQUEST;
            }

            if (pid == 0) {
                dup2(pipefd[1], 1);
                close(pipefd[0]);
                execl(m_real_file, &flag, name, passwd, "2", sql_user, passwd, sql_dbname, nullptr);
            } else {
                close(pipefd[1]);
                char result;
                int ret = read(pipefd[0], &result, 1);

                if (ret != 1) {
                    printf("管道read error: ret = %d", ret);
                    return BAD_REQUEST;
                }
                if (flag == '2') {
                    printf("登录检测");

                    if (result == '1')
                        strcpy(m_url, "/welcome.html");
                    else
                        strcpy(m_url, "/logError.html");
                } else if (flag == '3') {
                    printf("注册检测");
                    if (result == '1')
                        strcpy(m_url, "/log.html");
                    else
                        strcpy(m_url, "/registerError.html");
                }
                waitpid(pid, nullptr, 0);
            }
        }

        if (*(p + 1) == '0') {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/register.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
            free(m_url_real);
        } else if (*(p + 1) == '1') {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/log.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
            free(m_url_real);
        } else
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

        if (stat(m_real_file, &m_file_stat) < 0)
            return NO_RESOURCE;

        if (!(m_file_stat.st_mode & S_IROTH))
            return FORBIDDEN_REQUEST;
        if (S_ISDIR(m_file_stat.st_mode))
            return BAD_REQUEST;

        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char *)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        return FILE_REQUEST;
    }
    return http_conn::NO_REQUEST;
}
bool http_conn::process_write(http_conn::HTTP_CODE ret) {
    switch (ret) {
    case INTERNAL_ERROR: {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST: {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST: {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_cnt = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        } else {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default: return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_cnt = 1;
    bytes_to_send = m_write_idx;
    return true;
}
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_length) {
    return add_content_length(content_length) && add_linger() && add_blank_line();
}
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    printf("request: %s", m_write_buf);
    return true;
}
bool http_conn::add_content_length(int content_length) {
    return add_response("Content-Length: %d\r\n", content_length);
}
bool http_conn::add_linger() {
    return add_response("Connection :%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
void http_conn::initmysql_result(connection_pool *connpool) {
    MYSQL *mysql = nullptr;
    connectionRAII mysqlconn(&mysql, connpool);
    if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
        printf("SELECT error: %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    // LT 读数据
    if (m_TRIGMode == 0) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
            return false;

        return true;
    }
    // ET 读数据
    else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if (bytes_read == 0)
                return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write() {
    int tmp = 0;

    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (true) {
        tmp = writev(m_sockfd, m_iv, m_iv_cnt);

        if (tmp < 0) {
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += tmp;
        bytes_to_send -= tmp;
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}