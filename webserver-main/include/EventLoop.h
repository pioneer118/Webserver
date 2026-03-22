#pragma once // 防止头文件重复包含（替代传统的#ifndef+#define+#endif）

// 引入C++标准库头文件
#include <functional> // 函数对象（std::function，用于回调函数）
#include <vector>     // 动态数组（存储Channel列表）
#include <atomic>     // 原子变量（线程安全的布尔/整型）
#include <memory>     // 智能指针（std::unique_ptr，管理动态对象）
#include <mutex>      // 互斥锁（std::mutex，保护共享数据）

// 引入项目自定义头文件
#include "noncopyable.h"   // 禁用拷贝构造/赋值（基类，避免对象拷贝）
#include "Timestamp.h"     // 时间戳工具类（记录事件发生时间）
#include "CurrentThread.h" // 当前线程工具类（获取线程ID）
#include "TimerQueue.h"    // 定时器队列（管理定时任务）

// 前置声明：避免包含头文件，减少编译依赖（只告诉编译器“有这个类”）
class Channel; // 通道类（封装文件描述符+事件）
class Poller;  // IO复用抽象类（封装epoll/kqueue等）

// 事件循环类（核心！Reactor模式的核心）
// 主要包含两个核心模块：Channel（事件通道）、Poller（epoll的抽象）
// 一个EventLoop对应一个线程，负责监听事件、处理事件、执行回调
class EventLoop : noncopyable // 继承noncopyable，禁止拷贝EventLoop对象
{
public:
    // 定义回调函数类型：无返回值、无参数的函数对象
    // 用于存储上层注册的需要在EventLoop中执行的回调
    using Functor = std::function<void()>;

    // 构造函数：创建EventLoop对象，初始化核心成员
    EventLoop();
    // 析构函数：销毁EventLoop对象，释放资源
    ~EventLoop();

    // 开启事件循环（核心方法！进入epoll_wait的无限循环）
    void loop();
    // 退出事件循环（终止epoll_wait的无限循环）
    void quit();

    // 获取Poller返回事件的时间戳（只读接口）
    Timestamp pollReturnTime() const { return pollRetureTime_; }

    // 在当前EventLoop所属线程中执行回调函数
    // 如果当前线程就是EventLoop所属线程，直接执行；否则放入队列等待执行
    void runInLoop(Functor cb);
    // 把回调函数放入队列，唤醒EventLoop所属线程执行（非阻塞）
    void queueInLoop(Functor cb);

    // 通过eventfd唤醒EventLoop所属线程（解决epoll_wait阻塞问题）
    void wakeup();

    // EventLoop的方法转发给Poller（封装Poller的接口）
    // 更新Channel的事件（比如新增/修改EPOLLIN/EPOLLOUT）
    void updateChannel(Channel *channel);
    // 从Poller中移除Channel（不再监听该Channel的事件）
    void removeChannel(Channel *channel);
    // 判断Poller中是否包含该Channel（避免重复添加）
    bool hasChannel(Channel *channel);

    // 判断当前线程是否是EventLoop所属线程（线程安全检查）
    // threadId_：EventLoop创建时的线程ID；CurrentThread::tid()：当前运行的线程ID
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    /**
     * 定时任务相关函数（封装TimerQueue的功能）
     */
    // 在指定时间执行回调（一次性任务）
    // 参数：timestamp-执行时间，cb-回调函数（右值引用，减少拷贝）
    void runAt(Timestamp timestamp, Functor &&cb)
    {
        // 调用TimerQueue添加定时器：interval=0表示一次性任务
        timerQueue_->addTimer(std::move(cb), timestamp, 0.0);
    }

    // 延迟指定时间后执行回调（一次性任务）
    // 参数：waitTime-延迟时间（秒），cb-回调函数
    void runAfter(double waitTime, Functor &&cb)
    {
        // 计算执行时间：当前时间 + 延迟时间
        Timestamp time(addTime(Timestamp::now(), waitTime));
        // 调用runAt执行定时任务
        runAt(time, std::move(cb));
    }

    // 每隔指定时间执行一次回调（周期性任务）
    // 参数：interval-间隔时间（秒），cb-回调函数
    void runEvery(double interval, Functor &&cb)
    {
        // 计算第一次执行时间：当前时间 + 间隔时间
        Timestamp timestamp(addTime(Timestamp::now(), interval));
        // 调用TimerQueue添加定时器：interval>0表示周期性任务
        timerQueue_->addTimer(std::move(cb), timestamp, interval);
    }

private:
    // 处理wakeupFd_的读事件（eventfd触发时调用）
    // 读取wakeupFd_中的8字节数据，唤醒阻塞的epoll_wait
    void handleRead();
    // 执行队列中待处理的回调函数（pendingFunctors_中的函数）
    void doPendingFunctors();

    // 类型别名：Channel指针的动态数组（存储有事件发生的Channel）
    using ChannelList = std::vector<Channel *>;

    // 原子布尔：是否正在运行事件循环（CAS操作，线程安全）
    std::atomic_bool looping_;
    // 原子布尔：是否退出事件循环（CAS操作，线程安全）
    std::atomic_bool quit_;

    // 常量：EventLoop所属线程的ID（创建时赋值，不可修改）
    const pid_t threadId_;

    // Poller返回事件的时间戳（记录epoll_wait返回的时间）
    Timestamp pollRetureTime_;
    // 智能指针：管理Poller对象（Poller::newDefaultPoller创建epoll实例）
    std::unique_ptr<Poller> poller_;
    // 智能指针：管理定时器队列（处理定时任务）
    std::unique_ptr<TimerQueue> timerQueue_;
    // wakeup文件描述符（eventfd创建）：用于唤醒阻塞的epoll_wait
    int wakeupFd_;
    // 智能指针：管理wakeupChannel（封装wakeupFd_的事件）
    std::unique_ptr<Channel> wakeupChannel_;

    // 存储Poller检测到有事件发生的所有Channel
    ChannelList activeChannels_;

    // 原子布尔：是否正在执行待处理的回调（避免重复唤醒）
    std::atomic_bool callingPendingFunctors_;
    // 存储待执行的回调函数队列（跨线程调用时使用）
    std::vector<Functor> pendingFunctors_;
    // 互斥锁：保护pendingFunctors_的线程安全操作（添加/读取回调）
    std::mutex mutex_;
};
