#include "WebServer.h"

WebServer::WebServer()
{
    users = new HttpConnection[maxFd]; //HttpConnection类对象

    char serverPath[200]; //资源文件夹路径
    getcwd(serverPath, 200); //获取目录
    char _root[6] = "/root";
    root = (char *)malloc(strlen(serverPath) + strlen(_root) + 1);
    strcpy(root, serverPath);
    strcat(root, _root);

    userData = new ClientData[maxFd]; //定时器
}

WebServer::~WebServer()
{
    close(epollFd);
    close(listenFd);
    close(pipeFd[1]);
    close(pipeFd[0]);
    delete[] users;
    delete[] userData;
    delete pool;
}

void WebServer::init(int _port , string _user, string _passWord, string _dataBaseName,
              int _optLinger, int _mode, int _sqlNum, int _threadNum, int _model)
{
    port = _port;
    user = _user;
    passWord = _passWord;
    dataBaseName = _dataBaseName;
    sqlNum = _sqlNum;
    threadNum = _threadNum;
    optLinger = _optLinger;
    mode = _mode;
    model = _model;
}

void WebServer::Mode() //监听与接收分离，可以分别设置两个描述符的触发模式
{
    if (0 == mode) //LT + LT
    {
        listenMode = 0;
        connMode = 0;
    }
    else if (1 == mode) //LT + ET
    {
        listenMode = 0;
        connMode = 1;
    }
    else if (2 == mode) //ET + LT
    {
        listenMode = 1;
        connMode = 0;
    }
    else if (3 == mode) //ET + ET
    {
        listenMode = 1;
        connMode = 1;
    }
}

void WebServer::LogWrite()
{
    Log::getInstance()->init("./ServerLog", 2000, 800000, 800); //初始化日志
}

void WebServer::SqlPool()
{
    connPool = ConnectionPool::GetInstance(); 
    connPool->init("localhost", user, passWord, dataBaseName, 3306, sqlNum); //初始化数据库连接池
    users->initMysql(connPool);//初始化数据库读取表
}

void WebServer::ThreadPool()
{
    pool = new threadPool<HttpConnection>(model, connPool, threadNum); //创建线程池
}

void WebServer::EventListen()
{
    //网络连接步骤
    listenFd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenFd >= 0);

    if (0 == optLinger) //优雅关闭连接
    {
        struct linger tmp = {0, 1};
        setsockopt(listenFd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == optLinger)
    {
        struct linger tmp = {1, 1};
        setsockopt(listenFd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); //避免TIME_WAIT状态
    ret = bind(listenFd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenFd, 5);
    assert(ret >= 0);

    utils.init(timeSlot);

    epoll_event events[maxEvenNum]; //epoll创建内核事件表
    epollFd = epoll_create(5);
    assert(epollFd != -1);

    utils.addFd(epollFd, listenFd, false, listenMode);
    HttpConnection::epollFd = epollFd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipeFd);
    assert(ret != -1);
    utils.setNonblocking(pipeFd[1]);
    utils.addFd(epollFd, pipeFd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(timeSlot); //定时信号

    Utils::pipeFd = pipeFd;
    Utils::epollFd = epollFd;
}

void WebServer::SetTimer(int connfd, struct sockaddr_in clientAddr)
{
    users[connfd].init(connfd, clientAddr, root, connMode, user, passWord, dataBaseName); //初始化目标http连接类

    //初始化ClientData数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    userData[connfd].address = clientAddr;
    userData[connfd].sockFd = connfd;
    Timer *_timer = new Timer; //临时创建一个定时器指针
    _timer->clientData = &userData[connfd]; //定时器指针指向连接的用户
    _timer->func = func; //设置回调函数
    time_t cur = time(NULL); //获取当前时间
    _timer->expire = cur + 3 * timeSlot; //设置到期时间
    userData[connfd].timer = _timer; //将定时器绑定到用户数据类*/
    utils.timeHeap.push(_timer); //将定时器推入时间堆
}

void WebServer::AdjustTimer(Timer *_timer) //若有数据传输，则将定时器设置标志位为近期用过
{
    _timer->flag = 1;
    LOG_INFO("%s", "adjust timer once");
}

void WebServer::DealTimer(Timer *_timer, int _sockFd) //移除定时器
{
    _timer->func(&userData[_sockFd]);
    if (_timer)
    {
        utils.timeHeap.delTimer(_timer);
    }
    
    LOG_INFO("close fd %d", userData[_sockFd].sockFd);
}

bool WebServer::DealClinetData() //处理新到的客户连接
{
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLength = sizeof(clientAddr);
    if (0 == listenMode) //LT
    {
        int connfd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrLength); //接收
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (HttpConnection::userCount >= maxFd)
        {
            utils.showError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        SetTimer(connfd, clientAddr); //设置定时器
    }

    else //ET
    {
        while (1) //一次性处理完
        {
            int connfd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrLength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HttpConnection::userCount >= maxFd)
            {
                utils.showError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            SetTimer(connfd, clientAddr);
        }
        return false;
    }
    return true;
}

bool WebServer::DealSignal(bool &_timeOut, bool &_stopServer) //处理信号
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(pipeFd[0], signals, sizeof(signals), 0); //接收来自信号处理器内的消息
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                _timeOut = true;
                break;
            }
            case SIGTERM:
            {
                _stopServer = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::DealRead(int _sockFd)
{
    Timer *timer = userData[_sockFd].timer;

    if (1 == model) //reactor
    {
        pool->append(users + _sockFd, 0); //若监测到读事件，将该事件放入请求队列

        while (true)
        {
            if (1 == users[_sockFd].improv)
            {
                if (1 == users[_sockFd].timerFlag)
                {
                    DealTimer(timer, _sockFd);
                    users[_sockFd].timerFlag = 0;
                }
                users[_sockFd].improv = 0;
                break;
            }
        }
    }
    else
    {
        if (users[_sockFd].read_once()) //proactor
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[_sockFd].getAddress()->sin_addr));

            pool->append_p(users + _sockFd); //若监测到读事件，将该事件放入请求队列

            if (timer) //连接为活动状态就调整定时器到期时间
            {
                AdjustTimer(timer);
            }
        }
        else
        {
            DealTimer(timer, _sockFd);
        }
    }
}

void WebServer::DealWrite(int _sockFd)
{
    Timer *timer = userData[_sockFd].timer;
    if (1 == model) //reactor
    {
        if (timer)
        {
            AdjustTimer(timer);
        }

        pool->append(users + _sockFd, 1);

        while (true)
        {
            if (1 == users[_sockFd].improv)
            {
                if (1 == users[_sockFd].timerFlag)
                {
                    DealTimer(timer, _sockFd);
                    users[_sockFd].timerFlag = 0;
                }
                users[_sockFd].improv = 0;
                break;
            }
        }
    }
    else
    {
        if (users[_sockFd].write()) //proactor
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[_sockFd].getAddress()->sin_addr));

            if (timer)
            {
                AdjustTimer(timer);
            }
        }
        else
        {
            DealTimer(timer, _sockFd);
        }
    }
}

void WebServer::EventLoop()
{
    bool _timeOut = false;
    bool _stopServer = false;

    while (!_stopServer)
    {
        int number = epoll_wait(epollFd, events, maxEvenNum, -1); //对路复用监听描述符
        if (number < 0 && errno != EINTR) //排除信号产生的中断错误
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for (int i = 0; i < number; i++) //遍历监听列表
        {
            int sockfd = events[i].data.fd;

            if (sockfd == listenFd) //处理新到的客户连接
            {
                bool flag = DealClinetData();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) //处理对端挂断
            {
                Timer *timer = userData[sockfd].timer; 
                DealTimer(timer, sockfd); //移除对应的定时器
            }
            else if ((sockfd == pipeFd[0]) && (events[i].events & EPOLLIN)) //监听管道，处理信号
            {
                bool flag = DealSignal(_timeOut, _stopServer);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            else if (events[i].events & EPOLLIN) //处理客户连接上接收到的数据
            {
                DealRead(sockfd);
            }
            else if (events[i].events & EPOLLOUT) //写数据
            {
                DealWrite(sockfd);
            }
        }
        if (_timeOut) //处理超时
        {
            utils.timerHandler();
            LOG_INFO("%s", "timer tick");
            _timeOut = false;
        }
    }
}