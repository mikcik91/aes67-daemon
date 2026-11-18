#pragma once

#include "DeviceRegistry.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sap {

[[nodiscard]] inline std::string_view sdp_line(std::string_view payload,
                                               std::string_view tag) noexcept {
    const auto tag_pos = payload.find(tag);
    if (tag_pos == std::string_view::npos)
        return {};
    const auto begin = tag_pos + tag.size();
    auto       end   = payload.find_first_of("\r\n", begin);
    if (end == std::string_view::npos)
        end = payload.size();
    constexpr auto ws    = " \t";
    const auto     first = payload.find_first_not_of(ws, begin);
    if (first == std::string_view::npos || first >= end)
        return {};
    const auto last = payload.find_last_not_of(ws, end - 1);
    return payload.substr(first, last - first + 1);
}

// SAP header: byte 0, bit 0x04 = "T" flag -> deletion (SAPv2 RFC 2974).
[[nodiscard]] inline bool is_sap_delete(std::string_view payload) noexcept {
    return !payload.empty() && (static_cast<unsigned char>(payload[0]) & 0x04) != 0;
}

[[nodiscard]] inline std::optional<DeviceInfo> parse_sdp(std::string_view payload,
                                                         std::string      source_ip) noexcept {
    const auto name = sdp_line(payload, "s=");
    if (name.empty())
        return std::nullopt;

    DeviceInfo info;
    info.name      = std::string{name};
    info.source_ip = std::move(source_ip);

    // "c=IN IP4 239.69.10.20/32" -> "239.69.10.20"
    if (const auto c = sdp_line(payload, "c=IN IP4 "); !c.empty()) {
        const auto slash    = c.find('/');
        info.stream_address = std::string{c.substr(0, slash)};
    }

    // "m=audio 5004 RTP/AVP 97" -> type "audio", port 5004
    if (const auto m = sdp_line(payload, "m="); !m.empty()) {
        const auto sp1 = m.find(' ');
        const auto sp2 = m.find(' ', sp1 == std::string_view::npos ? sp1 : sp1 + 1);
        if (sp1 != std::string_view::npos) {
            info.stream_type = std::string{m.substr(0, sp1)};
            if (sp2 != std::string_view::npos) {
                try {
                    info.stream_port = static_cast<std::uint16_t>(
                        std::stoul(std::string{m.substr(sp1 + 1, sp2 - sp1 - 1)}));
                } catch (...) {
                    info.stream_port = 0;
                }
            }
        }
    }

    return info;
}

} // namespace sap
