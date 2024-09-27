#pragma once

#include <iostream>
#include <utility>
#include <vector>
#include <memory>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class NetworkContext {
protected:
    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    std::vector<std::shared_ptr<tcp::socket>> peers_;
    tcp::endpoint leaderId_;
    boost::asio::steady_timer timer_;

public:
    NetworkContext(boost::asio::io_context &io_context, short port)
            : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), socket_(io_context),
              timer_(io_context) {
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
                std::cout << "Trying to send message to peer " << socket->remote_endpoint() << ": " << message << "\n";
                boost::asio::async_write(*socket, boost::asio::buffer(message),
                                         [message, socket](boost::system::error_code ec, std::size_t length) {
                                             if (!ec) {
                                                 std::cout << "Message sent to peer " << socket->remote_endpoint()
                                                           << ": "
                                                           << message << "\n";
                                             } else {
                                                 std::cerr << "Error sending message: " << ec.message() << "\n";
                                             }
                                         });
            } catch (const boost::system::system_error &e) {
                std::cerr << "Error: remote_endpoint failed: " << e.what() << "\n";
                auto it = std::find(peers_.begin(), peers_.end(), socket);
                peers_.erase(it);
            }
        } else {
            std::cerr << "Socket is not open. Cannot send message.\n";
        }
    }


    void ReceiveMessage(std::string data, tcp::socket &socket) {
        auto rem = socket.remote_endpoint();

        std::string answer = "Your message is `" + data + "`. Have a nice day!\n";
        SendMessageToPeer(socket_by_peer(rem), answer);
    }

private:
    std::shared_ptr<tcp::socket> socket_by_peer(const tcp::endpoint &endpoint) {
        for (auto &socket: peers_) {
            if (socket->remote_endpoint() != endpoint) continue;
            return socket;
        }
        // handle properly
        throw "Some problems!";
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
                                                  std::cout << "Received message: " << data_ << "\n";
                                                  node_->ReceiveMessage(data_, *socket_);
                                                  data_.clear();
                                                  ReadMessage();  // wait again message
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
