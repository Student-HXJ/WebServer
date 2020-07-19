//
// Created by hxj on 7/15/20.
//

#ifndef WEBSERVER2_WEBSER_H
#define WEBSERVER2_WEBSER_H

#include "httpconn.h"
#include "sqlconn.h"
#include "threadpool.h"
#include "timer.h"
#include <arpa/inet.h>
#include <string>
#include <sys/socket.h>

using std::string;

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class webser {
public:
    webser();
    ~webser();

    void init(int port, string user, string passwd, string dbname, int opt_linger, int trigmode, int sql_num,
              int thread_num, int actor_model);
    void thread_pool();
    void sql_pool();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    int m_port;
    char *m_root;

    string m_user;
    string m_passwd;
    string m_dbname;

    int m_sql_num;
    int m_thread_num;

    int m_opt_linger;
    int m_trigmode;
    int m_actor_model;

    threadpool<http_conn> *m_listendpool;
    connection_pool *m_connpool;

    http_conn *users;
    client_data *users_timer;
    Utils utils;

    int m_epollfd;
    int m_pipefd[2];
    int m_listenfd;

    int m_listenTrigmode;
    int m_connTrigmode;

    epoll_event events[MAX_EVENT_NUMBER];
};

#endif  // WEBSERVER2_WEBSER_H
