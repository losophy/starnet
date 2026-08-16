#include "lua-netpack.h"
extern "C" {
    #include "lauxlib.h"
}
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//网络封包/粘包半包处理（对齐 skynet lua-netpack.c 的核心逻辑）
//包格式：2字节大端长度头 + 数据（长度最大 0xFFFF）

#define QUEUESIZE 1024
#define HASHSIZE 4096

struct netpack {
    int id;
    int size;
    void * buffer;
};

//fd 半包（跨多次 read 累积）
struct uncomplete {
    struct netpack pack;
    struct uncomplete * next;
    int read;
    int header;
};

//解析队列（userdata，由 starnet.lua 持有；满时丢弃最旧包，不扩容）
struct queue {
    int cap;
    int head;
    int tail;
    struct uncomplete * hash[HASHSIZE];
    struct netpack queue[QUEUESIZE];
};

static void
clear_list(struct uncomplete * uc) {
    while (uc) {
        free(uc->pack.buffer);
        void * tmp = uc;
        uc = uc->next;
        free(tmp);
    }
}

static inline int
hash_fd(int fd) {
    int a = fd >> 24;
    int b = fd >> 12;
    int c = fd;
    return (int)(((uint32_t)(a + b + c)) % HASHSIZE);
}

//取出指定 fd 的半包（并从哈希链表摘除）
static struct uncomplete *
find_uncomplete(struct queue *q, int fd) {
    if (q == NULL)
        return NULL;
    int h = hash_fd(fd);
    struct uncomplete * uc = q->hash[h];
    if (uc == NULL)
        return NULL;
    if (uc->pack.id == fd) {
        q->hash[h] = uc->next;
        return uc;
    }
    struct uncomplete * last = uc;
    while (last->next) {
        uc = last->next;
        if (uc->pack.id == fd) {
            last->next = uc->next;
            return uc;
        }
        last = uc;
    }
    return NULL;
}

//保存 fd 的半包
static struct uncomplete *
save_uncomplete(struct queue *q, int fd) {
    int h = hash_fd(fd);
    struct uncomplete * uc = (struct uncomplete*)malloc(sizeof(struct uncomplete));
    memset(uc, 0, sizeof(*uc));
    uc->next = q->hash[h];
    uc->pack.id = fd;
    q->hash[h] = uc;
    return uc;
}

static inline int
read_size(uint8_t * buffer) {
    int r = (int)buffer[0] << 8 | (int)buffer[1];
    return r;
}

//完整包入队列（more 场景，clone=1 拷贝一份）
static void
push_data(struct queue *q, int fd, void *buffer, int size, int clone) {
    if (clone) {
        void * tmp = malloc(size);
        memcpy(tmp, buffer, size);
        buffer = tmp;
    }
    struct netpack *np = &q->queue[q->tail];
    if (++q->tail >= q->cap)
        q->tail -= q->cap;
    np->id = fd;
    np->buffer = buffer;
    np->size = size;
    if (q->head == q->tail) {
        //队列满：丢弃最旧包（简化，不扩容）
        free(q->queue[q->head].buffer);
        if (++q->head >= q->cap)
            q->head = 0;
    }
}

//处理剩余数据：完整包入队列，尾部半包存 uncomplete
static void
push_more(struct queue *q, int fd, uint8_t *buffer, int size) {
    while (size > 0) {
        if (size == 1) {
            struct uncomplete * uc = save_uncomplete(q, fd);
            uc->read = -1;
            uc->header = *buffer;
            return;
        }
        int pack_size = read_size(buffer);
        buffer += 2;
        size -= 2;
        if (size < pack_size) {
            struct uncomplete * uc = save_uncomplete(q, fd);
            uc->read = size;
            uc->pack.size = pack_size;
            uc->pack.buffer = malloc(pack_size);
            memcpy(uc->pack.buffer, buffer, size);
            return;
        }
        push_data(q, fd, buffer, pack_size, 1);
        buffer += pack_size;
        size -= pack_size;
    }
}

//解析一段字节流，返回 0（半包累积）或 3（type, fd, msg）
static int
filter_data(lua_State *L, struct queue *q, int fd, uint8_t *buffer, int size) {
    if (size == 0)
        return 0;
    //先看是否有该 fd 的半包
    struct uncomplete * uc = find_uncomplete(q, fd);
    if (uc) {
        //填充半包
        if (uc->read < 0) {
            //只剩头 1 字节，补齐长度
            int pack_size = *buffer;
            pack_size |= uc->header << 8;
            ++buffer;
            --size;
            uc->pack.size = pack_size;
            uc->pack.buffer = malloc(pack_size);
            uc->read = 0;
        }
        int need = uc->pack.size - uc->read;
        if (size < need) {
            memcpy((char*)uc->pack.buffer + uc->read, buffer, size);
            uc->read += size;
            int h = hash_fd(fd);
            uc->next = q->hash[h];
            q->hash[h] = uc;
            return 0;
        }
        memcpy((char*)uc->pack.buffer + uc->read, buffer, need);
        buffer += need;
        size -= need;
        //得到一个完整包
        if (size == 0) {
            lua_pushliteral(L, "data");
            lua_pushinteger(L, fd);
            lua_pushlstring(L, (const char*)uc->pack.buffer, uc->pack.size);
            free(uc->pack.buffer);
            free(uc);
            return 3;
        }
        //还有剩余数据
        int first = uc->pack.size;
        void * firstbuf = uc->pack.buffer;
        free(uc);
        push_more(q, fd, buffer, size);
        lua_pushliteral(L, "more");
        lua_pushinteger(L, fd);
        lua_pushlstring(L, (const char*)firstbuf, first);
        free(firstbuf);
        return 3;
    } else {
        //无半包，新数据
        if (size == 1) {
            struct uncomplete * uc = save_uncomplete(q, fd);
            uc->read = -1;
            uc->header = *buffer;
            return 0;
        }
        int pack_size = read_size(buffer);
        buffer += 2;
        size -= 2;
        if (size < pack_size) {
            struct uncomplete * uc = save_uncomplete(q, fd);
            uc->read = size;
            uc->pack.size = pack_size;
            uc->pack.buffer = malloc(pack_size);
            memcpy(uc->pack.buffer, buffer, size);
            return 0;
        }
        if (size == pack_size) {
            //正好一个完整包
            lua_pushliteral(L, "data");
            lua_pushinteger(L, fd);
            lua_pushlstring(L, (const char*)buffer, size);
            return 3;
        }
        //多个完整包
        lua_pushliteral(L, "more");
        lua_pushinteger(L, fd);
        lua_pushlstring(L, (const char*)buffer, pack_size);
        buffer += pack_size;
        size -= pack_size;
        push_more(q, fd, buffer, size);
        return 3;
    }
}

//创建解析队列 userdata
static int
lcreate(lua_State *L) {
    struct queue *q = (struct queue*)lua_newuserdatauv(L, sizeof(struct queue), 0);
    memset(q, 0, sizeof(*q));
    q->cap = QUEUESIZE;
    return 1;
}

//过滤一段字节流（对齐 skynet netpack.filter）
//参数：queue, fd, buff, size
//返回："data", fd, msg 或 "more", fd, msg；半包时无返回值
static int
lfilter(lua_State *L) {
    struct queue *q = (struct queue*)lua_touserdata(L, 1);
    if (q == NULL) {
        return 0;
    }
    int fd = (int)luaL_checkinteger(L, 2);
    size_t len = 0;
    const char *buff = luaL_checklstring(L, 3, &len);
    int size = (int)luaL_checkinteger(L, 4);
    return filter_data(L, q, fd, (uint8_t*)buff, size);
}

//取一个完整包：fd, msg
static int
lpop(lua_State *L) {
    struct queue *q = (struct queue*)lua_touserdata(L, 1);
    if (q == NULL || q->head == q->tail)
        return 0;
    struct netpack *np = &q->queue[q->head];
    if (++q->head >= q->cap) {
        q->head = 0;
    }
    lua_pushinteger(L, np->id);
    lua_pushlstring(L, (const char*)np->buffer, np->size);
    free(np->buffer);
    return 2;
}

//连接关闭：清除该 fd 的半包
static int
lclose(lua_State *L) {
    struct queue *q = (struct queue*)lua_touserdata(L, 1);
    if (q == NULL) {
        return 0;
    }
    int fd = (int)luaL_checkinteger(L, 2);
    struct uncomplete * uc = find_uncomplete(q, fd);
    if (uc) {
        free(uc->pack.buffer);
        free(uc);
    }
    return 0;
}

//清空队列
static int
lclear(lua_State *L) {
    struct queue *q = (struct queue*)lua_touserdata(L, 1);
    if (q == NULL) {
        return 0;
    }
    int i;
    for (i=0;i<HASHSIZE;i++) {
        clear_list(q->hash[i]);
        q->hash[i] = NULL;
    }
    while (q->head != q->tail) {
        struct netpack *np = &q->queue[q->head];
        free(np->buffer);
        if (++q->head >= q->cap) {
            q->head = 0;
        }
    }
    q->head = q->tail = 0;
    return 0;
}

//加 2 字节大端长度头，返回带头字符串
static int
lpack(lua_State *L) {
    size_t len;
    const char *ptr = luaL_checklstring(L, 1, &len);
    if (len >= 0x10000) {
        return luaL_error(L, "Invalid size (too long) of data : %d", (int)len);
    }
    uint8_t *buffer = (uint8_t*)malloc(len + 2);
    buffer[0] = (uint8_t)(len >> 8);
    buffer[1] = (uint8_t)(len & 0xff);
    memcpy(buffer + 2, ptr, len);
    lua_pushlstring(L, (const char*)buffer, len + 2);
    free(buffer);
    return 1;
}

void
LuaNetpack::Register(lua_State *L) {
    static luaL_Reg libs[] = {
        { "create", lcreate },
        { "filter", lfilter },
        { "pop", lpop },
        { "close", lclose },
        { "clear", lclear },
        { "pack", lpack },
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
}