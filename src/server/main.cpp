#include <iostream>
#include "Server.h"

int main(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Usage: server <port> \n";
        return 1;
    }

    short port = std::atoi(argv[1]);

    try {
        Server server(port);
        server.Run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}