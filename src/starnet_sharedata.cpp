//共享只读数据实现（对齐 skynet sharedata：跨服务共享只读表，零拷贝直查）
#include "starnet_sharedata.h"

namespace starnet_sharedata {

//对齐 skynet lua-sharedata.c 的 calchash（字符串哈希）
static uint32_t HashKey(const std::string &s) {
    uint32_t h = (uint32_t)s.size();
    size_t step = (s.size() >> 5) + 1;
    for (size_t i = s.size(); i >= step; i -= step) {
        h = h ^ ((h << 5) + (h >> 2) + (uint8_t)s[i - 1]);
    }
    return h;
}

//构建期：数组段填充（1-based 下标，自动扩容到 index）
void SharedTable::SetArray(size_t index, const SharedValue &v) {
    if (index > 0) {
        if (index > array_.size()) {
            array_.resize(index);
        }
        array_[index - 1] = v;
    }
}

//构建期：哈希段收集（Finish 时统一填入）
void SharedTable::AddHash(const SharedKey &key, const SharedValue &v) {
    SharedKey k = key;
    if (k.type == KEYTYPE_STRING) {
        k.hash = HashKey(k.str);
    } else {
        k.hash = (uint32_t)k.integer;
    }
    pending_.push_back(std::make_pair(k, v));
}

//完成构建：按键数分配哈希段，主槽 + 碰撞链（对齐 skynet fillnocolliding/fillcolliding）
void SharedTable::Finish() {
    if (pending_.empty()) {
        return;
    }
    size_t size = pending_.size();
    hash_.assign(size, SharedNode{});
    FillNoColliding(pending_, size);
    FillColliding(pending_, size);
    pending_.clear();
}

//主槽填充（无碰撞的键直接落在哈希位置）
void SharedTable::FillNoColliding(const std::vector<std::pair<SharedKey, SharedValue>> &items, size_t size) {
    for (const auto &kv : items) {
        SharedNode &n = hash_[kv.first.hash % size];
        if (n.value.type == VALUETYPE_NIL) {
            n.key = kv.first;
            n.value = kv.second;
            n.next = -1;
            n.nocolliding = true;
        }
    }
}

//碰撞填充（挂到主槽的 next 链，溢出到空槽；对齐 skynet fillcolliding）
void SharedTable::FillColliding(const std::vector<std::pair<SharedKey, SharedValue>> &items, size_t size) {
    size_t emptyslot = 0;
    for (const auto &kv : items) {
        SharedNode &main = hash_[kv.first.hash % size];
        bool inserted = (main.key.type == kv.first.type &&
                         main.key.integer == kv.first.integer &&
                         main.key.str == kv.first.str);
        if (!inserted) {
            SharedNode *n = nullptr;
            for (size_t i = emptyslot; i < size; ++i) {
                if (hash_[i].value.type == VALUETYPE_NIL) {
                    n = &hash_[i];
                    emptyslot = i + 1;
                    break;
                }
            }
            if (n == nullptr) {
                continue; //理论不会发生（size=键数）
            }
            n->next = main.next;
            main.next = (int)(n - hash_.data());
            main.nocolliding = false;
            n->key = kv.first;
            n->value = kv.second;
            n->nocolliding = false;
        }
    }
}

const SharedValue* SharedTable::LookupArray(long long key) const {
    if (key > 0 && (size_t)key <= array_.size()) {
        return &array_[(size_t)key - 1];
    }
    return nullptr;
}

//哈希段查找（对齐 skynet lookup_key）
const SharedValue* SharedTable::LookupHash(const SharedKey &key) const {
    if (hash_.empty()) {
        return nullptr;
    }
    const SharedNode *n = &hash_[key.hash % hash_.size()];
    if (key.hash != n->key.hash && n->nocolliding) {
        return nullptr;
    }
    for (;;) {
        if (key.hash == n->key.hash) {
            if (n->key.type == KEYTYPE_INTEGER) {
                if (key.type == KEYTYPE_INTEGER && n->key.integer == key.integer) {
                    return &n->value;
                }
            } else {
                if (key.type == KEYTYPE_STRING && n->key.str == key.str) {
                    return &n->value;
                }
            }
        }
        if (n->next < 0) {
            return nullptr;
        }
        n = &hash_[n->next];
    }
}

//查找（整数键先试数组段，再走哈希段；字符串键走哈希段）
const SharedValue* SharedTable::Lookup(const SharedKey &key) const {
    if (key.type == KEYTYPE_INTEGER) {
        const SharedValue *v = LookupArray(key.integer);
        if (v) {
            return v;
        }
        SharedKey hk = key;
        hk.hash = (uint32_t)hk.integer;
        return LookupHash(hk);
    }
    SharedKey hk = key;
    hk.hash = HashKey(hk.str);
    return LookupHash(hk);
}

//哈希段槽位扫描（迭代按槽位顺序，对齐 skynet lnextkey）
bool SharedTable::NextHashSlot(size_t start, SharedKey &outKey, const SharedValue *&outVal) const {
    for (size_t i = start; i < hash_.size(); ++i) {
        if (hash_[i].value.type != VALUETYPE_NIL) {
            outKey = hash_[i].key;
            outVal = &hash_[i].value;
            return true;
        }
    }
    return false;
}

//迭代（对齐 skynet lnextkey；哨兵 integer=0 表示首次）
bool SharedTable::NextKey(const SharedKey &prev, SharedKey &outKey, const SharedValue *&outVal) const {
    if (prev.type == KEYTYPE_INTEGER && prev.integer == 0 && prev.str.empty()) {
        //首次：数组段 + 哈希段
        for (size_t i = 0; i < array_.size(); ++i) {
            if (array_[i].type != VALUETYPE_NIL) {
                outKey = SharedKey{KEYTYPE_INTEGER, (long long)i + 1, "", (uint32_t)(i + 1)};
                outVal = &array_[i];
                return true;
            }
        }
        return NextHashSlot(0, outKey, outVal);
    }
    if (prev.type == KEYTYPE_INTEGER && prev.integer > 0 && (size_t)prev.integer <= array_.size()) {
        //数组段内往后扫
        for (long long i = prev.integer; i < (long long)array_.size(); ++i) {
            if (array_[i].type != VALUETYPE_NIL) {
                outKey = SharedKey{KEYTYPE_INTEGER, i + 1, "", (uint32_t)(i + 1)};
                outVal = &array_[i];
                return true;
            }
        }
        return NextHashSlot(0, outKey, outVal);
    }
    //哈希段：从 prev 所在槽位往后扫
    size_t start = 0;
    if (prev.type == KEYTYPE_INTEGER) {
        SharedKey hk{KEYTYPE_INTEGER, prev.integer, "", (uint32_t)prev.integer};
        const SharedNode *n = LookupHash(hk);
        if (n) {
            start = (size_t)(n - hash_.data()) + 1;
        }
    } else {
        SharedKey hk{KEYTYPE_STRING, 0, prev.str, HashKey(prev.str)};
        const SharedNode *n = LookupHash(hk);
        if (n) {
            start = (size_t)(n - hash_.data()) + 1;
        }
    }
    return NextHashSlot(start, outKey, outVal);
}

std::shared_ptr<SharedTable> SharedTable::Create() {
    return std::make_shared<SharedTable>();
}

SharedataStore& SharedataStore::inst() {
    static SharedataStore store;
    return store;
}

std::shared_ptr<TableState> SharedataStore::Query(const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(lock_);
    auto it = map_.find(name);
    if (it == map_.end()) {
        return nullptr;
    }
    return it->second;
}

void SharedataStore::Update(const std::string &name, std::shared_ptr<SharedTable> table) {
    auto st = std::make_shared<TableState>();
    st->name = name;
    st->table = std::move(table);
    st->dirty = true;
    std::unique_lock<std::shared_mutex> lock(lock_);
    auto it = map_.find(name);
    if (it != map_.end()) {
        st->version = it->second->version.load() + 1;
    }
    map_[name] = st;
}

void SharedataStore::Delete(const std::string &name) {
    std::unique_lock<std::shared_mutex> lock(lock_);
    map_.erase(name);
}

bool SharedataStore::Exist(const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(lock_);
    return map_.count(name) > 0;
}

uint32_t SharedataStore::Version(const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(lock_);
    auto it = map_.find(name);
    if (it == map_.end()) {
        return 0;
    }
    return it->second->version.load();
}

} // namespace starnet_sharedata