#pragma once

#include "boost/asio.hpp"
#include "ServerNetwork.h"

class Server {
private:
    std::unique_ptr<NetworkContext> networkContext_;
    boost::asio::io_context io_context_;

public:
    Server(short port) {
        networkContext_ = std::make_unique<NetworkContext>(io_context_, port);

    }

    void Run() {
        io_context_.run();
    }
};