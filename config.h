//
// Created by hxj on 7/15/20.
//

#ifndef CONFIG_H
#define CONFIG_H
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

#endif  // CONFIG_H
