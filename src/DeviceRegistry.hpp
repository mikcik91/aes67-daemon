#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sap {

struct DeviceInfo {
    std::string  name;          // SDP "s=" — session/stream name
    std::string  source_ip;     // UDP sender address
    std::string  stream_address; // SDP "c=" — stream multicast address (may be empty)
    std::uint16_t stream_port{0}; // SDP "m=" port (0 = not present)
    std::string  stream_type;   // SDP "m=" media type, e.g. "audio"
};

class IDeviceObserver {
public:
    virtual ~IDeviceObserver() = default;
    virtual void on_device_discovered(const DeviceInfo& info) = 0;
};

class DeviceRegistry final : public IDeviceObserver {
public:
    using Clock    = std::chrono::steady_clock;
    using LogSink  = std::function<void(const std::string&)>;

    template <typename Rep, typename Period>
    explicit DeviceRegistry(std::chrono::duration<Rep, Period> timeout,
                            LogSink log = nullptr)
        : timeout_{std::chrono::duration_cast<std::chrono::milliseconds>(timeout)},
          log_{std::move(log)} {}

    explicit DeviceRegistry(LogSink log)
        : DeviceRegistry{std::chrono::seconds{60}, std::move(log)} {}

    // Called from the io_context thread on every SAP packet.
    void on_device_discovered(const DeviceInfo& info) override {
        bool is_new = false;
        {
            std::unique_lock lock{mutex_};
            auto it = devices_.find(info.source_ip);
            if (it != devices_.end()) {
                // Hot path: bump the timestamp and refresh the name in case
                // the device was renamed without changing its IP.
                it->second.last_seen = Clock::now();
                it->second.info.name = info.name;
            } else {
                devices_.emplace(info.source_ip,
                                 Entry{.info = info, .last_seen = Clock::now()});
                is_new = true;
            }
        }
        // Log outside the lock — cout/sink can block.
        if (is_new && log_) {
            log_("[Registry] + " + info.name + " (" + info.source_ip + ")");
        }
    }

    // Only returns devices seen within the timeout window.
    [[nodiscard]] std::vector<DeviceInfo> get_active_devices() const {
        std::shared_lock lock{mutex_};
        const auto cutoff = Clock::now() - timeout_;
        std::vector<DeviceInfo> result;
        result.reserve(devices_.size());
        for (const auto& [ip, entry] : devices_) {
            if (entry.last_seen >= cutoff)
                result.push_back(entry.info);
        }
        return result;
    }

    // Call this periodically (timer, dedicated thread, etc.).
    [[nodiscard]] std::size_t evict_stale() {
        std::vector<std::string> expired_names;
        {
            std::unique_lock lock{mutex_};
            const auto cutoff = Clock::now() - timeout_;
            for (auto it = devices_.begin(); it != devices_.end();) {
                if (it->second.last_seen < cutoff) {
                    expired_names.push_back(
                        it->second.info.name + " (" + it->first + ")");
                    it = devices_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // Log outside the lock.
        if (log_) {
            for (const auto& s : expired_names)
                log_("[Registry] - " + s);
        }
        return expired_names.size();
    }

private:
    struct Entry {
        DeviceInfo        info;
        Clock::time_point last_seen;
    };

    const std::chrono::milliseconds                timeout_;
    LogSink                                        log_;
    mutable std::shared_mutex                      mutex_;
    std::unordered_map<std::string, Entry>         devices_;
};

} // namespace sap