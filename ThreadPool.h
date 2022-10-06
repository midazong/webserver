#ifndef threadPool_H
#define threadPool_H

#include <exception>
#include "Locker.h"
#include "SqlConnectionPool.h"

template <class T>
class threadPool
{
public:
    /*_threadNum是线程池中线程的数量，_maxRequests是请求队列中最多允许的、等待处理的请求的数量*/
    threadPool(int _model, ConnectionPool *_connPool, int _threadNum = 8, int _maxRequest = 10000);
    ~threadPool();
    bool append(T *request, int _state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int threadNum;            //线程池中的线程数
    int maxRequest;           //请求队列中允许的最大请求数
    pthread_t *threads;       //描述线程池的数组，其大小为threadNum
    list<T*> workQueue;       //请求队列
    Locker mutex;             //保护请求队列的互斥锁
    Sem queueState;           //是否有任务需要处理
    ConnectionPool *connPool; //数据库
    int model;                //模型切换
};
template <class T>
threadPool<T>::threadPool(int _model, ConnectionPool *_connPool, int _threadNum,
                          int _maxRequests) : model(_model), threadNum(_threadNum), maxRequest(_maxRequests), threads(NULL), connPool(_connPool)
{
    if (_threadNum <= 0 || _maxRequests <= 0)
        throw std::exception();
    threads = new pthread_t[threadNum];
    if (!threads)
        throw std::exception();
    for (int i = 0; i < _threadNum; ++i) //创建线程供使用
    {
        if (pthread_create(threads + i, NULL, worker, this) != 0)
        {
            delete[] threads;
            throw std::exception();
        }
        if (pthread_detach(threads[i]))
        {
            delete[] threads;
            throw std::exception();
        }
    }
}
template <class T>
threadPool<T>::~threadPool()
{
    delete[] threads;
}
template <class T>
bool threadPool<T>::append(T *request, int _state) //添加事件到工作队列
{
    mutex.lock();
    if (workQueue.size() >= maxRequest)
    {
        mutex.unlock();
        return false;
    }
    request->state = _state;
    workQueue.push_back(request);
    mutex.unlock();
    queueState.post();
    return true;
}
template <class T>
bool threadPool<T>::append_p(T *request)
{
    mutex.lock();
    if (workQueue.size() >= maxRequest)
    {
        mutex.unlock();
        return false;
    }
    workQueue.push_back(request);
    mutex.unlock();
    queueState.post();
    return true;
}
template <class T>
void *threadPool<T>::worker(void *arg) //工作线程
{
    threadPool *pool = (threadPool *)arg;
    pool->run();
    return pool;
}
template <class T>
void threadPool<T>::run() 
{
    while (true)
    {
        queueState.wait();
        mutex.lock();
        if (workQueue.empty())
        {
            mutex.unlock();
            continue;
        }
        T *request = workQueue.front();
        workQueue.pop_front();
        mutex.unlock();
        if (!request)
            continue;
        if (1 == model)
        {
            if (0 == request->state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timerFlag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timerFlag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, connPool);
            request->process();
        }
    }
}
#endif