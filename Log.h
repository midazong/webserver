#ifndef LOG_H
#define LOG_H

#include <string>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "BlockQueue.h"
#include "LockFreeQueue.h"

class Log
{
public:
    static Log *getInstance() //懒汉模式，使用的时候才初始化对象
    {
        static Log instance;
        return &instance;
    }
    static void* dealLogThread(void *args)
    {
        Log::getInstance()->asyncWriteLog();
        return 0;
    }
    bool init(const char* _fileName, int maxQueueSize, int _bufSize = 8192, int _maxLines = 100000);

    void writeLog(int type, const char* format, ...);

    void flush();

private:
    Log(); //私有化构造函数，防止被别人初始化
    ~Log();
    void *asyncWriteLog();

    char dirName[128]; //日志路径名
    char fileName[128]; //日志文件名
    int maxLines; //一个日志文件存储最大行数
    long long count;
    int bufSize; //日志缓冲区大小
    int today; //当天时间
    char* buf;
    FILE *fp; //日志文件指针
    BlockQueue<string> *logQueue; //消息队列
    //LockFreeQueue<string> *logQueue; //无锁队列
    Locker mutex; //互斥锁
};

#define LOG_DEBUG(format, ...) \
{Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#define LOG_INFO(format, ...) \
{Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#define LOG_WARN(format, ...) \
{Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#define LOG_ERROR(format, ...) \
{Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#endif