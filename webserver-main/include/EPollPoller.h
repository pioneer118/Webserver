#pragma once // 防止头文件重复包含（替代传统的#ifndef+#define+#endif）

// 引入C++标准库头文件
#include <vector> // 动态数组（存储epoll_wait返回的事件、活跃Channel）
// 引入系统头文件
#include <sys/epoll.h> // epoll相关函数/宏（epoll_create/epoll_ctl/epoll_wait等）

// 引入项目自定义头文件
#include "Poller.h"    // 继承Poller抽象基类
#include "Timestamp.h" // 时间戳工具类（记录poll返回的时间）

/**
 * 核心说明（小白版）：
 * 1. EPollPoller是Poller的子类，实现了epoll版本的IO复用；
 * 2. epoll使用三步曲：
 *    - epoll_create：创建epoll实例（得到epollfd）；
 *    - epoll_ctl：添加/修改/删除要监听的fd及事件；
 *    - epoll_wait：等待事件发生，返回有事件的fd列表；
 * 3. 作用：替代Poller的抽象接口，实现具体的epoll事件监听逻辑。
 **/

// 前置声明：避免包含头文件，减少编译依赖
class Channel;

// EPollPoller继承自Poller（实现epoll的具体逻辑）
class EPollPoller : public Poller
{
public:
    // 构造函数：创建EPollPoller对象
    // 参数：loop - 所属的EventLoop（传给父类Poller的构造函数）
    EPollPoller(EventLoop *loop);
    // 析构函数：override表示重写父类的虚析构函数
    ~EPollPoller() override;

    // 重写父类Poller的纯虚函数（实现epoll的核心逻辑）
    // 1. 等待事件发生（对应epoll_wait）
    // 参数：timeoutMs - 超时时间（毫秒）；activeChannels - 输出参数，存储有事件的Channel
    // 返回值：事件发生的时间戳
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    // 2. 更新Channel的监听事件（对应epoll_ctl的ADD/MOD）
    void updateChannel(Channel *channel) override;
    // 3. 移除Channel（对应epoll_ctl的DEL）
    void removeChannel(Channel *channel) override;

private:
    // 静态常量：epoll事件列表的初始大小（默认16，不够时自动扩容）
    static const int kInitEventListSize = 16;

    // 辅助函数1：把epoll_wait返回的有事件的fd，填充到activeChannels中
    // 参数：numEvents - epoll_wait返回的事件数量；activeChannels - 要填充的活跃Channel列表
    // const：函数不修改EPollPoller的成员变量
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 辅助函数2：调用epoll_ctl更新Channel的事件（ADD/MOD/DEL）
    // 参数：operation - epoll_ctl的操作类型（EPOLL_CTL_ADD/MOD/DEL）；channel - 要更新的Channel
    void update(int operation, Channel *channel);

    // 类型别名：epoll_event的动态数组（存储epoll_wait返回的事件）
    // 说明：C++中可以省略struct，直接写epoll_event（epoll_event是系统定义的结构体）
    using EventList = std::vector<epoll_event>;

    // 核心成员变量
    int epollfd_;      // epoll_create创建的epoll实例fd（epoll的句柄）
    EventList events_; // 存储epoll_wait返回的所有事件（每个元素是epoll_event结构体）
};