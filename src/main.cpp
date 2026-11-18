#include "DeviceRegistry.hpp"
#include "Logger.hpp"
#include "SdpParser.hpp"
#include "httplib.h"
#include <array>
#include <boost/asio.hpp>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace sap {

namespace detail {

[[nodiscard]] inline std::uint16_t env_port(const char* name, std::uint16_t fallback) noexcept {
    if (const char* v = std::getenv(name)) {
        try {
            const auto parsed = std::stoi(v);
            if (parsed > 0 && parsed <= 65535)
                return static_cast<std::uint16_t>(parsed);
        } catch (...) {
        }
    }
    return fallback;
}

[[nodiscard]] inline std::string env_str(const char* name, std::string fallback) {
    if (const char* v = std::getenv(name))
        return v;
    return fallback;
}

[[nodiscard]] inline int env_seconds(const char* name, int fallback) noexcept {
    if (const char* v = std::getenv(name)) {
        try {
            const auto parsed = std::stoi(v);
            if (parsed > 0)
                return parsed;
        } catch (...) {
        }
    }
    return fallback;
}

} // namespace detail

struct Config {
    std::string   multicast_address{detail::env_str("AES67_SAP_GROUP", "239.255.255.255")};
    std::uint16_t sap_port{detail::env_port("AES67_SAP_PORT", 9875)};
    std::string   http_host{detail::env_str("AES67_HTTP_HOST", "0.0.0.0")};
    std::uint16_t http_port{detail::env_port("AES67_HTTP_PORT", 8080)};
    int           device_timeout_sec{detail::env_seconds("AES67_DEVICE_TIMEOUT", 60)};
    int           evict_interval_sec{detail::env_seconds("AES67_EVICT_INTERVAL", 10)};
};

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
        socket_.set_option(ip::multicast::join_group(ip::make_address(config.multicast_address)));
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
        if (!observer_)
            return;
        const auto source_ip = sender_endpoint_.address().to_string();
        if (is_sap_delete(payload)) {
            if (auto* reg = dynamic_cast<DeviceRegistry*>(observer_.get()))
                reg->on_device_removed(source_ip);
            return;
        }
        auto info = parse_sdp(payload, source_ip);
        if (!info.has_value())
            return;
        observer_->on_device_discovered(std::move(*info));
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
                         j.push_back({{"name", dev.name},
                                      {"ip", dev.source_ip},
                                      {"stream_address", dev.stream_address},
                                      {"stream_type", dev.stream_type},
                                      {"stream_port", dev.stream_port}});
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
            const auto evicted = registry->evict_stale();
            if (evicted > 0) {
                sap::Logger::info("Evicted " + std::to_string(evicted) + " stale device(s)");
            }
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

        const auto kTimeout  = std::chrono::seconds{config.device_timeout_sec};
        const auto kEvictRun = std::chrono::seconds{config.evict_interval_sec};

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