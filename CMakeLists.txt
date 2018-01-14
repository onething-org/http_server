cmake_minimum_required(VERSION 3.9)
project(http_server)

set(CMAKE_CXX_STANDARD 11)

add_executable(http_server
        server/include/ajs_define.h
        server/src/client_auth.cpp
        server/src/client_auth.h
        server/src/http_pkt.h
        server/src/http_server.cpp
        server/src/http_server.h
        server/src/json_helper.h
        server/src/mcd_frame.cpp
        server/src/mcd_frame.h
        server/src/param_check.h
        server/src/statistic.cpp
        server/src/statistic.h
        server/include_tfc.mk)
