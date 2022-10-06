#include "Config.h"

int main(int argc, char *argv[])
{
    //数据库信息,登录名,密码,库名
    string user = "root";
    string passWord = "root";
    string dataBaseName = "yourdb";

    //命令行解析
    Config config;
    config.parseArg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.port, user, passWord, dataBaseName, config.optLinger, 
    config.mode,  config.sqlNum,  config.threadNum, config.model);

    //日志
    server.LogWrite();

    //数据库
    server.SqlPool();

    //线程池
    server.ThreadPool();

    //触发模式，边缘触发或水平触发
    server.Mode();

    //开启监听
    server.EventListen();

    //事件循环
    server.EventLoop();

    return 0;
}