#include "DeviceRegistry.hpp"
#include "Logger.hpp"
#include "httplib.h"
#include <array>
#include <boost/asio.hpp>
#include <csignal>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace sap {

struct Config {
    std::string   multicast_address{"239.255.255.255"};
    std::uint16_t sap_port{9875};
    std::string   http_host{"0.0.0.0"};
    std::uint16_t http_port{8080};
};

[[nodiscard]] inline std::optional<std::string>
parse_session_name(std::string_view payload) noexcept {
    constexpr std::string_view kTag{"s="};
    constexpr std::string_view kCRLF{"\r\n"};
    const auto                 tag_pos = payload.find(kTag);
    if (tag_pos == std::string_view::npos)
        return std::nullopt;
    const auto name_begin = tag_pos + kTag.size();
    const auto name_end   = payload.find(kCRLF, name_begin);
    if (name_end == std::string_view::npos)
        return std::nullopt;
    return std::string{payload.substr(name_begin, name_end - name_begin)};
}

class SapListener : public std::enable_shared_from_this<SapListener> {
  public:
    [[nodiscard]] static std::shared_ptr<SapListener>
    create(boost::asio::io_context& io_context, const Config& config,
           std::shared_ptr<IDeviceObserver> observer) {
        return std::shared_ptr<SapListener>(
            new SapListener(io_context, config, std::move(observer)));
    }

    void start() {
        schedule_receive();
    }

    void shutdown() noexcept {
        boost::system::error_code ec;
        socket_.cancel(ec);
        socket_.close(ec);
    }

    void stop() noexcept {
        shutdown();
    }

    ~SapListener() {
        shutdown();
    }

  private:
    static constexpr std::size_t kMaxPacketBytes{4096};
    using Buffer   = std::array<char, kMaxPacketBytes>;
    using Endpoint = boost::asio::ip::udp::endpoint;
    using Socket   = boost::asio::ip::udp::socket;

    SapListener(boost::asio::io_context& io_context, const Config& config,
                std::shared_ptr<IDeviceObserver> observer)
        : socket_{io_context}, observer_{std::move(observer)} {
        namespace ip = boost::asio::ip;
        const Endpoint listen_endpoint{ip::address_v4::any(), config.sap_port};
        socket_.open(listen_endpoint.protocol());
        socket_.set_option(boost::asio::socket_base::reuse_address{true});
        socket_.bind(listen_endpoint);
        socket_.set_option(
            ip::multicast::join_group(ip::address::from_string(config.multicast_address)));
    }

    void schedule_receive() {
        socket_.async_receive_from(boost::asio::buffer(recv_buffer_), sender_endpoint_,
                                   [self = shared_from_this()](const boost::system::error_code& ec,
                                                               std::size_t bytes_received) {
                                       self->on_receive(ec, bytes_received);
                                   });
    }

    void on_receive(const boost::system::error_code& ec, std::size_t bytes_received) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        if (ec) {
            Logger::error("[SapListener] Receive error: " + ec.message());
        } else if (bytes_received > 0) {
            dispatch({recv_buffer_.data(), bytes_received});
        }
        if (socket_.is_open())
            schedule_receive();
    }

    void dispatch(std::string_view payload) {
        const auto name = parse_session_name(payload);
        if (!name.has_value() || !observer_)
            return;
        observer_->on_device_discovered(
            {.name = std::move(*name), .source_ip = sender_endpoint_.address().to_string()});
    }

    Socket                           socket_;
    Endpoint                         sender_endpoint_;
    Buffer                           recv_buffer_{};
    std::shared_ptr<IDeviceObserver> observer_;
};

// Wrapper around httplib::Server running on a joinable thread,
// so it can be stopped cleanly instead of running detached forever.
class HttpServer {
  public:
    HttpServer(std::string host, std::uint16_t port, std::shared_ptr<DeviceRegistry> registry)
        : host_{std::move(host)}, port_{port}, registry_{std::move(registry)} {}

    ~HttpServer() {
        stop();
    }

    void start() {
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        if (svr_.is_running())
            svr_.stop();
        if (thread_.joinable())
            thread_.join();
    }

  private:
    void run() {
        svr_.Get("/api/devices",
                 [registry = registry_](const httplib::Request&, httplib::Response& res) {
                     const auto     devices = registry->get_active_devices();
                     nlohmann::json j       = nlohmann::json::array();

                     for (const auto& dev : devices) {
                         j.push_back({{"name", dev.name}, {"ip", dev.source_ip}});
                     }

                     res.set_content(j.dump(), "application/json");
                 });

        svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("{\"status\":\"ok\"}", "application/json");
        });

        Logger::info("[HTTP] REST API listening on " + host_ + ':' + std::to_string(port_));
        // listen() returns false when the bind fails (e.g. port already in use).
        if (!svr_.listen(host_.c_str(), static_cast<int>(port_))) {
            Logger::error("[HTTP] Failed to bind " + host_ + ':' + std::to_string(port_) +
                          " (port busy?)");
        }
    }

    std::string                     host_;
    std::uint16_t                   port_;
    std::shared_ptr<DeviceRegistry> registry_;
    httplib::Server                 svr_;
    std::thread                     thread_;
};

} // namespace sap

static void schedule_eviction(boost::asio::steady_timer&           timer,
                              std::shared_ptr<sap::DeviceRegistry> registry,
                              std::chrono::seconds                 interval) {
    timer.expires_after(interval);
    timer.async_wait(
        [&timer, registry = std::move(registry), interval](const boost::system::error_code& ec) {
            if (ec)
                return;
            registry->evict_stale();
            schedule_eviction(timer, registry, interval);
        });
}

int main() {
    try {
        const sap::Config config{};
        sap::Logger::info("Starting AES67/Dante SAP Listener on " + config.multicast_address + ':' +
                          std::to_string(config.sap_port) + "...");

        boost::asio::io_context io_context;

        // Route registry log lines through the leveled logger.
        auto log = [](const std::string& msg) { sap::Logger::info(msg); };

        constexpr auto kTimeout  = std::chrono::seconds{60};
        constexpr auto kEvictRun = std::chrono::seconds{10};

        auto registry = std::make_shared<sap::DeviceRegistry>(kTimeout, log);
        auto listener = sap::SapListener::create(io_context, config, registry);

        sap::HttpServer http{config.http_host, config.http_port, registry};
        http.start();

        boost::asio::steady_timer evict_timer{io_context};
        schedule_eviction(evict_timer, registry, kEvictRun);

        // Catch SIGINT/SIGTERM and shut down gracefully instead of dying abruptly.
        boost::asio::signal_set signals{io_context, SIGINT, SIGTERM};
        signals.async_wait([&](const boost::system::error_code& ec, int sig) {
            if (ec)
                return;
            sap::Logger::warn("Received signal " + std::to_string(sig) + ", shutting down...");
            listener->shutdown();
            evict_timer.cancel();
            io_context.stop();
        });

        listener->start();
        io_context.run();

        http.stop();
        sap::Logger::info("Shutdown complete.");
        return 0;
    } catch (const std::exception& e) {
        sap::Logger::error(std::string{"Fatal: "} + e.what());
        return 1;
    }
}