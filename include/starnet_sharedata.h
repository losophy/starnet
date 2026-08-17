#pragma once

//共享只读数据（对齐 skynet sharedata：跨服务共享同一份只读数据，零拷贝直查）
//精简版：C++ 全局表（name -> TableState），更新即整体替换（快照语义），
//延迟回收由 shared_ptr 引用计数天然实现（box userdata 持副本，GC 后释放旧版）
#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <atomic>
#include <unordered_map>
#include <shared_mutex>

namespace starnet_sharedata {

//值类型（对齐 skynet lua-sharedata.c）
enum ValueType : uint8_t {
    VALUETYPE_NIL = 0,
    VALUETYPE_REAL = 1,
    VALUETYPE_STRING = 2,
    VALUETYPE_BOOLEAN = 3,
    VALUETYPE_TABLE = 4,
    VALUETYPE_INTEGER = 5,
};

//键类型（对齐 skynet lua-sharedata.c KEYTYPE_*）
enum KeyType : uint8_t {
    KEYTYPE_INTEGER = 0,
    KEYTYPE_STRING = 1,
};

struct SharedTable;

//只读表的一个值（非 union 简化版，type 决定有效字段）
struct SharedValue {
    ValueType type = VALUETYPE_NIL;
    double real = 0;
    long long integer = 0;
    bool boolean = false;
    std::string str;                        //VALUETYPE_STRING
    std::shared_ptr<SharedTable> table;     //VALUETYPE_TABLE（嵌套共享表）
};

//查找键（Lua 绑定层从 Lua 栈构造）
struct SharedKey {
    KeyType type = KEYTYPE_INTEGER;
    long long integer = 0;
    std::string str;
    uint32_t hash = 0;
};

//哈希段节点（开放寻址 + 碰撞链，对齐 skynet struct node）
struct SharedNode {
    SharedKey key;
    SharedValue value;
    int next = -1;      //碰撞链：hash 段内下标，-1 表示链尾
    bool nocolliding = false;   //主槽独占（查找时 keyhash 不匹配可直接退出）
};

//紧凑只读表（构建后不可变）：数组段 [1..sizearray] + 哈希段（开放寻址）
class SharedTable {
public:
    //构建期：数组段填充（1-based 下标，自动扩容到 index）
    void SetArray(size_t index, const SharedValue &v);
    void AddHash(const SharedKey &key, const SharedValue &v);
    void Finish();  //完成哈希段（内部）

    //查找（只读）
    const SharedValue* Lookup(const SharedKey &key) const;

    //迭代（对齐 skynet lnextkey：prev 为空取首个，否则取后继；返回 false 表示结束）
    bool NextKey(const SharedKey &prev, SharedKey &outKey, const SharedValue *&outVal) const;

    size_t ArrayLen() const { return array_.size(); }
    size_t HashLen() const { return hash_.size(); }

    //从 Lua 表构建（递归入口，lua 绑定层调用）
    static std::shared_ptr<SharedTable> Create();

private:
    const SharedValue* LookupArray(long long key) const;
    const SharedValue* LookupHash(const SharedKey &key) const;
    bool NextHashSlot(size_t start, SharedKey &outKey, const SharedValue *&outVal) const;
    void FillNoColliding(const std::vector<std::pair<SharedKey, SharedValue>> &items, size_t size);
    void FillColliding(const std::vector<std::pair<SharedKey, SharedValue>> &items, size_t size);

    std::vector<SharedValue> array_;    //数组段（下标 0 对应键 1，长度=sizearray）
    std::vector<SharedNode> hash_;      //哈希段
    std::vector<std::pair<SharedKey, SharedValue>> pending_;    //构建期哈希键值收集
};

//共享表状态（name 下的一个版本：表 + 版本号，更新时整体替换）
struct TableState {
    std::string name;
    std::shared_ptr<SharedTable> table;
    std::atomic<uint32_t> version{0};
    std::atomic<bool> dirty{false};
};

//全局共享存储（跨服务可见，读写分离）
class SharedataStore {
public:
    static SharedataStore& inst();

    //按名取当前版本（不存在返回 null）
    std::shared_ptr<TableState> Query(const std::string &name);

    //新建或整体替换（版本递增，旧版由引用方持有延迟回收）
    void Update(const std::string &name, std::shared_ptr<SharedTable> table);

    void Delete(const std::string &name);
    bool Exist(const std::string &name);
    uint32_t Version(const std::string &name);

private:
    SharedataStore();
    mutable std::shared_mutex lock_;
    std::unordered_map<std::string, std::shared_ptr<TableState>> map_;
};

} // namespace starnet_sharedata