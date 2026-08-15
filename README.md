# Starnet

## 第三方依赖

项目依赖 Lua 5.3.5（源码不随仓库分发），需要手动下载并编译：

```
# 下载并解压到 3rd 目录，确保目录结构为 3rd/lua-5.3.5/
wget https://www.lua.org/ftp/lua-5.3.5.tar.gz
tar -xzf lua-5.3.5.tar.gz -C 3rd/

# 编译生成 src/liblua.a
cd 3rd/lua-5.3.5
make linux
cd ../..
```

## 构建与运行

```
cd starnet/build

cmake ../

make

./starnet

客户端：telnet 127.0.0.1 8002
```