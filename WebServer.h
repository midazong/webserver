#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "ThreadPool.h"
#include "HttpConnection.h"

const int maxFd = 65536; //最大文件描述符
const int maxEvenNum = 10000; //最大事件数
const int timeSlot = 5; //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int _port , string _user, string _passWord, string _dataBaseName,
              int _optLinger, int _mode, int _sqlNum, int _threadNum, int _model);

    void ThreadPool();
    void SqlPool();
    void LogWrite();
    void Mode();
    void EventListen();
    void EventLoop();
    void AdjustTimer(Timer *_timer);
    void SetTimer(int connfd, struct sockaddr_in clientAddr);
    void DealTimer(Timer *_timer, int _sockFd);
    bool DealClinetData();
    bool DealSignal(bool& _timeOut, bool& _stopServer);
    void DealRead(int _sockFd);
    void DealWrite(int _sockFd);

public:
    //基础
    int port;
    char *root;
    int model;

    int pipeFd[2];
    int epollFd;
    HttpConnection *users;

    //数据库相关
    ConnectionPool *connPool;
    string user; //登陆数据库用户名
    string passWord;  //登陆数据库密码
    string dataBaseName; //使用数据库名
    int sqlNum;

    //线程池相关
    threadPool<HttpConnection> *pool;
    int threadNum;

    //epoll_event相关
    epoll_event events[maxEvenNum];

    int listenFd;
    int optLinger;
    int mode;
    int listenMode;
    int connMode;

    //定时器相关
    ClientData *userData;
    Utils utils;
};
#endif
