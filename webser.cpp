//
// Created by hxj on 7/15/20.
//

#include "webser.h"

#include <utility>
webser::webser() {
    users = new http_conn[MAX_FD];

    // 资源文件夹
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

webser::~webser() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_listendpool;
}
void webser::init(int port, string user, string passwd, string dbname, int opt_linger, int trigmode, int sql_num,
                  int thread_num, int actor_model) {
    m_port = port;
    m_user = std::move(user);
    m_passwd = std::move(passwd);
    m_dbname = std::move(dbname);
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_opt_linger = opt_linger;
    m_trigmode = trigmode;
    m_actor_model = actor_model;
}
void webser::thread_pool() {
    m_listendpool = new threadpool<http_conn>(m_actor_model, m_connpool, m_thread_num, 10000);
}
void webser::sql_pool() {

    // 初始化数据库连接池
    m_connpool = connection_pool::GetInstance();
    m_connpool->init("localhost", m_user, m_passwd, m_dbname, 3306, m_sql_num);

    // 初始化数据库读取表
    users->initmysql_result(m_connpool);
}
void webser::trig_mode() {

    // LT + LT
    if (m_trigmode == 0) {
        m_listenTrigmode = 0;
        m_connTrigmode = 0;
    }

    // LT + ET
    if (m_trigmode == 1) {
        m_listenTrigmode = 0;
        m_connTrigmode = 1;
    }

    // ET + LT
    if (m_trigmode == 2) {
        m_listenTrigmode = 1;
        m_connTrigmode = 0;
    }

    // ET + ET
    if (m_trigmode == 3) {
        m_listenTrigmode = 1;
        m_connTrigmode = 1;
    }
}
void webser::eventListen() {
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭
    if (m_opt_linger == 0) {
        struct linger tmp = { 0, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (m_opt_linger == 1) {
        struct linger tmp = { 1, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll 创建内核时间表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_listenTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}
void webser::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            // 处理新到的客户连接
            if (sockfd == m_listenfd) {
                bool flag = dealclientdata();
                if (!flag)
                    continue;
            }
            // 服务器端关闭连接，移除对应的定时器
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                if (!flag) {
                    printf("dealclientdata failure\n");
                }
            }
            // 处理客户连接上接受到的数据
            else if (events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            } else if (events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
        if (timeout) {
            utils.timer_handler();
            timeout = false;
        }
    }
}
void webser::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_connTrigmode, m_user, m_passwd, m_dbname);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}
void webser::adjust_timer(util_timer *timer) {
    // 若有数据传输，则将定时器往后延迟3个单位
    // 并对新的定时器在链表上的位置进行调整
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    printf("adjust timer once\n");
}
void webser::deal_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    utils.m_timer_lst.del_timer(timer);
    printf("close fd %d", users_timer[sockfd].sockfd);
}
bool webser::dealclientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (m_listenTrigmode == 0) {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0) {
            printf("accept error: errno is %d", errno);
            return false;
        }
        if (http_conn::m_user_cnt >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            printf("Internal server busy\n");
            return false;
        }
        timer(connfd, client_address);
    } else {
        while (true) {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0) {
                printf("accept error: errno is %d", errno);
                break;
            }
            if (http_conn::m_user_cnt >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                printf("Internal server busy\n");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}
bool webser::dealwithsignal(bool &timeout, bool &stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];

    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0)
        return false;
    else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
            case SIGALRM: {
                timeout = true;
                break;
            }
            case SIGTERM: {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}
void webser::dealwithread(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if (m_actor_model == 1) {
        if (timer)
            adjust_timer(timer);
        // 若监测到读事件，将该事件放入请求队列
        m_listendpool->append(users + sockfd, 0);

        while (true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        // proactor
        if (users[sockfd].read_once()) {
            printf("deal with the client(%s)\n", inet_ntoa(users[sockfd].get_address()->sin_addr));
            m_listendpool->append(users + sockfd);

            if (timer)
                adjust_timer(timer);
        } else
            deal_timer(timer, sockfd);
    }
}
void webser::dealwithwrite(int sockfd) {

    util_timer *timer = users_timer[sockfd].timer;
    // reator
    if (m_actor_model == 1) {
        if (timer)
            adjust_timer(timer);

        m_listendpool->append(users + sockfd, 1);
        while (true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        // proactor
        if (users[sockfd].write()) {
            printf("send data to the client(%s)\n", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
                adjust_timer(timer);
        } else
            deal_timer(timer, sockfd);
    }
}
