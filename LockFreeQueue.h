#ifndef LOCKFREEQUEUE_H
#define LOCKFREEQUEUE_H

#include <atomic>
#include <memory>

template <class T>
class LockFreeQueue //基于链表实现的无锁队列
{
public:
    LockFreeQueue(int _maxSize) : maxSize(_maxSize), head(new Node()), tail(head) {}
    ~LockFreeQueue()
    {
        Node *tmp;
        while (head != nullptr)
        {
            tmp = head;
            head = head->next;
            delete tmp;
        }
        tmp = nullptr;
        tail = nullptr;
    }
    bool pop(T &_data) //不断尝试出队
    {
        Node *oldHead, *oldTail, *firstNode;
        while (true)
        {
            oldHead = head; //先保存头指针再保存尾指针
            oldTail = tail; //否则可能因其他线程的操作乱序
            firstNode = oldHead->next;

            if (oldHead != head) //如果头结点被取出那么重新获取
            {
                continue;
            }

            if (oldHead == oldTail)       //头尾指针指向同一节点说明队列为空
            {                             //或者其他线程取出了头结点导致指针后移
                if (firstNode == nullptr) //队列为空
                {
                    return false;
                }
                //队列非空说明其他线程祛除了头结点，所以更新尾指针并继续循环
                __sync_bool_compare_and_swap(&tail, oldTail, firstNode);
                continue;
            }
            else
            {
                _data = firstNode->data; //取出数据
                //如果头指针不变则退出,否则继续循环
                if (__sync_bool_compare_and_swap(&head, oldHead, firstNode))
                    break;
            }
        }
        delete oldHead;
        return true;
    }
    void push(const T &_data)
    {
        Node *tmp = new Node(_data); //初始化入队节点
        Node *oldTail, *oldTailNext;

        while (true)
        {
            oldTail = tail;
            oldTailNext = oldTail -> next;

            if (oldTail != tail) //尾指针发生变化则重新开始
            {
                continue;
            }

            if (oldTailNext == nullptr) //判断尾指针指向最后一个节点
            {
                //如果节点入队成功就退出,尾节点不一致就继续循环
                if (__sync_bool_compare_and_swap(&oldTail->next, oldTailNext, tmp))
                {
                    break;
                }
            }
            else
            {
                //尾指针不指向最后一个节点就向后递推直到最后一个节点
                __sync_bool_compare_and_swap(&tail, oldTail, oldTailNext);
                continue;
            }
        }
        //设置尾指针指向尾节点，无论其他线程是否修改都会指向最后一个节点
        __sync_bool_compare_and_swap(&tail, oldTail, tmp);
    }

private:
    struct Node //链表节点
    {
        T data;     //节点值
        Node *next; //每个节点内保存指向下一节点的指针
        Node() : data(), next(nullptr) {}
        Node(T &_data) : data(_data), next(nullptr) {}
        Node(const T &_data) : data(_data), next(nullptr) {}
    };
    int maxSize;
    Node *head; //头指针
    Node *tail; //尾指针
};

#endif