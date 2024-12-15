#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <random>

using boost::asio::ip::tcp;

class NetworkContext {
protected:
    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
    std::unordered_map<std::shared_ptr<tcp::socket>, uint64_t> client_last_epoch_;
    std::unordered_map<std::shared_ptr<tcp::socket>, std::shared_ptr<boost::asio::steady_timer>> client_timers_;
public:
    NetworkContext(boost::asio::io_context &io_context, short port)
            : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        std::cout << "Node created on port " << port << "\n";
        StartAccept();
    }

    void StartAccept() {
        auto new_socket = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*new_socket, [this, new_socket](boost::system::error_code ec) {
            if (!ec) {
                auto peer = new_socket->remote_endpoint();
                std::cout << "Accepted connection from " << peer << "\n";

                client_last_epoch_[new_socket] = 0; // Get last epoch for this client
                client_timers_.emplace(new_socket, std::make_shared<boost::asio::steady_timer>(io_context_));
                StartPingTimeout(new_socket);
                std::make_shared<Session>(new_socket, this)->Start();
            }
            StartAccept();
        });
    }

    void SendMessageToPeer(const std::shared_ptr<tcp::socket>& socket, const std::string &message) {
        if (socket && socket->is_open()) {
            try {
                std::string message_with_newline = message + "\n";
                std::cout << "Sending message to peer " << socket->remote_endpoint() << ": " << message_with_newline << "\n";

                boost::asio::async_write(*socket, boost::asio::buffer(message_with_newline),
                                         [socket](boost::system::error_code ec, std::size_t /*length*/) {
                                             if (ec) {
                                                 std::cerr << "Error sending message: " << ec.message() << "\n";
                                             }
                                         });
            } catch (const boost::system::system_error &e) {
                std::cerr << "Error: remote_endpoint failed: " << e.what() << "\n";
            }
        } else {
            std::cerr << "Socket is not open. Cannot send message.\n";
        }
    }

    void ReceiveMessage(const std::string &data, std::shared_ptr<tcp::socket> socket) {
        try {
            auto rem = socket->remote_endpoint();
            std::cout << "Received message from " << rem << ": " << data << "\n";

            std::string response;
            if (data.rfind("Ping ", 0) == 0) {
                std::string epoch_str = data.substr(5);
                uint64_t received_epoch = std::stoull(epoch_str);

                if (received_epoch > client_last_epoch_[socket]) {
                    client_last_epoch_[socket] = received_epoch;
                    response = "Pong " + std::to_string(received_epoch);
                    StartPingTimeout(socket); // Reset the ping timeout timer
                } else {
                    response = "Invalid epoch " + std::to_string(received_epoch);
                }
            } else {
                response = "Your message is '" + data + "'. " + GetRandomPhrase();
            }

            SendMessageToPeer(socket, response);
        } catch (const std::exception &e) {
            std::cerr << "ReceiveMessage error: " << e.what() << "\n";
        }
    }

private:
    void StartPingTimeout(const std::shared_ptr<tcp::socket>& socket) {
        if (client_timers_.find(socket) == client_timers_.end()) {
            client_timers_[socket] = std::make_shared<boost::asio::steady_timer>(io_context_);
        }

        auto& timer = client_timers_[socket];
        timer->expires_after(std::chrono::seconds(10));

        timer->async_wait([this, socket](boost::system::error_code ec) {
            if (!ec) {
                std::cerr << "Client " << socket->remote_endpoint() << " failed to send ping in time. Closing connection.\n";
                socket->close(); // Close the socket if the client fails to ping in time
                client_last_epoch_.erase(socket);
                client_timers_.erase(socket);
            } else if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "Timer error: " << ec.message() << "\n";
            }
        });
    }

    std::string GetRandomPhrase() {
        static const std::vector<std::string> phrases = {
                "Have a nice day!",
                "Good deal!",
                "From Mars with love",
                "Stay awesome!",
                "Keep it up!",
                "May the Force be with you!",
                "Live long and prosper!",
                "Hakuna Matata!",
                "To infinity and beyond!",
                "Winter is coming!",
                "I am Groot!",
                "Why so serious?",
                "Elementary, my dear Watson.",
                "Here's looking at you, kid.",
                "Hasta la vista, baby.",
                "Yippee-ki-yay!",
                "I'll be back.",
                "You can't handle the truth!",
                "Just keep swimming.",
                "I'm king of the world!"
        };

        // Thread-safe random number generator
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<size_t> distribution(0, phrases.size() - 1);

        return phrases[distribution(generator)];
    }

    class Session : public std::enable_shared_from_this<Session> {
    private:
        std::shared_ptr<tcp::socket> socket_;
        std::string data_;
        NetworkContext *node_;

        void ReadMessage() {
            auto self(shared_from_this());
            boost::asio::async_read_until(*socket_, boost::asio::dynamic_buffer(data_), '\n',
                                          [this, self](boost::system::error_code ec, std::size_t length) {
                                              if (!ec) {
                                                  std::string message(data_.substr(0, length - 1)); // Exclude '\n'
                                                  data_.erase(0, length); // Remove the processed message

                                                  node_->ReceiveMessage(message, socket_);
                                                  ReadMessage();
                                              } else {
                                                  std::cerr << "Read error: " << ec.message() << "\n";
                                              }
                                          });
        }

    public:
        Session(std::shared_ptr<tcp::socket> socket, NetworkContext *node)
                : socket_(std::move(socket)), node_(node) {}

        void Start() {
            ReadMessage();
        }
    };
};