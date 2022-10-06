#include "Config.h"

Config::Config()
{
    port = 2022;
    mode = 0; //触发组合模式,默认listenfd LT + connfd LT
    listenMode = 0; //listenfd触发模式，默认LT
    connMode = 0; //connfd触发模式，默认LT
    optLinger = 0; //优雅关闭链接，默认不使用
    sqlNum = 8; //数据库连接池数量,默认8
    threadNum = 8; //线程池内的线程数量,默认8
    model = 0; //并发模型,默认是proactor
}

void Config::parseArg(int argc, char*argv[])
{
    int opt;
    const char *str = "p:m:o:s:t:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
            case 'p':
            {
                port = atoi(optarg);
                break;
            }
            case 'm':
            {
                mode = atoi(optarg);
                break;
            }
            case 'o':
            {
                optLinger = atoi(optarg);
                break;
            }
            case 's':
            {
                sqlNum = atoi(optarg);
                break;
            }
            case 't':
            {
                threadNum = atoi(optarg);
                break;
            }
            case 'a':
            {
                model = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }
}