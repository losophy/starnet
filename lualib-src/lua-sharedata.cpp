#include "lua-sharedata.h"
#include "lua.h"
#include "lauxlib.h"
#include "starnet_sharedata.h"
#include <string>
#include <memory>

using namespace starnet_sharedata;

#define MT_NAME "starnet.sharedata.box"

//box userdata：持有共享表状态（顶层表 + 版本；嵌套子表共享同一状态）
struct BoxUserData {
    std::shared_ptr<TableState> state;
    std::shared_ptr<SharedTable> table;
};

static BoxUserData* check_box(lua_State *L, int index) {
    BoxUserData *b = (BoxUserData*)luaL_checkudata(L, index, MT_NAME);
    return b;
}

//值压栈（TABLE 递归为子 box）
static int push_value(lua_State *L, const SharedValue &v);

//键压栈
static void push_key(lua_State *L, const SharedKey &key) {
    if (key.type == KEYTYPE_INTEGER) {
        lua_pushinteger(L, (lua_Integer)key.integer);
    } else {
        lua_pushlstring(L, key.str.data(), key.str.size());
    }
}

//递归：Lua 表 -> 紧凑只读表
static std::shared_ptr<SharedTable> conv_table(lua_State *L, int idx);

static SharedValue conv_value(lua_State *L, int idx) {
    SharedValue v;
    int vt = lua_type(L, idx);
    switch (vt) {
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
            v.type = VALUETYPE_INTEGER;
            v.integer = (long long)lua_tointeger(L, idx);
        } else {
            v.type = VALUETYPE_REAL;
            v.real = lua_tonumber(L, idx);
        }
        break;
    case LUA_TSTRING: {
        size_t sz = 0;
        const char *s = lua_tolstring(L, idx, &sz);
        v.type = VALUETYPE_STRING;
        v.str.assign(s, sz);
        break;
    }
    case LUA_TBOOLEAN:
        v.type = VALUETYPE_BOOLEAN;
        v.boolean = lua_toboolean(L, idx) != 0;
        break;
    case LUA_TTABLE:
        v.type = VALUETYPE_TABLE;
        v.table = conv_table(L, idx);
        break;
    default:
        luaL_error(L, "Unsupport value type %s", lua_typename(L, vt));
    }
    return v;
}

static std::shared_ptr<SharedTable> conv_table(lua_State *L, int idx) {
    idx = lua_absindex(L, idx);
    auto tbl = SharedTable::Create();
    //数组段长度由 lua_rawlen 决定（对齐 skynet lua-sharedata.c）
    int sizearray = (int)lua_rawlen(L, idx);
    //遍历收集：1..sizearray 整数键入数组段，其余入哈希段
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        int kt = lua_type(L, -2);
        if (kt == LUA_TNUMBER) {
            if (!lua_isinteger(L, -2)) {
                luaL_error(L, "Invalid key %f", lua_tonumber(L, -2));
            }
            long long nkey = (long long)lua_tointeger(L, -2);
            SharedValue v = conv_value(L, -1);
            if (nkey > 0 && nkey <= sizearray) {
                tbl->SetArray((size_t)nkey, v);
            } else {
                SharedKey k;
                k.type = KEYTYPE_INTEGER;
                k.integer = nkey;
                tbl->AddHash(k, v);
            }
        } else if (kt == LUA_TSTRING) {
            size_t sz = 0;
            const char *s = lua_tolstring(L, -2, &sz);
            SharedValue v = conv_value(L, -1);
            SharedKey k;
            k.type = KEYTYPE_STRING;
            k.str.assign(s, sz);
            tbl->AddHash(k, v);
        } else {
            luaL_error(L, "Invalid key type %s", lua_typename(L, kt));
        }
        lua_pop(L, 1);
    }
    tbl->Finish();
    return tbl;
}

//值压栈（TABLE 创建子 box）
static int push_value(lua_State *L, const SharedValue &v) {
    switch (v.type) {
    case VALUETYPE_REAL:
        lua_pushnumber(L, v.real);
        break;
    case VALUETYPE_INTEGER:
        lua_pushinteger(L, (lua_Integer)v.integer);
        break;
    case VALUETYPE_BOOLEAN:
        lua_pushboolean(L, v.boolean);
        break;
    case VALUETYPE_STRING:
        lua_pushlstring(L, v.str.data(), v.str.size());
        break;
    case VALUETYPE_TABLE: {
        BoxUserData *b = (BoxUserData*)lua_newuserdata(L, sizeof(BoxUserData));
        new (b) BoxUserData();
        //子 box 的 state 由调用方（__index）回填
        b->state = nullptr;
        b->table = v.table;
        luaL_setmetatable(L, MT_NAME);
        break;
    }
    default:
        lua_pushnil(L);
        break;
    }
    return 1;
}

//递归：紧凑只读表 -> Lua 表（deepcopy 用）
static void push_table(lua_State *L, const std::shared_ptr<SharedTable> &tbl) {
    lua_createtable(L, (int)tbl->ArrayLen(), (int)tbl->HashLen());
    SharedKey prev;
    SharedKey outKey;
    const SharedValue *outVal = nullptr;
    while (tbl->NextKey(prev, outKey, outVal)) {
        push_key(L, outKey);
        if (outVal->type == VALUETYPE_TABLE) {
            push_table(L, outVal->table);
        } else {
            push_value(L, *outVal);
        }
        lua_rawset(L, -3);
        prev = outKey;
    }
}

//client：按名取 box（不存在返回 nil）
static int lquery(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    auto st = SharedataStore::inst().Query(name);
    if (!st) {
        return 0;
    }
    BoxUserData *b = (BoxUserData*)lua_newuserdata(L, sizeof(BoxUserData));
    new (b) BoxUserData();
    b->state = st;
    b->table = st->table;
    luaL_setmetatable(L, MT_NAME);
    return 1;
}

//写入：新建或整体替换
static int lhostnew(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    auto table = conv_table(L, 2);
    SharedataStore::inst().Update(name, table);
    return 0;
}

static int ldelete(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    SharedataStore::inst().Delete(name);
    return 0;
}

static int lexist(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_pushboolean(L, SharedataStore::inst().Exist(name));
    return 1;
}

static int lversion(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_pushinteger(L, (lua_Integer)SharedataStore::inst().Version(name));
    return 1;
}

//deepcopy：导出为普通 Lua 表（不存在返回 nil）
static int lcopy(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    auto st = SharedataStore::inst().Query(name);
    if (!st) {
        return 0;
    }
    push_table(L, st->table);
    return 1;
}

//box 只读查找（__index）
static int lindex(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    SharedKey key;
    int kt = lua_type(L, 2);
    if (kt == LUA_TNUMBER) {
        if (!lua_isinteger(L, 2)) {
            return luaL_error(L, "Invalid key %f", lua_tonumber(L, 2));
        }
        key.type = KEYTYPE_INTEGER;
        key.integer = (long long)lua_tointeger(L, 2);
    } else if (kt == LUA_TSTRING) {
        size_t sz = 0;
        const char *s = lua_tolstring(L, 2, &sz);
        key.type = KEYTYPE_STRING;
        key.str.assign(s, sz);
    } else {
        return 0;
    }
    const SharedValue *v = b->table->Lookup(key);
    if (!v) {
        return 0;
    }
    if (v->type == VALUETYPE_TABLE) {
        BoxUserData *nb = (BoxUserData*)lua_newuserdata(L, sizeof(BoxUserData));
        new (nb) BoxUserData();
        nb->state = b->state;   //共享顶层状态（version/isdirty 全局一致）
        nb->table = v->table;
        luaL_setmetatable(L, MT_NAME);
        return 1;
    }
    return push_value(L, *v);
}

//box 迭代（__pairs：返回 迭代函数, box, nil）
static int lnextkey(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    SharedKey prev;
    if (!lua_isnil(L, 2)) {
        int kt = lua_type(L, 2);
        if (kt == LUA_TNUMBER) {
            if (!lua_isinteger(L, 2)) {
                lua_pushnil(L);
                return 1;
            }
            prev.type = KEYTYPE_INTEGER;
            prev.integer = (long long)lua_tointeger(L, 2);
        } else if (kt == LUA_TSTRING) {
            size_t sz = 0;
            const char *s = lua_tolstring(L, 2, &sz);
            prev.type = KEYTYPE_STRING;
            prev.str.assign(s, sz);
        } else {
            lua_pushnil(L);
            return 1;
        }
    }
    SharedKey outKey;
    const SharedValue *outVal = nullptr;
    if (!b->table->NextKey(prev, outKey, outVal)) {
        return 0;
    }
    push_key(L, outKey);
    if (outVal->type == VALUETYPE_TABLE) {
        BoxUserData *nb = (BoxUserData*)lua_newuserdata(L, sizeof(BoxUserData));
        new (nb) BoxUserData();
        nb->state = b->state;
        nb->table = outVal->table;
        luaL_setmetatable(L, MT_NAME);
        return 2;
    }
    push_value(L, *outVal);
    return 2;
}

static int lpairs(lua_State *L) {
    check_box(L, 1);
    lua_pushcfunction(L, lnextkey);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int llen(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    lua_pushinteger(L, (lua_Integer)b->table->ArrayLen());
    return 1;
}

static int lhashlen(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    lua_pushinteger(L, (lua_Integer)b->table->HashLen());
    return 1;
}

static int lversion_box(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    if (b->state) {
        lua_pushinteger(L, (lua_Integer)b->state->version.load());
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int lisdirty(lua_State *L) {
    BoxUserData *b = check_box(L, 1);
    if (b->state) {
        lua_pushboolean(L, b->state->dirty.load());
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

static int lgc(lua_State *L) {
    BoxUserData *b = (BoxUserData*)luaL_checkudata(L, 1, MT_NAME);
    b->~BoxUserData();
    return 0;
}

void LuaSharedata::Register(lua_State *L) {
    static luaL_Reg libs[] = {
        { "query", lquery },
        { "new", lhostnew },
        { "update", lhostnew },
        { "delete", ldelete },
        { "exist", lexist },
        { "version", lversion },
        { "copy", lcopy },
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    //box metatable（只读视图）
    luaL_newmetatable(L, MT_NAME);
    lua_pushcfunction(L, lindex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lpairs);
    lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, llen);
    lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, lgc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, lversion_box);
    lua_setfield(L, -2, "version");
    lua_pushcfunction(L, lisdirty);
    lua_setfield(L, -2, "isdirty");
    lua_pushcfunction(L, lhashlen);
    lua_setfield(L, -2, "hashlen");
    lua_pop(L, 1);
}