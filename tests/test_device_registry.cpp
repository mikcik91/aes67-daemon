#include "DeviceRegistry.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace sap;
using namespace std::chrono_literals;

namespace {

DeviceInfo make_info(std::string ip, std::string name) {
    DeviceInfo info;
    info.name      = std::move(name);
    info.source_ip = std::move(ip);
    info.stream_address = "239.69.10.20";
    info.stream_type    = "audio";
    info.stream_port    = 5004;
    return info;
}

} // namespace

TEST(DeviceRegistry, NewDeviceAppearsInActiveList) {
    DeviceRegistry reg{60s};
    reg.on_device_discovered(make_info("10.0.0.1", "Dev A"));

    const auto devices = reg.get_active_devices();
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].name, "Dev A");
    EXPECT_EQ(devices[0].source_ip, "10.0.0.1");
}

TEST(DeviceRegistry, SameIpWithNewNameUpdatesSingleEntry) {
    DeviceRegistry reg{60s};
    reg.on_device_discovered(make_info("10.0.0.1", "Old Name"));
    reg.on_device_discovered(make_info("10.0.0.1", "New Name"));

    const auto devices = reg.get_active_devices();
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].name, "New Name");
}

TEST(DeviceRegistry, DistinctIpsCreateDistinctEntries) {
    DeviceRegistry reg{60s};
    reg.on_device_discovered(make_info("10.0.0.1", "A"));
    reg.on_device_discovered(make_info("10.0.0.2", "B"));
    reg.on_device_discovered(make_info("10.0.0.3", "C"));

    EXPECT_EQ(reg.get_active_devices().size(), 3u);
}

TEST(DeviceRegistry, EvictStaleRemovesExpiredDevices) {
    DeviceRegistry reg{100ms};
    reg.on_device_discovered(make_info("10.0.0.1", "Short Lived"));
    std::this_thread::sleep_for(200ms);

    EXPECT_EQ(reg.evict_stale(), 1u);
    EXPECT_TRUE(reg.get_active_devices().empty());
    // Second eviction is a no-op.
    EXPECT_EQ(reg.evict_stale(), 0u);
}

TEST(DeviceRegistry, ActiveDevicesFilterOldEntriesWithoutEviction) {
    DeviceRegistry reg{100ms};
    reg.on_device_discovered(make_info("10.0.0.1", "Stale"));
    std::this_thread::sleep_for(200ms);
    reg.on_device_discovered(make_info("10.0.0.2", "Fresh"));

    const auto devices = reg.get_active_devices();
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].name, "Fresh");
    // Entry still exists internally until eviction runs.
    EXPECT_EQ(reg.evict_stale(), 1u);
}

TEST(DeviceRegistry, RefreshKeepsDeviceAlive) {
    DeviceRegistry reg{150ms};
    reg.on_device_discovered(make_info("10.0.0.1", "Refreshed"));
    std::this_thread::sleep_for(100ms);
    reg.on_device_discovered(make_info("10.0.0.1", "Refreshed")); // bump timestamp
    std::this_thread::sleep_for(100ms);                             // total 200ms > 150ms, but refreshed at 100ms

    EXPECT_EQ(reg.evict_stale(), 0u);
    EXPECT_EQ(reg.get_active_devices().size(), 1u);
}

TEST(DeviceRegistry, LogSinkCalledForNewAndEvictedOnly) {
    std::vector<std::string> logs;
    DeviceRegistry reg{100ms, [&](const std::string& m) { logs.push_back(m); }};

    reg.on_device_discovered(make_info("10.0.0.1", "Logged"));
    reg.on_device_discovered(make_info("10.0.0.1", "Logged")); // refresh — no log
    ASSERT_EQ(logs.size(), 1u);
    EXPECT_NE(logs[0].find("+ Logged"), std::string::npos);

    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(reg.evict_stale(), 1u);
    ASSERT_EQ(logs.size(), 2u);
    EXPECT_NE(logs[1].find("- Logged"), std::string::npos);
}
