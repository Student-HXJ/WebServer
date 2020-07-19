//
// Created by hxj on 7/15/20.
//

#include "config.h"

config::config() {
    port = 9006;

    // 默认组合模式 listenfd LT + connfd LT
    trigmode = 0;
    // 默认不适用优雅关闭连接
    opt_linger = 0;

    sql_num = 8;

    thread_num = 8;

    actor_model = 0;
}

void config::parse_arg(int argc, char **argv) {
    int opt;
    const char *str = "p:m:o:s:t:a";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
        case 'p': {
            port = atoi(optarg);
            break;
        }
        case 'm': {
            trigmode = atoi(optarg);
            break;
        }
        case 'o': {
            opt_linger = atoi(optarg);
            break;
        }
        case 's': {
            sql_num = atoi(optarg);
            break;
        }
        case 't': {
            thread_num = atoi(optarg);
            break;
        }
        case 'a': {
            actor_model = atoi(optarg);
            break;
        }
        default: {
            cout << "no action" << endl;
            break;
        }
        }
    }
}
