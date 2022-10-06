#include <fstream>
#include "HttpConnection.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

Locker mutex;
map<string, string> users;

void HttpConnection::initMysql(ConnectionPool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int numFields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setNonBlock(int fd)
{
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollFd, int fd, bool oneShot, int Mode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == Mode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (oneShot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);
    setNonBlock(fd);
}

//从内核时间表删除描述符
void removefd(int epollFd, int fd)
{
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollFd, int fd, int ev, int Mode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == Mode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConnection::userCount = 0;
int HttpConnection::epollFd = -1;

//关闭连接，关闭一个连接，客户总量减一
void HttpConnection::closeConn(bool closeFlag)
{
    if (sockFd != -1)
    {
        printf("close %d\n", sockFd);
        removefd(epollFd, sockFd);
        sockFd = -1;
        userCount--;
    }
}

//初始化连接,外部调用初始化套接字地址
void HttpConnection::init(int _sockFd, const sockaddr_in &_addr, char *_root,
              int _mode, string _sqlUser, string _sqlPasswd, string _sqlName)
{
    sockFd = _sockFd;
    address = _addr;

    addfd(epollFd, _sockFd, true, _mode); 
    userCount++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错
    //或者访问的文件中内容完全为空
    root = _root;
    mode = _mode;

    strcpy(sqlUser, _sqlUser.c_str());
    strcpy(sqlPasswd, _sqlPasswd.c_str());
    strcpy(sqlName, _sqlName.c_str());

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void HttpConnection::init()
{
    mysql = NULL;
    bytesToSend = 0;
    bytesHaveSend = 0;
    checkState = CHECK_STATE_REQUESTLINE;
    linger = false;
    method = GET;
    url = 0;
    version = 0;
    contentLength = 0;
    host = 0;
    startLine = 0;
    checkedIdx = 0;
    readIdx = 0;
    writeIdx = 0;
    cgi = 0;
    state = 0;
    timerFlag = 0;
    improv = 0;

    memset(readBuf, '\0', readBufSize);
    memset(writeBuf, '\0', writeBufSize);
    memset(realFile, '\0', fileNameLength);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
HttpConnection::LINE_STATUS HttpConnection::parseLine()
{
    char tmp;
    for (; checkedIdx < readIdx; ++checkedIdx)
    {
        tmp = readBuf[checkedIdx];
        if (tmp == '\r')
        {
            if ((checkedIdx + 1) == readIdx)
                return LINE_OPEN;
            else if (readBuf[checkedIdx + 1] == '\n')
            {
                readBuf[checkedIdx++] = '\0';
                readBuf[checkedIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (checkedIdx > 1 && readBuf[checkedIdx - 1] == '\r')
            {
                readBuf[checkedIdx - 1] = '\0';
                readBuf[checkedIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


bool HttpConnection::read_once() //一次性非阻塞读完，直到没有数据或者对方关闭连接
{
    if (readIdx >= readBufSize)
    {
        return false;
    }
    int bytes_read = 0;

    if (0 == mode) //LT读取数据
    {
        bytes_read = recv(sockFd, readBuf + readIdx, readBufSize - readIdx, 0);
        readIdx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    else //ET读数据
    {
        while (true)
        {
            bytes_read = recv(sockFd, readBuf + readIdx, readBufSize - readIdx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            readIdx += bytes_read;
        }
        return true;
    }
}

HttpConnection::HTTP_CODE HttpConnection::parseRequestLine(char *text) //解析http请求行，获得请求方法，目标url及http版本号
{
    url = strpbrk(text, " \t");
    if (!url)
    {
        return BAD_REQUEST;
    }
    *url++ = '\0';
    char *_method = text;
    if (strcasecmp(_method, "GET") == 0)
        method = GET;
    else if (strcasecmp(_method, "POST") == 0)
    {
        method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    url += strspn(url, " \t");
    version = strpbrk(url, " \t");
    if (!version)
        return BAD_REQUEST;
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(url, "http://", 7) == 0)
    {
        url += 7;
        url = strchr(url, '/');
    }

    if (strncasecmp(url, "https://", 8) == 0)
    {
        url += 8;
        url = strchr(url, '/');
    }

    if (!url || url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(url) == 1)
        strcat(url, "judge.html");
    checkState = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HttpConnection::HTTP_CODE HttpConnection::parseHeaders(char *text) //解析http请求的一个头部信息
{
    if (text[0] == '\0')
    {
        if (contentLength != 0)
        {
            checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        contentLength = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
HttpConnection::HTTP_CODE HttpConnection::parseContent(char *text)
{
    if (readIdx >= (contentLength + checkedIdx))
    {
        text[contentLength] = '\0';
        //POST请求中最后为输入的用户名和密码
        requestString = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConnection::HTTP_CODE HttpConnection::processRead()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((checkState == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parseLine()) == LINE_OK))
    {
        text = getLine();
        startLine = checkedIdx;
        LOG_INFO("%s", text);
        switch (checkState)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parseRequestLine(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parseHeaders(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return dealRequest();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parseContent(text);
            if (ret == GET_REQUEST)
                return dealRequest();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

HttpConnection::HTTP_CODE HttpConnection::dealRequest()
{
    strcpy(realFile, root);
    int len = strlen(root);
    //printf("url:%s\n", url);
    const char *p = strrchr(url, '/');

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = url[1];

        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, url + 2);
        strncpy(realFile + len, url_real, fileNameLength - len - 1);
        free(url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; requestString[i] != '&'; ++i)
            name[i - 5] = requestString[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; requestString[i] != '\0'; ++i, ++j)
            password[j] = requestString[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                mutex.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                mutex.unlock();

                if (!res)
                    strcpy(url, "/log.html");
                else
                    strcpy(url, "/registerError.html");
            }
            else
                strcpy(url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(url, "/welcome.html");
            else
                strcpy(url, "/logError.html");
        }
    }

    if (*(p + 1) == '0')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        strncpy(realFile + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/log.html");
        strncpy(realFile + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/picture.html");
        strncpy(realFile + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/video.html");
        strncpy(realFile + len, url_real, strlen(url_real));

        free(url_real);
    }
    else
        strncpy(realFile + len, url, fileNameLength - len - 1);

    if (stat(realFile, &fileStat) < 0)
        return NO_RESOURCE;

    if (!(fileStat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(fileStat.st_mode))
        return BAD_REQUEST;

    int fd = open(realFile, O_RDONLY);
    fileAddress = (char *)mmap(0, fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void HttpConnection::unmap()
{
    if (fileAddress)
    {
        munmap(fileAddress, fileStat.st_size);
        fileAddress = 0;
    }
}
bool HttpConnection::write()
{
    int temp = 0;

    if (bytesToSend == 0)
    {
        modfd(epollFd, sockFd, EPOLLIN, mode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(sockFd, iv, ivCount);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(epollFd, sockFd, EPOLLOUT, mode);
                return true;
            }
            unmap();
            return false;
        }

        bytesHaveSend += temp;
        bytesToSend -= temp;
        if (bytesHaveSend >= iv[0].iov_len)
        {
            iv[0].iov_len = 0;
            iv[1].iov_base = fileAddress + (bytesHaveSend - writeIdx);
            iv[1].iov_len = bytesToSend;
        }
        else
        {
            iv[0].iov_base = writeBuf + bytesHaveSend;
            iv[0].iov_len = iv[0].iov_len - bytesHaveSend;
        }

        if (bytesToSend <= 0)
        {
            unmap();
            modfd(epollFd, sockFd, EPOLLIN, mode);

            if (linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool HttpConnection::addResponse(const char *format, ...)
{
    if (writeIdx >= writeBufSize)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(writeBuf + writeIdx, writeBufSize - 1 - writeIdx, format, arg_list);
    if (len >= (writeBufSize - 1 - writeIdx))
    {
        va_end(arg_list);
        return false;
    }
    writeIdx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", writeBuf);

    return true;
}
bool HttpConnection::addStatusLine(int status, const char *title)
{
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HttpConnection::addHeaders(int content_len)
{
    return addContentLength(content_len) && addLinger() &&
           addBlankLine();
}
bool HttpConnection::addContentLength(int content_len)
{
    return addResponse("Content-Length:%d\r\n", content_len);
}
bool HttpConnection::addContentType()
{
    return addResponse("Content-Type:%s\r\n", "text/html");
}
bool HttpConnection::addLinger()
{
    return addResponse("Connection:%s\r\n", (linger == true) ? "keep-alive" : "close");
}
bool HttpConnection::addBlankLine()
{
    return addResponse("%s", "\r\n");
}
bool HttpConnection::addContent(const char *content)
{
    return addResponse("%s", content);
}
bool HttpConnection::processWrite(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (!addContent(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST:
        {
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (!addContent(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addContent(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST:
        {
            addStatusLine(200, ok_200_title);
            if (fileStat.st_size != 0)
            {
                addHeaders(fileStat.st_size);
                iv[0].iov_base = writeBuf;
                iv[0].iov_len = writeIdx;
                iv[1].iov_base = fileAddress;
                iv[1].iov_len = fileStat.st_size;
                ivCount = 2;
                bytesToSend = writeIdx + fileStat.st_size;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                addHeaders(strlen(ok_string));
                if (!addContent(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    iv[0].iov_base = writeBuf;
    iv[0].iov_len = writeIdx;
    ivCount = 1;
    bytesToSend = writeIdx;
    return true;
}
void HttpConnection::process()
{
    HTTP_CODE read_ret = processRead();
    if (read_ret == NO_REQUEST)
    {
        modfd(epollFd, sockFd, EPOLLIN, mode);
        return;
    }
    bool write_ret = processWrite(read_ret);
    if (!write_ret)
    {
        closeConn();
    }
    modfd(epollFd, sockFd, EPOLLOUT, mode);
}
