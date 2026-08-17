--sharedata 演示数据文件（对齐 skynet sharedatad CMD.new 的 "@文件" 加载：loadfile 后取 return 的表）
return {
    max_level = 50,
    server_name = "starnet-demo",
    rates = { 1.5, 2.0, 3.25 },
    items = {
        { id = 1001, name = "sword", price = 100 },
        { id = 1002, name = "shield", price = 80 },
    },
    item_price = {
        [1001] = 100,
        [1002] = 80,
        [1003] = 999,
    },
}