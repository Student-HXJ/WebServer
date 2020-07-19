cmake_minimum_required(VERSION 3.10)
project(WebServer)

set(CMAKE_CXX_STANDARD 11)
set(mysqllib /usr/lib/x86_64-linux-gnu/libmysqlclient.so)

add_executable(
        main main.cpp
        sqlconn.h sqlconn.cpp
        threadpool.h
        locker.h locker.cpp
        httpconn.h httpconn.cpp
        config.h config.cpp
        webser.h webser.cpp
        timer.h timer.cpp
)
#add_executable(main main.cpp)


find_package(Threads REQUIRED)
target_link_libraries(main Threads::Threads ${mysqllib})

