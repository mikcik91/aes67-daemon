#include <iostream>
#include <string>
#include <boost/asio.hpp>

class SapListener {
public:
    SapListener(boost::asio::io_context& io_context, const std::string& multicast_address, short port)
        : socket_(io_context) {
        
        boost::asio::ip::udp::endpoint listen_endpoint(
            boost::asio::ip::address::from_string("0.0.0.0"), port);
        
        socket_.open(listen_endpoint.protocol());
        socket_.set_option(boost::asio::socket_base::reuse_address(true));
        socket_.bind(listen_endpoint);
        
        socket_.set_option(
            boost::asio::ip::multicast::join_group(
                boost::asio::ip::address::from_string(multicast_address)));
        
        start_receive();
    }

private:
    void start_receive() {
        socket_.async_receive_from(
            boost::asio::buffer(data_, max_length), sender_endpoint_,
            [this](boost::system::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    handle_receive(bytes_recvd);
                }
                start_receive();
            });
    }

    void handle_receive(std::size_t bytes_recvd) {
        std::string payload(data_, bytes_recvd);
        
        std::size_t pos = payload.find("s=");
        if (pos != std::string::npos) {
            std::size_t end_pos = payload.find("\r\n", pos);
            if (end_pos != std::string::npos) {
                std::string device_name = payload.substr(pos + 2, end_pos - pos - 2);
                std::cout << "[SAP Received] Device: " << device_name 
                          << " | IP: " << sender_endpoint_.address().to_string() << std::endl;
            }
        }
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
};

int main() {
    try {
        std::cout << "Starting AES67/Dante SAP Listener on 239.255.255.255:9875...\n";
        boost::asio::io_context io_context;
        SapListener listener(io_context, "239.255.255.255", 9875);
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}