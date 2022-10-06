#include <time.h>
#include "Log.h"

Log::Log()
{
    count = 0;
}
Log::~Log()
{
    if (fp != NULL)
        fclose(fp);
}

bool Log::init(const char *_fileName, int maxQueueSize, int _bufSize, int _maxLines)
{
    //logQueue = new LockFreeQueue<string>(maxQueueSize); //初始化消息队列
    logQueue = new BlockQueue<string>(maxQueueSize);
    pthread_t tid;
    pthread_create(&tid, NULL, dealLogThread, NULL); //创建日志处理线程

    bufSize = _bufSize;
    buf = new char[bufSize];
    memset(buf, 0, bufSize);
    maxLines = _maxLines;

    time_t t = time(NULL); //获取当前时间
    struct tm *lt = localtime(&t); //转化为本地时间
    struct tm mlt = *lt; //用类类型而不是指针调用

    const char *p = strrchr(_fileName, '/');
    char fullName[512] = {0};

    if (p == NULL)
    {
        snprintf(fullName, 512, "%d_%02d_%02d_%s", mlt.tm_year + 1900,
                 mlt.tm_mon + 1, mlt.tm_mday, _fileName);
    }
    else //解析文件路径名
    {
        strcpy(fileName, p + 1);
        strncpy(dirName, _fileName, p - _fileName + 1);
        snprintf(fullName, 512, "%s%d_%02d_%02d_%s", dirName, mlt.tm_year + 1900,
                 mlt.tm_mon + 1, mlt.tm_mday, fileName);
    }

    today = mlt.tm_mday;
    fp = fopen(fullName, "a"); //根据时间打开日志文件
    if (fp == NULL)
    {
        return false;
    }
    return true;
}
void Log::writeLog(int type, const char *format, ...)
{
    struct timeval now = {0, 0}; //当前时间
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *lt = localtime(&t);
    struct tm mlt = *lt;
    char s[16] = {0};

    switch (type)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    mutex.lock();
    count++;

    //日期不对或者行数超过最大限制就创建新日志文件
    if (today != mlt.tm_mday || count % maxLines == 0) // everyday log
    {

        char newName[512] = {0};
        fflush(fp);
        fclose(fp); //关闭旧文件
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", mlt.tm_year + 1900, mlt.tm_mon + 1, mlt.tm_mday);

        //如果时间不对就变更时间
        if (today != mlt.tm_mday)
        {
            snprintf(newName, 512, "%s%s%s", dirName, tail, fileName);
            today = mlt.tm_mday;
            count = 0;
        }
        else //否则加后缀
        {
            snprintf(newName, 512, "%s%s%s.%lld", dirName, tail, fileName, count / maxLines);
        }
        fp = fopen(newName, "a");
    }
    mutex.unlock();

    va_list valst;
    va_start(valst, format); //初始化参数列表
    string log;

    mutex.lock();
    //格式化时间
    int n = snprintf(buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     mlt.tm_year + 1900, mlt.tm_mon + 1, mlt.tm_mday,
                     mlt.tm_hour, mlt.tm_min, mlt.tm_sec, now.tv_usec, s);
    //格式化打印参数到缓冲区
    int m = vsnprintf(buf + n, bufSize - 1, format, valst);
    buf[n + m] = '\n';
    buf[n + m + 1] = '\0';
    log = buf;
    
    mutex.unlock();

    logQueue->push(log);
    //logQueue->push(log);
    
    va_end(valst); //释放参数容器
}
void*Log::asyncWriteLog()
{
    string log;
    while (1) //从消息队列取出一条日志写入文件
    {                          //如果为空会在pop内部等待条件变量
        if (!logQueue->pop(log))
            sleep(3);

        mutex.lock();
        fputs(log.c_str(), fp);
        mutex.unlock();
    }
    return 0;
}
void Log::flush()
{
    mutex.lock();
    fflush(fp); //刷新缓冲区
    mutex.unlock();
}