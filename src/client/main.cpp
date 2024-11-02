#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <deque>
#include <unordered_map>
#include <memory>
#include <utility>

using boost::asio::ip::tcp;

class EchoClient : public std::enable_shared_from_this<EchoClient> {
private:
    struct PingInfo {
        PingInfo(boost::asio::io_context &io_context)
                : timeout_timer(io_context) {}
        uint64_t epoch;
        boost::asio::steady_timer timeout_timer;
    };

    uint64_t last_epoch_ = 0;
    std::unordered_map<uint64_t, std::shared_ptr<PingInfo>> active_pings_;
    boost::asio::steady_timer ping_timer_;

    boost::asio::io_context &io_context_; // Reference to io_context
    tcp::socket socket_;
    boost::asio::steady_timer reconnect_timer_;
    std::string read_msg_;
    std::deque<std::string> write_msgs_;

    std::string host_;
    short port_;

    void SendPing() {
        uint64_t epoch = ++last_epoch_;
        std::string ping_message = "Ping " + std::to_string(epoch);

        // Ping info
        auto ping_info = std::make_shared<PingInfo>(io_context_);
        ping_info->epoch = epoch;
        ping_info->timeout_timer.expires_after(std::chrono::seconds(2));

        // Ping timeout
        ping_info->timeout_timer.async_wait([self = shared_from_this(), epoch](boost::system::error_code ec) {
            if (!ec) {
                std::cout << "Server did not respond to ping with epoch " << epoch << "\n";
            } else if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "Ping timeout timer error: " << ec.message() << "\n";
            }
            self->active_pings_.erase(epoch);
        });

        active_pings_[epoch] = ping_info;
        SendMessage(ping_message);
    }

    void DoWrite() {
        if (write_msgs_.empty()) {
            return;
        }

        std::string msg = write_msgs_.front();
        std::cout << "Sending message: " << msg << "\n";

        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(msg),
                                 [self](boost::system::error_code ec, std::size_t) {
                                     if (!ec) {
                                         std::cout << "Message sent successfully." << "\n";

                                         self->write_msgs_.pop_front();
                                         if (!self->write_msgs_.empty()) {
                                             self->DoWrite();
                                         }
                                     } else {
                                         std::cerr << "Write error: " << ec.message() << "\n";
                                         if (ec == boost::asio::error::eof ||
                                             ec == boost::asio::error::connection_reset ||
                                             ec == boost::asio::error::broken_pipe) {
                                             self->HandleDisconnect();
                                         }
                                     }
                                 });
    }

public:
    EchoClient(boost::asio::io_context &io_context, std::string host, short port)
            : io_context_(io_context),
              socket_(io_context), ping_timer_(io_context), reconnect_timer_(io_context),
              host_(std::move(host)), port_(port) {
    }

    void Start() {
        StartConnect();
    }

    void StartConnect() {
        socket_ = tcp::socket(io_context_);

        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));

        auto self = shared_from_this();
        boost::asio::async_connect(socket_, endpoints,
                                   [self](boost::system::error_code ec, const tcp::endpoint &) {
                                       if (!ec) {
                                           std::cout << "Connected to the server." << "\n";
                                           self->StartRead();
                                           self->StartTimer();
                                           if (!self->write_msgs_.empty()) {
                                               self->DoWrite();
                                           }
                                       } else {
                                           std::cerr << "Connect failed: " << ec.message() << "\n";
                                           self->AttemptReconnect();
                                       }
                                   });
    }

    void StartRead() {
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_msg_), '\n',
                                      [self](boost::system::error_code ec, std::size_t length) {
                                          if (!ec) {
                                              std::string response(self->read_msg_.substr(0, length - 1));
                                              self->read_msg_.erase(0, length);

                                              std::cout << "Response from server: " << response << "\n";

                                              if (response.rfind("Pong ", 0) == 0) {
                                                  uint64_t epoch = std::stoull(response.substr(5));
                                                  auto it = self->active_pings_.find(epoch);
                                                  if (it != self->active_pings_.end()) {
                                                      it->second->timeout_timer.cancel();
                                                      self->active_pings_.erase(it);
                                                      std::cout << "Received pong for epoch " << epoch << "\n";
                                                  } else {
                                                      std::cout << "Received pong for unknown epoch " << epoch << "\n";
                                                  }
                                              }

                                              self->StartRead();
                                          } else {
                                              std::cerr << "Read error: " << ec.message() << "\n";
                                              if (ec == boost::asio::error::eof ||
                                                  ec == boost::asio::error::connection_reset ||
                                                  ec == boost::asio::error::broken_pipe) {
                                                  self->HandleDisconnect();
                                              }
                                          }
                                      });
    }

    void StartTimer() {
        ping_timer_.expires_after(std::chrono::seconds(5));
        auto self = shared_from_this();
        ping_timer_.async_wait([self](boost::system::error_code ec) {
            if (!ec) {
                self->SendPing();
                self->StartTimer();
            } else if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "Timer error: " << ec.message() << "\n";
            }
        });
    }

    void HandleDisconnect() {
        std::cout << "Connection lost. Attempting to reconnect..." << "\n";

        boost::system::error_code ec;
        socket_.close(ec);

        if (ec) {
            std::cerr << "Error closing socket: " << ec.message() << "\n";
        }

        write_msgs_.clear();

        reconnect_timer_.cancel();
        ping_timer_.cancel();

        for (auto &pair : active_pings_) {
            pair.second->timeout_timer.cancel();
        }
        active_pings_.clear();

        AttemptReconnect();
    }

    void AttemptReconnect() {
        reconnect_timer_.expires_after(std::chrono::seconds(5));
        auto self = shared_from_this();
        reconnect_timer_.async_wait([self](boost::system::error_code ec) {
            if (!ec) {
                std::cout << "Reconnecting..." << "\n";
                self->StartConnect();
            } else {
                std::cerr << "Reconnect timer error: " << ec.message() << "\n";
            }
        });
    }

    void Stop() {
    }

    void SendMessage(const std::string &message) {
        boost::asio::post(io_context_,
                          [self = shared_from_this(), message]() {
                              if (!self->socket_.is_open()) {
                                  std::cout << "Socket is not open. Message not sent: " << message << "\n";
                                  return;
                              }
                              bool write_in_progress = !self->write_msgs_.empty();
                              self->write_msgs_.push_back(message + "\n");

                              if (!write_in_progress) {
                                  self->DoWrite();
                              }
                          });
    }
};

int main() {
    try {
        boost::asio::io_context io_context;

        std::string host = "127.0.0.1";
        short port = 5002;

        auto client = std::make_shared<EchoClient>(io_context, host, port);
        client->Start();

        // Run io_context in a separate thread because input will block our thread
        std::thread io_thread([&io_context]() {
            io_context.run();
        });

        while (true) {
            std::string message;
            std::cout << "Enter a message to send (empty line to exit): ";
            std::getline(std::cin, message);

            if (message.empty()) {
                break;
            }

            client->SendMessage(message);
        }

        // Stop the client and join the thread
        client->Stop();
        io_context.stop();
        io_thread.join();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}