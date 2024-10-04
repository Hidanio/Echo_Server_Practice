#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <deque>


using boost::asio::ip::tcp;

class EchoClient {
private:
    boost::asio::io_context io_context_;
    tcp::socket socket_;
    boost::asio::steady_timer timer_;
    std::string read_msg_;
    std::deque<std::string> write_msgs_;

    void DoWrite() {
        if (write_msgs_.empty()) {
            return;
        }

        std::string msg = write_msgs_.front();
        std::cout << "Sending message: " << msg << "\n";

        boost::asio::async_write(socket_, boost::asio::buffer(msg),
                                 [this](boost::system::error_code ec, std::size_t) {
                                     if (!ec) {
                                         std::cout << "Message sent successfully." << "\n";
                                         ResetTimer();

                                         write_msgs_.pop_front();
                                         if (!write_msgs_.empty()) {
                                             DoWrite();
                                         }
                                     } else {
                                         std::cerr << "Write error: " << ec.message() << "\n";
                                     }
                                 });
    }

public:
    EchoClient(const std::string &host, short port)
            : socket_(io_context_), timer_(io_context_) {
        // Resolve the host and port
        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        boost::asio::connect(socket_, endpoints);

        StartRead();
        StartTimer();
    }

    void StartRead() {
        boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_msg_), '\n',
                                      [this](boost::system::error_code ec, std::size_t length) {
                                          if (!ec) {
                                              std::string response(read_msg_.substr(0, length - 1));
                                              read_msg_.erase(0, length);

                                              std::cout << "Response from server: " << response << "\n";

                                              StartTimer();
                                              StartRead();
                                          } else {
                                              std::cerr << "Read error: " << ec.message() << "\n";
                                          }
                                      });
    }

    void StartTimer() {
        timer_.expires_after(std::chrono::seconds(5));
        timer_.async_wait([this](boost::system::error_code ec) {
            if (!ec) {
                SendMessage("Ping!");
                StartTimer();
            } else if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "Timer error: " << ec.message() << "\n";
            }
        });
    }

    void ResetTimer() {
        StartTimer();
    }

    void Run() {
        io_context_.run();
    }

    void Stop() {
        io_context_.stop();
    }

    void SendMessage(const std::string &message) {
        std::cout << "Let's send message" << "\n";
        boost::asio::post(io_context_,
                          [this, message]() {
                              bool write_in_progress = !write_msgs_.empty();
                              write_msgs_.push_back(message + "\n");

                              if (!write_in_progress) {
                                  std::cout << "Sending message!" << "\n";
                                  DoWrite();
                              }
                          });
    }
};

int main() {
    try {
        std::string host = "127.0.0.1";
        short port = 5001;

        EchoClient client(host, port);

        // Run io_context in a separate thread because input will block our thread
        std::thread io_thread([&client]() {
            client.Run();
        });

        while (true) {
            std::string message;
            std::cout << "Enter a message to send (empty line to exit): ";
            std::getline(std::cin, message);

            if (message.empty()) {
                break;
            }

            client.SendMessage(message);
        }

        // Stop the client and join the thread
        client.Stop();
        io_thread.join();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
