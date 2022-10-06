#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "Locker.h"
#include "SqlConnectionPool.h"
#include "TimeHeap.h"
#include "Log.h"

class HttpConnection
{
public:
    static const int fileNameLength = 200;
    static const int readBufSize = 2048;
    static const int writeBufSize = 1024;
    enum METHOD //浏览器请求类型码
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE //状态码记录状态机状态，用于跳转操作
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE //返回请求结果码
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS //解析行结果码
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpConnection() {}
    ~HttpConnection() {}

public:
    void init(int _sockfd, const sockaddr_in &_addr, char *_root,
              int _mode, string _sqlUser, string _passwd, string _sqlname);
    void closeConn(bool closeFlag = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *getAddress()
    {
        return &address;
    }
    void initMysql(ConnectionPool *connPool);
    int timerFlag;
    int improv;

private:
    void init();
    HTTP_CODE processRead();
    bool processWrite(HTTP_CODE ret);
    HTTP_CODE parseRequestLine(char *text);
    HTTP_CODE parseHeaders(char *text);
    HTTP_CODE parseContent(char *text);
    HTTP_CODE dealRequest();
    char *getLine() { return readBuf + startLine; };
    LINE_STATUS parseLine();
    void unmap();
    bool addResponse(const char *format, ...);
    bool addContent(const char *content);
    bool addStatusLine(int status, const char *title);
    bool addHeaders(int contentLength);
    bool addContentType();
    bool addContentLength(int contentLength);
    bool addLinger();
    bool addBlankLine();

public:
    static int epollFd;
    static int userCount;
    MYSQL *mysql;
    int state; //读为0, 写为1

private:
    int sockFd;
    sockaddr_in address;
    char readBuf[readBufSize];
    int readIdx;
    int checkedIdx;
    int startLine;
    char writeBuf[writeBufSize];
    int writeIdx;
    CHECK_STATE checkState;
    METHOD method;
    char realFile[fileNameLength];
    char *url;
    char *version;
    char *host;
    int contentLength;
    bool linger;
    char *fileAddress;
    struct stat fileStat;
    struct iovec iv[2];
    int ivCount;
    int cgi;             //是否启用的POST
    char *requestString; //存储请求头数据
    int bytesToSend;
    int bytesHaveSend;
    char *root;

    map<string, string> users;
    int mode;
    char sqlUser[100];
    char sqlPasswd[100];
    char sqlName[100];
};

#endif
