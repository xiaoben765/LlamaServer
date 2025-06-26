#pragma once
#include <vector> // 引入 std::vector，一个动态数组。我们用它来存储所有排序后的哈希值 (sortedHashes_)。
#include <unordered_map> // 引入 std::unordered_map，一个哈希表。我们用它来存储哈希值到节点名称的映射 (circle_)。
#include <string> // 引入 std::string，用于表示节点名称和键。
#include <functional> // 引入 std::function，一个通用的函数包装器。它允许我们接受任意可调用对象（如函数指针、lambda表达式）作为哈希函数。
#include <algorithm> // 引入标准算法库，例如 std::sort (排序) 和 std::upper_bound (查找)。
#include <mutex> // 引入 std::mutex 和 std::lock_guard，用于在多线程环境下保护共享数据，确保线程安全。
#include <stdexcept> // 引入标准异常类，如 std::runtime_error，用于在发生错误时通知调用者。

/**
 * @class ConsistentHash
 * @brief 实现一致性哈希算法的类。
 *
 * 一致性哈希是一种分布式哈希技术，旨在最小化在节点添加或移除时键的重新分配。
 * 常用于分布式缓存系统和分布式数据库分片等场景。
 */
class ConsistentHash {
public:
    /**
     * @brief 构造函数
     * @param numReplicas 每个物理节点的虚拟节点数量，增加虚拟节点可改善负载均衡效果。
     * @param hashFunc 可选的自定义哈希函数，默认为 std::hash。
     */
    ConsistentHash(size_t numReplicas, std::function<size_t(const std::string&)> hashFunc = std::hash<std::string>())
        : numReplicas_(numReplicas), hashFunction_(hashFunc) {}

    /**
     * @brief 向哈希环中添加一个节点。
     *
     * 每个节点会被复制为若干个虚拟节点。每个虚拟节点通过 `node + index` 计算出唯一的哈希值。
     * 这些哈希值存储在哈希环上，并进行排序以便高效查找。
     *
     * @param node 要添加的节点名称（如服务器地址）。
     */

    // 创建虚拟节点: 对每个物理节点，我们创建 numReplicas_ 个虚拟节点。
    void addNode(const std::string& node) {
        std::lock_guard<std::mutex> lock(mtx_); // 确保多线程安全
        for (size_t i = 0; i < numReplicas_; ++i) {
            // 为每个虚拟节点计算唯一哈希值
            size_t hash = hashFunction_(node +"_0"+std::to_string(i));
            circle_[hash] = node;         // 哈希值映射到节点
            sortedHashes_.push_back(hash); // 添加到排序列表
        }
        // 对哈希值进行排序，便于后续的查找
        std::sort(sortedHashes_.begin(), sortedHashes_.end());
    }

    /**
     * @brief 从哈希环中移除一个节点。
     *
     * 删除该节点的所有虚拟节点及其对应的哈希值。
     *
     * @param node 要移除的节点名称。
     */
    void removeNode(const std::string& node) {
        std::lock_guard<std::mutex> lock(mtx_); // 确保多线程安全
        for (size_t i = 0; i < numReplicas_; ++i) {
            // 计算虚拟节点的哈希值
            size_t hash = hashFunction_(node +"_0"+std::to_string(i)); 
            circle_.erase(hash); // 从哈希环中删除该哈希
            auto it = std::find(sortedHashes_.begin(), sortedHashes_.end(), hash);
            if (it != sortedHashes_.end()) {
                sortedHashes_.erase(it); // 从排序列表中删除
            }
        }
    }

    /**
     * @brief 查找负责处理给定键的节点。
     *
     * 根据键的哈希值在哈希环中查找第一个大于等于该值的节点。
     * 如果没有找到（即超出哈希环最大值），则回绕到第一个节点。
     *
     * @param key 要查找的键（如数据的标识符）。
     * @return 负责处理该键的节点名称。
     * @throws std::runtime_error 如果哈希环为空（没有节点）。
     */

    size_t getNode(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_); // 确保多线程安全
        if (circle_.empty()) {
            throw std::runtime_error("No nodes in consistent hash"); // 环为空时抛出异常
        }
        size_t hash = hashFunction_(key); // 计算键的哈希值
        // 在已排序的哈希列表中找到第一个大于键哈希值的位置
        auto it = std::upper_bound(sortedHashes_.begin(), sortedHashes_.end(), hash);
        if (it == sortedHashes_.end()) {
            // 如果超出环最大值，则回绕到第一个节点
            it = sortedHashes_.begin();
        }
        return *it; // 返回对应的哈希值
    }

private:
    // 每个物理节点对应多少个虚拟节点。虚拟节点越多，数据分布越均匀。
    size_t numReplicas_; 

    // 哈希函数。可以是默认的 std::hash，也可以是用户指定的函数。
    // std::function 使得这个设计非常灵活。
    std::function<size_t(const std::string&)> hashFunction_; 

    // 哈希环的核心数据结构。它是一个哈希表（map），
    // 键(key)是虚拟节点的哈希值(size_t)，值(value)是它所属的真实物理节点的名称(std::string)。
    // 根据哈希值 快速（O(1)平均时间）找到对应的节点名称
    std::unordered_map<size_t, std::string> circle_;

    // 一个动态数组，专门用来存储 circle_ 中所有的键（也就是所有虚拟节点的哈希值）。
    // 并且这个数组始终保持有序状态。这样我们就可以使用高效的二分查找算法（如 std::upper_bound）来快速定位节点。
    // 在环上寻找下一个节点
    std::vector<size_t> sortedHashes_; 

    // 互斥锁。因为添加或删除节点会修改 circle_ 和 sortedHashes_，
    // 如果有多个线程同时操作，可能会导致数据损坏。这个锁就是为了防止这种情况发生。
    std::mutex mtx_;
};
