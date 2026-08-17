--starnet 共享只读数据客户端库（对齐 skynet.sharedata）
--跨服务共享同一份只读数据（C++ 全局表，box 只读视图零拷贝直查，不走消息队列）
--写入：new/update 直接调 C 绑定（建议业务侧由单一服务负责加载/热更）
--读取：query 返回最新快照 box（老 box 继续读旧版，GC 后释放）；本地缓存按版本号自动失效
--订阅：subscribe 为轮询简化版（对齐 skynet 的 monitor 推送，本期用轮询）

local starnet = require "starnet"
local sd = starnet.sharedata

local sharedata = {}
local cache = setmetatable({}, { __mode = "kv" })  -- name -> box（弱值，业务不持有时回收）

--查询（返回 box 只读视图；版本未变用本地缓存，变了重查）
function sharedata.query(name)
    local b = cache[name]
    if b and b:version() == sd.version(name) then
        return b
    end
    local nb = sd.query(name)
    if nb then
        cache[name] = nb
    else
        cache[name] = nil
    end
    return nb
end

--数据加载（对齐 skynet sharedatad CMD.new）：table 直接用；"@文件" loadfile；代码串 load
local env_mt = { __index = _ENV }
local function loaddata(name, t)
    local dt = type(t)
    if dt == "table" then
        return t
    elseif dt == "string" then
        local value = setmetatable({}, env_mt)
        local f
        if t:sub(1, 1) == "@" then
            f = assert(loadfile(t:sub(2), "bt", value))
        else
            f = assert(load(t, "=" .. name, "bt", value))
        end
        local _, ret = assert(pcall(f))
        setmetatable(value, nil)
        if type(ret) == "table" then
            return ret
        end
        return value
    else
        error("sharedata.new: unknown data type " .. dt)
    end
end

--新建/整体替换（版本递增，dirty 置位）
function sharedata.new(name, v, ...)
    sd.new(name, loaddata(name, v))
end

function sharedata.update(name, v, ...)
    sd.update(name, loaddata(name, v))
end

function sharedata.delete(name)
    cache[name] = nil
    sd.delete(name)
end

--导出整表为普通 Lua 表（深拷贝；不存在返回 nil）
function sharedata.deepcopy(name)
    return sd.copy(name)
end

--订阅更新（轮询版）：版本变化时重查并回调 fn(name, box)；返回订阅协程（starnet.exit 可停）
function sharedata.subscribe(name, fn)
    local last = sd.version(name)
    return starnet.fork(function()
        while true do
            starnet.sleep(10)
            local cur = sd.version(name)
            if cur ~= last then
                last = cur
                local obj = sharedata.query(name)
                if obj then
                    fn(name, obj)
                end
            end
        end
    end)
end

return sharedata