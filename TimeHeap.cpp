#include "TimeHeap.h"
#include "HttpConnection.h"

void TimeHeap::pop()
{
    timeheap.pop();
}
void TimeHeap::push(Timer* timer)
{
    timeheap.push(timer);
}
bool TimeHeap::empty()
{
    return timeheap.empty();
}
Timer* TimeHeap::top()
{
    return timeheap.top();
}
void TimeHeap::delTimer(Timer* timer)
{
    if (!timer)
        return;
    timer->flag = -1; //置为-1表示销毁
    timer->func = nullptr; 
}
void TimeHeap::tick(int _timeSlot, time_t &_expire)
{
    if (timeheap.empty()) //无连接状态直接退出
        return;
    Timer* tmp = top(); //获取堆顶指针用于遍历时间堆
    time_t cur = time(NULL); //获取当前时间
    while (!timeheap.empty())
    {
        if (!tmp)
            return;
        
        if (tmp->flag == -1 || (tmp->expire <= cur && tmp->flag == 0)) //被置为销毁位或者到期
        {
            if (tmp->flag == 0) //如果到期就执行处理函数
                tmp->func(tmp->clientData);

            timeheap.pop(); //真正销毁定时器
            if (timeheap.empty())
                break;
            tmp = timeheap.top(); //继续检查下一个定时器
            continue;
        }

        if (tmp->expire > cur) //定时器没到期就退出, 无论近期是否使用过
        {
            _expire = tmp->expire; //将时间间隔设置成最新到期时间
            break;
        }
        timeheap.pop(); //近期使用过就延长时间，并且置标识位为0
        tmp->expire = cur + 3 * _timeSlot; 
        tmp->flag = 0;
        timeheap.push(tmp);
    }
}

void Utils::init(int _timeSlot)
{
    timeSlot = _timeSlot;
}

//对文件描述符设置非阻塞
int Utils::setNonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addFd(int _epollfd, int fd, bool oneShot, int _mode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == _mode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (oneShot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipeFd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timerHandler()
{
    timeHeap.tick(timeSlot, expire);
    if (timeHeap.empty()) //无连接状态设置默认定时并退出
    {
        alarm(timeSlot);
        return;
    }
    time_t cur = time(NULL); //获取当前时间
    alarm(expire - cur); //设置定时为到期时间，每次定时信号都会在最新到期定时器到期时间触发
}

void Utils::showError(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::pipeFd = 0;
int Utils::epollFd = 0;

class Utils;
void func(ClientData *_clientData)
{
    epoll_ctl(Utils::epollFd, EPOLL_CTL_DEL, _clientData->sockFd, 0);
    assert(_clientData);
    close(_clientData->sockFd);
    HttpConnection::userCount--;
}