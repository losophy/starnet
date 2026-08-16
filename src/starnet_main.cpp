#include "starnet.h"
#include "starnet_config.h"
#include <iostream>
#include <unistd.h>

int testConn() {
    Starnet::inst->AddConn(1, 1, Conn::TYPE::LISTEN);
    Starnet::inst->AddConn(2, 1, Conn::TYPE::CLIENT);
    Starnet::inst->RemoveConn(2);
    cout << Starnet::inst->GetConn(1).get() << endl;
    cout << Starnet::inst->GetConn(2).get() << endl;
    return 0;
}

int testSocketCtrl() {
    int fd = Starnet::inst->Listen(8001, 1);
    usleep(30*1000000);
    Starnet::inst->CloseConn(fd);
    return 0;
}


int TestEcho() {
    auto t = make_shared<string>("gateway");
    uint32_t gateway = Starnet::inst->NewService(t);
    return 0;
}

int main(int argc, char* argv[]) {
    //加载配置（对齐 skynet：starnet [config.lua]，无参数用默认）
    StarnetConfig cfg = StarnetConfig::Load(argc > 1 ? argv[1] : NULL);
    new Starnet();
    Starnet::inst->Start(cfg);
    //启动start服务（对齐 skynet config.start）
    auto t = make_shared<string>(cfg.start);
    Starnet::inst->NewService(t);
    //wait
    Starnet::inst->Wait();
    return 0;
}