#ifndef BlockQueue_H
#define BlockQueue_H

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <queue>
#include "Locker.h"
using namespace std;

template <class T>
class BlockQueue
{
public:
    BlockQueue(int _maxSize = 1000) //设置阻塞队列最大容量
    {
        if (_maxSize <= 0)
        {
            exit(-1);
        }
        maxSize = _maxSize;
    }
    void clear() //清空阻塞队列
    {
        mutex.lock();
        while (!que.empty())
        {
            que.pop();
        }
        mutex.unlock();
    }
    bool full() //检查是否为满
    {
        mutex.lock();
        if (que.size() >= maxSize)
        {
            mutex.unlock();
            return true;
        }
        mutex.unlock();
        return false;
    }
    bool empty() //检查是否为空
    {
        mutex.lock();
        bool ret = que.empty();
        mutex.unlock();
        return ret;
    }
    bool front(T &value) //获取阻塞队列第一个元素
    {
        mutex.lock();
        if (que.empty())
        {
            mutex.unlock();
            return false;
        }
        value = que.front();
        mutex.unlock();
        return true;
    }
    bool back(T &value) //获取阻塞队列最后一个元素
    {
        mutex.lock();
        if (que.empty())
        {
            mutex.unlock();
            return false;
        }
        value = que.back();
        mutex.unlock();
        return true;
    }
    int size() //获取阻塞队列大小
    {
        mutex.lock();
        int ret = que.size();
        mutex.unlock();
        return ret;
    }
    int getMaxSize() //获取阻塞队列最大容量
    {
        return maxSize;
    }
    bool push(const T &value) //加入队列
    {
        mutex.lock();
        if (que.size() >= maxSize) //如果队列已满
        {                             //广播所有线程处理队列
            cond.broadcast();
            mutex.unlock();
            return false;
        }
        que.push(value);
        cond.broadcast();
        mutex.unlock();
        return true;
    }
    bool pop(T &value) //弹出元素
    {
        mutex.lock();
        while (que.empty()) //只要为空就等待条件变量
        {
            if (!cond.wait(mutex.get()))
            {
                mutex.unlock();
                return false;
            }
        }
        value = que.front();
        que.pop();
        mutex.unlock();
        return true;
    }
    bool pop(T &value, int timeout) //加超时处理的弹出
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        mutex.lock();
        if (que.empty())
        {
            t.tv_sec = now.tv_sec + timeout / 1000;
            t.tv_nsec = (timeout % 1000) * 1000;
            if (!cond.timewait(mutex.get(), t))
            {
                mutex.unlock();
                return false;
            }
        }
        if (que.empty())
        {
            mutex.unlock();
            return false;
        }
        value = que.front();
        que.pop();
        mutex.unlock();
        return true;
    }
    ~BlockQueue() //析构
    {
        clear();
    }

private:
    Locker mutex; //互斥锁
    Cond cond; //条件变量

    queue<T> que; //用队列构造阻塞队列
    int maxSize; //最大队列长度
};
#endif