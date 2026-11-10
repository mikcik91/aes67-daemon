#include <iostream>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

int main() {
    boost::asio::io_context io;
    nlohmann::json test_json = {{"status", "ok"}, {"daemon", "AES67 Discovery"}};

    std::cout << "AES67 Discovery Daemon initialized successfully.\n";
    std::cout << "JSON Test Payload: " << test_json.dump(2) << "\n";
    return 0;
}