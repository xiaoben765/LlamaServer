#include <Poller.h>
#include <Channel.h>

Poller::Poller(EventLoop *loop) // Poller的构造函数，传入所属的EventLoop
    : ownerLoop_(loop)
{
}

bool Poller::hasChannel(Channel *channel) const // 检查通道是否在Poller中
{
    auto it = channels_.find(channel->fd());
    // find 方法的返回值是一个迭代器 (it)。如果找到了，迭代器就指向对应的键值对；
    // 如果没找到，就返回一个指向 map 末尾的特殊迭代器 channels_.end()。
    return it != channels_.end() && it->second == channel;
    // 为什么需要第二次检查？因为可能存在同一个 fd 对应多个 Channel 的情况。
}