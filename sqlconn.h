
#ifndef SQLCONN_H
#define SQLCONN_H

#include "locker.h"
#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <string>
using namespace std;

class connection_pool {
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();

    static connection_pool *GetInstance();

    void init(string url, string User, string Passwd, string DBName, int Port, int MaxConn);

private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;
    int m_CurConn;
    int m_FreeConn;
    locker lock;
    std::list<MYSQL *> connList;
    sem m_sem;

public:
    string m_url;
    string m_port;
    string m_user;
    string m_passwd;
    string m_dbname;
};

class connectionRAII {
public:
    connectionRAII(MYSQL **conn, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif
