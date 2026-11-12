#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <boost/asio.hpp>
#include "DeviceRegistry.hpp"

namespace sap {

struct Config {
    std::string   multicast_address{ "239.255.255.255" };
    std::uint16_t port{ 9875 };
};

[[nodiscard]] inline std::optional<std::string>
parse_session_name(std::string_view payload) noexcept {
    constexpr std::string_view kTag { "s=" };
    constexpr std::string_view kCRLF{ "\r\n" };
    const auto tag_pos = payload.find(kTag);
    if (tag_pos == std::string_view::npos) return std::nullopt;
    const auto name_begin = tag_pos + kTag.size();
    const auto name_end   = payload.find(kCRLF, name_begin);
    if (name_end == std::string_view::npos) return std::nullopt;
    return std::string{ payload.substr(name_begin, name_end - name_begin) };
}

class SapListener : public std::enable_shared_from_this<SapListener> {
public:
    [[nodiscard]] static std::shared_ptr<SapListener>
    create(boost::asio::io_context&         io_context,
           const Config&                    config,
           std::shared_ptr<IDeviceObserver> observer) {
        return std::shared_ptr<SapListener>(
            new SapListener(io_context, config, std::move(observer)));
    }

    void start() { schedule_receive(); }

    void stop() {
        boost::system::error_code ec;
        socket_.cancel(ec);
    }

private:
    static constexpr std::size_t kMaxPacketBytes{ 4096 };
    using Buffer   = std::array<char, kMaxPacketBytes>;
    using Endpoint = boost::asio::ip::udp::endpoint;
    using Socket   = boost::asio::ip::udp::socket;

    SapListener(boost::asio::io_context&         io_context,
                const Config&                    config,
                std::shared_ptr<IDeviceObserver> observer)
        : socket_  { io_context }
        , observer_{ std::move(observer) }
    {
        namespace ip = boost::asio::ip;
        const Endpoint listen_endpoint{ ip::address_v4::any(), config.port };
        socket_.open(listen_endpoint.protocol());
        socket_.set_option(boost::asio::socket_base::reuse_address{ true });
        socket_.bind(listen_endpoint);
        socket_.set_option(ip::multicast::join_group(
            ip::address::from_string(config.multicast_address)));
    }

    void schedule_receive() {
        socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_),
            sender_endpoint_,
            [self = shared_from_this()]
            (const boost::system::error_code& ec, std::size_t bytes_received) {
                self->on_receive(ec, bytes_received);
            });
    }

    void on_receive(const boost::system::error_code& ec,
                    std::size_t                      bytes_received) {
        if (ec == boost::asio::error::operation_aborted) return;
        if (ec) {
            std::cerr << "[SapListener] Receive error: " << ec.message() << '\n';
        } else if (bytes_received > 0) {
            dispatch({ recv_buffer_.data(), bytes_received });
        }
        schedule_receive();
    }

    void dispatch(std::string_view payload) {
        const auto name = parse_session_name(payload);
        if (!name.has_value() || !observer_) return;
        observer_->on_device_discovered(
            { .name      = *name,
              .source_ip = sender_endpoint_.address().to_string() });
    }

    Socket                           socket_;
    Endpoint                         sender_endpoint_;
    Buffer                           recv_buffer_{};
    std::shared_ptr<IDeviceObserver> observer_;
};

} // namespace sap

// Fires registry.evict_stale() every `interval` seconds on the io_context thread.
static void schedule_eviction(boost::asio::steady_timer&            timer,
                              std::shared_ptr<sap::DeviceRegistry>  registry,
                              std::chrono::seconds                  interval) {
    timer.expires_after(interval);
    timer.async_wait([&timer, registry = std::move(registry), interval]
                     (const boost::system::error_code& ec) {
        if (ec) return;
        registry->evict_stale();
        schedule_eviction(timer, registry, interval);
    });
}

int main() {
    try {
        const sap::Config config{};
        std::cout << "Starting AES67/Dante SAP Listener on "
                  << config.multicast_address << ':' << config.port << "...\n";
                  
        boost::asio::io_context io_context;

        auto log = [](const std::string& msg) { std::cout << msg << '\n'; };

        constexpr auto kTimeout  = std::chrono::seconds{60};
        constexpr auto kEvictRun = std::chrono::seconds{10};

        auto registry = std::make_shared<sap::DeviceRegistry>(kTimeout, log);
        auto listener = sap::SapListener::create(io_context, config, registry);
        
        // Periodic stale-device eviction.
        boost::asio::steady_timer evict_timer{io_context};
        schedule_eviction(evict_timer, registry, kEvictRun);

        listener->start();
        io_context.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}