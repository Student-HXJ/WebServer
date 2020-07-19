#include "config.h"
#include "webser.h"
#include <string>
using std::string;
int main(int argc, char **argv) {
    string user = "root";
    string passwd = "root";
    string dbname = "hxj";

    config conf;
    conf.parse_arg(argc, argv);

    webser serv;
    serv.init(conf.port, user, passwd, dbname, conf.opt_linger, conf.trigmode, conf.sql_num, conf.thread_num,
              conf.actor_model);

    serv.sql_pool();

    serv.thread_pool();

    serv.trig_mode();

    serv.eventListen();

    serv.eventLoop();

    return 0;
}
