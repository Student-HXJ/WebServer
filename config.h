//
// Created by hxj on 7/15/20.
//

#ifndef WEBSERVER2_CONFIG_H
#define WEBSERVER2_CONFIG_H
#include <cstdlib>
#include <iostream>
#include <unistd.h>
using namespace std;

class config {
public:
    config();
    ~config() = default;
    ;

    void parse_arg(int argc, char **argv);

    int port;
    int trigmode;
    int opt_linger;
    int sql_num;
    int thread_num;
    int actor_model;
};

#endif  // WEBSERVER2_CONFIG_H
