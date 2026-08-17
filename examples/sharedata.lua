--sharedata 演示服务：配置数据加载 / 快照读取 / 更新 / 订阅（对齐 skynet.sharedata）
local starnet = require "starnet"
local sharedata = require "starnet.sharedata"

starnet.start(function()
    --从数据文件加载配置（"@路径" loadfile，return 的表进共享表）
    sharedata.new("config", "@../examples/config_data.lua")
    starnet.log("config loaded, version = "..tostring(starnet.sharedata.version("config")))

    --快照读取：零拷贝只读视图
    local c = sharedata.query("config")
    starnet.log("max_level = "..tostring(c.max_level)..", server_name = "..tostring(c.server_name))
    starnet.log("rates[2] = "..tostring(c.rates[2])..", items[1].name = "..tostring(c.items[1].name))
    starnet.log("item_price[1003] = "..tostring(c.item_price[1003]))

    --pairs 迭代（数组段 + 哈希段）
    local sum = 0
    for k, v in pairs(c.item_price) do
        sum = sum + v
    end
    starnet.log("item_price sum = "..tostring(sum))

    --deepcopy：导出普通 Lua 表（可修改，不影响共享表）
    local dc = sharedata.deepcopy("config")
    dc.max_level = 999
    starnet.log("deepcopy.max_level = "..tostring(dc.max_level).." (改副本), box.max_level = "..tostring(c.max_level).." (共享表不变)")

    --快照语义：老 box 继续读旧版，新 query 拿新版
    sharedata.update("config", "@../examples/config_data.lua")
    starnet.sleep(1)
    sharedata.update("config", { max_level = 99, server_name = "hotfix", items = { { id = 2001, name = "axe" } } })
    starnet.log("old box max_level = "..tostring(c.max_level).." (快照，应为旧值 50)")
    local c2 = sharedata.query("config")
    starnet.log("new box max_level = "..tostring(c2.max_level)..", server_name = "..tostring(c2.server_name))

    --订阅（轮询版）：后续 update 触发回调
    sharedata.subscribe("config", function(name, obj)
        starnet.log("subscribe: "..name.." updated, max_level = "..tostring(obj.max_level))
    end)
    starnet.timeout(30, function()
        sharedata.update("config", { max_level = 100, server_name = "v2", items = {} })
    end)

    starnet.log("sharedata demo running, update at 300ms")
end)