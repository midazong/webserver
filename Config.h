#ifndef CONFIG_H
#define CONFIG_H

#include "WebServer.h"

class Config
{
public:
    Config();
    ~Config(){};
    void parseArg(int argc, char*argv[]);

    int port; //端口号
    int mode; //触发组合模式
    int listenMode; //listenfd触发模式
    int connMode; //connfd触发模式
    int optLinger; //优雅关闭链接
    int sqlNum; //数据库连接池数量
    int threadNum; //线程池内的线程数量
    int model; //并发模型选择
};

#endif