#pragma once

#include <iostream>
#include <utility>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <__random/random_device.h>
#include <random>

using boost::asio::ip::tcp;

class NetworkContext {
protected:
    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    std::vector<std::shared_ptr<tcp::socket>> peers_;
    tcp::endpoint leaderId_;

public:
    NetworkContext(boost::asio::io_context &io_context, short port)
            : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), socket_(io_context) {
        std::cout << "Node created on port " << port << "\n";
        StartAccept();
    }

    void StartAccept() {
        auto new_socket = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*new_socket, [this, new_socket](boost::system::error_code ec) {
            if (!ec) {
                auto peer = new_socket->remote_endpoint();
                peers_.emplace_back(new_socket);
                std::make_shared<Session>(new_socket, this)->Start(); // one session - one socket
                std::cout << "Accepted connection from " << peer << "\n";
            }
            StartAccept();
        });
    }

    void SendMessageToPeer(std::shared_ptr<tcp::socket> socket, const std::string &message) {
        if (socket && socket->is_open()) {
            try {
                std::string message_with_newline = message + "\n";
                std::cout << "Sending message to peer " << socket->remote_endpoint() << ": " << message_with_newline
                          << "\n";

                boost::asio::async_write(*socket, boost::asio::buffer(message_with_newline),
                                         [message_with_newline, socket](boost::system::error_code ec,
                                                                        std::size_t length) {
                                             if (!ec) {
                                                 std::cout << "Message sent to peer " << socket->remote_endpoint()
                                                           << ": "
                                                           << message_with_newline << "\n";
                                             } else {
                                                 std::cerr << "Error sending message: " << ec.message() << "\n";
                                             }
                                         });
            } catch (const boost::system::system_error &e) {
                std::cerr << "Error: remote_endpoint failed: " << e.what() << "\n";
                auto it = std::find(peers_.begin(), peers_.end(), socket);
                if (it != peers_.end()) {
                    peers_.erase(it);
                }
            }
        } else {
            std::cerr << "Socket is not open. Cannot send message.\n";
        }
    }


    void ReceiveMessage(const std::string &data, tcp::socket &socket) {
        try {
            auto rem = socket.remote_endpoint();
            std::cout << "Preparing to send response\n";

            std::string answer = "Your message is '" + data + "'. " + GetRandomPhrase();
            auto sock_ptr = socket_by_peer(rem);

            if (sock_ptr) {
                SendMessageToPeer(sock_ptr, answer);
            } else {
                std::cerr << "Socket not found for endpoint " << rem << "\n";
            }
        } catch (const std::exception &e) {
            std::cerr << "ReceiveMessage error: " << e.what() << "\n";
        }
    }

private:
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

    std::shared_ptr<tcp::socket> socket_by_peer(const tcp::endpoint &endpoint) {
        for (auto &socket: peers_) {
            try {
                if (socket->remote_endpoint() == endpoint) {
                    return socket;
                }
            } catch (const std::exception &e) {
                std::cerr << "Error in socket_by_peer: " << e.what() << "\n";
            }
        }
        return nullptr;
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

                                                  std::cout << "Received message: " << message << "\n";
                                                  node_->ReceiveMessage(message, *socket_);

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
