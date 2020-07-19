//
// Created by hxj on 7/15/20.
//

#ifndef WEBSERVER2_THREADPOOL_H
#define WEBSERVER2_THREADPOOL_H
#include "locker.h"
#include "sqlconn.h"
#include <list>
#include <pthread.h>

template <typename T>
class threadpool {
public:
    threadpool(int actor_model, connection_pool *connpool, int thread_num = 8, int max_reques = 10000);
    ~threadpool();

    bool append(T *request, int state);
    bool append(T *request);

private:
    static void *worker(void *arg);
    [[noreturn]] void run();

private:
    int m_thread_num;
    int m_max_requests;
    pthread_t *m_threads;
    int m_actor_model;
    connection_pool *m_connpool;
    sem m_queuestat;  // 信号量判断是否有任务需要处理
    locker m_queuelocker;
    std::list<T *> m_workqueue;
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connpool, int thread_num, int max_requests)
    : m_actor_model(actor_model)
    , m_thread_num(thread_num)
    , m_max_requests(max_requests)
    , m_connpool(connpool)
    , m_threads(nullptr) {
    if (thread_num <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_num];

    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_num; ++i) {
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    auto *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
[[noreturn]] void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();
        m_queuelocker.lock();

        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        if (m_actor_model == 1) {
            if (request->m_state == 0) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->m_mysql, m_connpool);
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write())
                    request->improv = 1;
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlcon(&request->m_mysql, m_connpool);
            request->process();
        }
    }
}

template <typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append(T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() < m_max_requests) {
        m_workqueue.push_back(request);
        m_queuelocker.unlock();
        m_queuestat.post();
        return true;
    } else {
        m_queuelocker.unlock();
        return false;
    }
}

#endif  // WEBSERVER2_THREADPOOL_H
