#include "sqlconn.h"

connection_pool *connection_pool::GetInstance() {
    static connection_pool connpool;
    return &connpool;
}

void connection_pool::init(string url, string user, string passwd, string dbname, int port, int maxconn) {
    m_url = url;
    m_user = user;
    m_passwd = passwd;
    m_dbname = dbname;
    m_port = port;
    m_MaxConn = maxconn;
    for (int i = 0; i < maxconn; ++i) {
        MYSQL *conn = nullptr;
        conn = mysql_init(conn);

        if (conn == nullptr) {
            printf("MySQL Error!\n");
            exit(1);
        }

        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), passwd.c_str(), dbname.c_str(), port, nullptr, 0);

        if (conn == nullptr) {
            printf("MySQL Error\n");
            exit(1);
        }

        connList.push_back(conn);
        ++m_FreeConn;
    }
    m_sem = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

MYSQL *connection_pool::GetConnection() {
    MYSQL *conn = nullptr;

    if (connList.size() == 0) {
        return nullptr;
    }

    m_sem.wait();
    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return conn;
}

bool connection_pool::ReleaseConnection(MYSQL *conn) {
    if (conn == nullptr)
        return false;

    lock.lock();

    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();
    m_sem.post();
    return true;
}

void connection_pool::DestroyPool() {
    lock.lock();
    if (connList.size() > 0) {
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            mysql_close(*it);
        }

        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}
connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}
connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}
