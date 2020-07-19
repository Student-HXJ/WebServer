//
// Created by hxj on 7/15/20.
//

#ifndef TIMER_H
#define TIMER_H

#include "httpconn.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
class util_timer;

struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer {
public:
    util_timer() : prev(nullptr), next(nullptr) {}

public:
    time_t expire{};

    void (*cb_func)(client_data *){};
    client_data *user_data{};
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

class Utils {
public:
    Utils() = default;
    ~Utils() = default;

    void init(int timeslot);

    static int setnonblocking(int fd);
    static void addfd(int epollfd, int fd, bool one_shot, int trigmode);
    static void sig_handler(int sig);
    void addsig(int sig, void(handler)(int), bool restart = true);
    void timer_handler();
    void show_error(int connfd, const char *info);

    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_timeslot{};
};

void cb_func(client_data *user_data);

#endif  // TIMER_H
