#include "lua-seri.h"
extern "C" {
    #include "lauxlib.h"
}
#include <string.h>
#include <stdint.h>
#include <string>

//序列化（对齐 skynet lualib-src/lua-seri.c 的核心编码）
//类型编码：0=nil 1=boolean 2=number(hibits:0零/1byte/2word/4dword/6qword/8double) 3=userdata 4=短字符串 5=长字符串 6=表

#define TYPE_NIL 0
#define TYPE_BOOLEAN 1
#define TYPE_NUMBER 2
#define TYPE_NUMBER_ZERO 0
#define TYPE_NUMBER_BYTE 1
#define TYPE_NUMBER_WORD 2
#define TYPE_NUMBER_DWORD 4
#define TYPE_NUMBER_QWORD 6
#define TYPE_NUMBER_REAL 8

#define TYPE_USERDATA 3
#define TYPE_SHORT_STRING 4
#define TYPE_LONG_STRING 5
#define TYPE_TABLE 6

#define MAX_COOKIE 32
#define COMBINE_TYPE(t,v) ((t) | (v) << 3)
#define MAX_DEPTH 32

static inline void
wb_nil(std::string &b) {
    uint8_t n = TYPE_NIL;
    b.append((char *)&n, 1);
}

static inline void
wb_boolean(std::string &b, int boolean) {
    uint8_t n = COMBINE_TYPE(TYPE_BOOLEAN, boolean ? 1 : 0);
    b.append((char *)&n, 1);
}

static inline void
wb_integer(std::string &b, lua_Integer v) {
    int type = TYPE_NUMBER;
    if (v == 0) {
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_ZERO);
        b.append((char *)&n, 1);
    } else if (v != (int32_t)v) {
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_QWORD);
        int64_t v64 = v;
        b.append((char *)&n, 1);
        b.append((char *)&v64, sizeof(v64));
    } else if (v < 0) {
        int32_t v32 = (int32_t)v;
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_DWORD);
        b.append((char *)&n, 1);
        b.append((char *)&v32, sizeof(v32));
    } else if (v < 0x100) {
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_BYTE);
        b.append((char *)&n, 1);
        uint8_t byte = (uint8_t)v;
        b.append((char *)&byte, sizeof(byte));
    } else if (v < 0x10000) {
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_WORD);
        b.append((char *)&n, 1);
        uint16_t word = (uint16_t)v;
        b.append((char *)&word, sizeof(word));
    } else {
        uint8_t n = COMBINE_TYPE(type, TYPE_NUMBER_DWORD);
        b.append((char *)&n, 1);
        uint32_t v32 = (uint32_t)v;
        b.append((char *)&v32, sizeof(v32));
    }
}

static inline void
wb_real(std::string &b, double v) {
    uint8_t n = COMBINE_TYPE(TYPE_NUMBER, TYPE_NUMBER_REAL);
    b.append((char *)&n, 1);
    b.append((char *)&v, sizeof(v));
}

static inline void
wb_pointer(std::string &b, void *v) {
    uint8_t n = TYPE_USERDATA;
    b.append((char *)&n, 1);
    b.append((char *)&v, sizeof(v));
}

static inline void
wb_string(std::string &b, const char *str, int len) {
    if (len < MAX_COOKIE) {
        uint8_t n = COMBINE_TYPE(TYPE_SHORT_STRING, len);
        b.append((char *)&n, 1);
        if (len > 0) {
            b.append(str, len);
        }
    } else {
        uint8_t n;
        if (len < 0x10000) {
            n = COMBINE_TYPE(TYPE_LONG_STRING, 2);
            b.append((char *)&n, 1);
            uint16_t x = (uint16_t)len;
            b.append((char *)&x, 2);
        } else {
            n = COMBINE_TYPE(TYPE_LONG_STRING, 4);
            b.append((char *)&n, 1);
            uint32_t x = (uint32_t)len;
            b.append((char *)&x, 4);
        }
        b.append(str, len);
    }
}

static void pack_one(lua_State *L, std::string &b, int index, int depth);

static int
wb_table_array(lua_State *L, std::string &b, int index, int depth) {
    int array_size = lua_rawlen(L, index);
    if (array_size >= MAX_COOKIE-1) {
        uint8_t n = COMBINE_TYPE(TYPE_TABLE, MAX_COOKIE-1);
        b.append((char *)&n, 1);
        wb_integer(b, array_size);
    } else {
        uint8_t n = COMBINE_TYPE(TYPE_TABLE, array_size);
        b.append((char *)&n, 1);
    }
    int i;
    for (i=1;i<=array_size;i++) {
        lua_rawgeti(L, index, i);
        pack_one(L, b, -1, depth);
        lua_pop(L, 1);
    }
    return array_size;
}

static void
wb_table_hash(lua_State *L, std::string &b, int index, int depth, int array_size) {
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER) {
            if (lua_isinteger(L, -2)) {
                lua_Integer x = lua_tointeger(L, -2);
                if (x>0 && x<=array_size) {
                    lua_pop(L, 1);
                    continue;
                }
            }
        }
        pack_one(L, b, -2, depth);
        pack_one(L, b, -1, depth);
        lua_pop(L, 1);
    }
    wb_nil(b);
}

static int
wb_table_metapairs(lua_State *L, std::string &b, int index, int depth) {
    uint8_t n = COMBINE_TYPE(TYPE_TABLE, 0);
    b.append((char *)&n, 1);
    lua_pushvalue(L, index);
    if (lua_pcall(L, 1, 3, 0) != LUA_OK)
        return 1;
    for(;;) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_copy(L, -5, -3);
        if (lua_pcall(L, 2, 2, 0) != LUA_OK)
            return 1;
        int type = lua_type(L, -2);
        if (type == LUA_TNIL) {
            lua_pop(L, 4);
            break;
        }
        pack_one(L, b, -2, depth);
        pack_one(L, b, -1, depth);
        lua_pop(L, 1);
    }
    wb_nil(b);
    return 0;
}

static int
wb_table(lua_State *L, std::string &b, int index, int depth) {
    if (!lua_checkstack(L, LUA_MINSTACK)) {
        lua_pushstring(L, "out of memory");
        return 1;
    }
    if (index < 0) {
        index = lua_gettop(L) + index + 1;
    }
    if (luaL_getmetafield(L, index, "__pairs") != LUA_TNIL) {
        return wb_table_metapairs(L, b, index, depth);
    } else {
        int array_size = wb_table_array(L, b, index, depth);
        wb_table_hash(L, b, index, depth, array_size);
        return 0;
    }
}

static void
pack_one(lua_State *L, std::string &b, int index, int depth) {
    if (depth > MAX_DEPTH) {
        luaL_error(L, "serialize can't pack too depth table");
    }
    int type = lua_type(L, index);
    switch(type) {
    case LUA_TNIL:
        wb_nil(b);
        break;
    case LUA_TNUMBER: {
        if (lua_isinteger(L, index)) {
            lua_Integer x = lua_tointeger(L, index);
            wb_integer(b, x);
        } else {
            lua_Number n = lua_tonumber(L, index);
            wb_real(b, n);
        }
        break;
    }
    case LUA_TBOOLEAN:
        wb_boolean(b, lua_toboolean(L, index));
        break;
    case LUA_TSTRING: {
        size_t sz = 0;
        const char *str = lua_tolstring(L, index, &sz);
        wb_string(b, str, (int)sz);
        break;
    }
    case LUA_TLIGHTUSERDATA:
        wb_pointer(b, lua_touserdata(L, index));
        break;
    case LUA_TTABLE: {
        if (index < 0) {
            index = lua_gettop(L) + index + 1;
        }
        if (wb_table(L, b, index, depth+1)) {
            lua_error(L);
        }
        break;
    }
    default:
        luaL_error(L, "Unsupport type %s to serialize", lua_typename(L, type));
    }
}

//读端
struct read_block {
    const char * buffer;
    int len;
    int ptr;
};

static const void *
rb_read(struct read_block *rb, int sz) {
    if (rb->len < sz) {
        return NULL;
    }
    int ptr = rb->ptr;
    rb->ptr += sz;
    rb->len -= sz;
    return rb->buffer + ptr;
}

static inline void
invalid_stream_line(lua_State *L, struct read_block *rb, int line) {
    int len = rb->len;
    luaL_error(L, "Invalid serialize stream %d (line:%d)", len, line);
}

#define invalid_stream(L,rb) invalid_stream_line(L,rb,__LINE__)

static lua_Integer
get_integer(lua_State *L, struct read_block *rb, int cookie) {
    switch (cookie) {
    case TYPE_NUMBER_ZERO:
        return 0;
    case TYPE_NUMBER_BYTE: {
        uint8_t n;
        const uint8_t * pn = (const uint8_t *)rb_read(rb, sizeof(n));
        if (pn == NULL)
            invalid_stream(L, rb);
        n = *pn;
        return n;
    }
    case TYPE_NUMBER_WORD: {
        uint16_t n;
        const void * pn = rb_read(rb, sizeof(n));
        if (pn == NULL)
            invalid_stream(L, rb);
        memcpy(&n, pn, sizeof(n));
        return n;
    }
    case TYPE_NUMBER_DWORD: {
        int32_t n;
        const void * pn = rb_read(rb, sizeof(n));
        if (pn == NULL)
            invalid_stream(L, rb);
        memcpy(&n, pn, sizeof(n));
        return n;
    }
    case TYPE_NUMBER_QWORD: {
        int64_t n;
        const void * pn = rb_read(rb, sizeof(n));
        if (pn == NULL)
            invalid_stream(L, rb);
        memcpy(&n, pn, sizeof(n));
        return n;
    }
    default:
        invalid_stream(L, rb);
        return 0;
    }
}

static double
get_real(lua_State *L, struct read_block *rb) {
    double n;
    const void * pn = rb_read(rb, sizeof(n));
    if (pn == NULL)
        invalid_stream(L, rb);
    memcpy(&n, pn, sizeof(n));
    return n;
}

static void *
get_pointer(lua_State *L, struct read_block *rb) {
    void * userdata = 0;
    const void * v = rb_read(rb, sizeof(userdata));
    if (v == NULL) {
        invalid_stream(L, rb);
    }
    memcpy(&userdata, v, sizeof(userdata));
    return userdata;
}

static void
get_buffer(lua_State *L, struct read_block *rb, int len) {
    const char * p = (const char *)rb_read(rb, len);
    if (p == NULL) {
        invalid_stream(L, rb);
    }
    lua_pushlstring(L, p, len);
}

static void unpack_one(lua_State *L, struct read_block *rb);

static void
unpack_table(lua_State *L, struct read_block *rb, int array_size) {
    if (array_size == MAX_COOKIE-1) {
        uint8_t type;
        const uint8_t * t = (const uint8_t *)rb_read(rb, sizeof(type));
        if (t==NULL) {
            invalid_stream(L, rb);
        }
        type = *t;
        int cookie = type >> 3;
        if ((type & 7) != TYPE_NUMBER || cookie == TYPE_NUMBER_REAL) {
            invalid_stream(L, rb);
        }
        array_size = get_integer(L, rb, cookie);
    }
    luaL_checkstack(L, LUA_MINSTACK, NULL);
    lua_createtable(L, array_size, 0);
    int i;
    for (i=1;i<=array_size;i++) {
        unpack_one(L, rb);
        lua_rawseti(L, -2, i);
    }
    for (;;) {
        unpack_one(L, rb);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return;
        }
        unpack_one(L, rb);
        lua_rawset(L, -3);
    }
}

static void
push_value(lua_State *L, struct read_block *rb, int type, int cookie) {
    switch(type) {
    case TYPE_NIL:
        lua_pushnil(L);
        break;
    case TYPE_BOOLEAN:
        lua_pushboolean(L, cookie);
        break;
    case TYPE_NUMBER:
        if (cookie == TYPE_NUMBER_REAL) {
            lua_pushnumber(L, get_real(L, rb));
        } else {
            lua_pushinteger(L, get_integer(L, rb, cookie));
        }
        break;
    case TYPE_USERDATA:
        lua_pushlightuserdata(L, get_pointer(L, rb));
        break;
    case TYPE_SHORT_STRING:
        get_buffer(L, rb, cookie);
        break;
    case TYPE_LONG_STRING: {
        if (cookie == 2) {
            const void * plen = rb_read(rb, 2);
            if (plen == NULL) {
                invalid_stream(L, rb);
            }
            uint16_t n;
            memcpy(&n, plen, sizeof(n));
            get_buffer(L, rb, n);
        } else {
            if (cookie != 4) {
                invalid_stream(L, rb);
            }
            const void * plen = rb_read(rb, 4);
            if (plen == NULL) {
                invalid_stream(L, rb);
            }
            uint32_t n;
            memcpy(&n, plen, sizeof(n));
            get_buffer(L, rb, n);
        }
        break;
    }
    case TYPE_TABLE: {
        unpack_table(L, rb, cookie);
        break;
    }
    default: {
        invalid_stream(L, rb);
        break;
    }
    }
}

static void
unpack_one(lua_State *L, struct read_block *rb) {
    uint8_t type;
    const uint8_t * t = (const uint8_t *)rb_read(rb, sizeof(type));
    if (t==NULL) {
        invalid_stream(L, rb);
    }
    type = *t;
    push_value(L, rb, type & 0x7, type>>3);
}

//反序列化核心：从 buffer 读 len 字节，返回值个数（结果压栈）
static int
unpack_buffer(lua_State *L, const char *buffer, int len) {
    if (len == 0) {
        return 0;
    }
    if (buffer == NULL) {
        return luaL_error(L, "deserialize null pointer");
    }
    struct read_block rb;
    rb.buffer = buffer;
    rb.len = len;
    rb.ptr = 0;
    int i;
    for (i=0;;i++) {
        if (i%8==7) {
            luaL_checkstack(L, LUA_MINSTACK, NULL);
        }
        uint8_t type = 0;
        const uint8_t * t = (const uint8_t *)rb_read(&rb, sizeof(type));
        if (t==NULL)
            break;
        type = *t;
        push_value(L, &rb, type & 0x7, type>>3);
    }
    return lua_gettop(L);
}

//序列化栈上全部参数为字符串（对齐 skynet.serialize）
int LuaSeri::Serialize(lua_State *luaState) {
    std::string out;
    int n = lua_gettop(luaState);
    for (int i=1;i<=n;i++) {
        pack_one(luaState, out, i, 0);
    }
    lua_pushlstring(luaState, out.data(), out.size());
    return 1;
}

//反序列化字符串为多个返回值（对齐 skynet.unserialize）
int LuaSeri::Unserialize(lua_State *luaState) {
    if (lua_isnoneornil(luaState, 1)) {
        return 0;
    }
    size_t sz = 0;
    const char *buffer = lua_tolstring(luaState, 1, &sz);
    lua_settop(luaState, 0);
    int n = unpack_buffer(luaState, buffer, (int)sz);
    return n;
}

//序列化 + 4 字节大端长度前缀（网络/跨节点友好，对齐 skynet.packstring）
int LuaSeri::PackString(lua_State *luaState) {
    std::string out;
    int n = lua_gettop(luaState);
    for (int i=1;i<=n;i++) {
        pack_one(luaState, out, i, 0);
    }
    uint32_t len = (uint32_t)out.size();
    char head[4] = { (char)(len>>24), (char)(len>>16), (char)(len>>8), (char)len };
    std::string s;
    s.append(head, 4);
    s.append(out);
    lua_pushlstring(luaState, s.data(), s.size());
    return 1;
}

//读 4 字节大端长度前缀 + 反序列化（对齐 skynet.unpackstring）
int LuaSeri::UnpackString(lua_State *luaState) {
    size_t sz = 0;
    const char *buffer = lua_tolstring(luaState, 1, &sz);
    if (buffer == NULL || sz < 4) {
        return luaL_error(luaState, "Invalid serialize stream");
    }
    uint32_t len = ((uint32_t)(uint8_t)buffer[0]<<24)
                 | ((uint32_t)(uint8_t)buffer[1]<<16)
                 | ((uint32_t)(uint8_t)buffer[2]<<8)
                 | ((uint32_t)(uint8_t)buffer[3]);
    if (sz < 4 + len) {
        return luaL_error(luaState, "Invalid serialize stream");
    }
    lua_settop(luaState, 0);
    int n = unpack_buffer(luaState, buffer + 4, (int)len);
    return n;
}