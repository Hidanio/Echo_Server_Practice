#include <boost/asio.hpp>
#include <iostream>
#include <string>

using boost::asio::ip::tcp;

class EchoClient {
private:
    boost::asio::io_context io_context_;
    tcp::socket socket_;
    std::string message_;

public:
    EchoClient(const std::string &host, short port)
            : socket_(io_context_) {
        // Resolve the host and port
        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        // Connect to the server
        boost::asio::connect(socket_, endpoints);
    }

    void SendMessage(const std::string &message) {
        message_ = message + "\n";

        // Write the message to the server
        boost::asio::write(socket_, boost::asio::buffer(message_));

        std::cout << "Message sent to server: " << message << std::endl;
    }

    void ReceiveMessage() {
        boost::asio::streambuf buffer;
        boost::asio::read_until(socket_, buffer, "\n");

        std::istream input(&buffer);
        std::string response;
        std::getline(input, response);

        std::cout << "Response from server: " << response << std::endl;
    }


};

int main() {
    try {
        std::string host = "127.0.0.1";
        short port = 5001;

        EchoClient client(host, port);


        std::string message;
        std::cout << "Enter a message to send: ";
        std::getline(std::cin, message);

        client.SendMessage(message);
        client.ReceiveMessage();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}