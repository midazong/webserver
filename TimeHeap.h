#ifndef TIMEHEAP_H
#define TIMEHEAP_H

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <time.h>
#include <queue>
using namespace std;

//定时器类声明
class Timer;

//客户端数据
struct ClientData
{
    sockaddr_in address;
    int sockFd;
    Timer* timer;
};

//定时器
class Timer
{
public:
    Timer(){ flag = 0; }

    time_t expire; //到期时间
    ClientData* clientData; //客户端数据
    int flag; //定时器标记 0表示未被使用，1表示近期用过， -1表示已销毁
    void (* func)(ClientData*); //定时器回调函数
};

struct cmp  //定时器比较仿函数，用于构造小根堆
{
    bool operator()(Timer* a, Timer* b)
    {
        if (a != nullptr && b != nullptr)
            return (a -> expire) > (b -> expire);
        return false;
    }
};
//时间堆
class TimeHeap
{
public:
    void pop(); //弹出堆顶定时器
    void push(Timer* timer); //推入一个定时器到时间堆
    bool empty(); //时间堆是否为空
    Timer* top(); //获取堆顶定时器
    void delTimer(Timer* timer); //销毁目标定时器
    void tick(int _timeSlot, time_t &_expire); //定时任务处理函数

private:
    priority_queue<Timer*, vector<Timer*>, cmp> timeheap; //时间堆
};

class Utils //配置信号，时间堆定时任务由信号触发
{
public:
    Utils() {}
    ~Utils() {}

    void init(int _timeSlot); //默认间隔
    int setNonblocking(int _fd); //对文件描述符设置非阻塞
    void addFd(int _epollFd, int _fd, bool oneShot, int _mode); //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    static void sig_handler(int sig); //信号处理函数
    void addsig(int sig, void(handler)(int), bool restart = true); //设置信号函数
    void timerHandler(); //定时处理任务，重新定时以不断触发SIGALRM信号
    void showError(int _connFd, const char *info);

public:
    static int *pipeFd;
    TimeHeap timeHeap;
    static int epollFd;
    int timeSlot; //时间帧
    time_t expire;
};

void func(ClientData *_clientData);

#endif