#include "SdpParser.hpp"
#include <gtest/gtest.h>

using namespace sap;

namespace {

constexpr std::string_view kFullSdp =
    "v=0\r\n"
    "o=- 1 1 IN IP4 192.168.1.10\r\n"
    "s=Virtual Q-SYS Core\r\n"
    "c=IN IP4 239.69.10.20/32\r\n"
    "m=audio 5004 RTP/AVP 97\r\n"
    "a=rtpmap:97 L24/48000/2\r\n";

} // namespace

TEST(SdpParser, ParsesFullValidSdp) {
    const auto info = parse_sdp(kFullSdp, "172.30.36.69");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "Virtual Q-SYS Core");
    EXPECT_EQ(info->source_ip, "172.30.36.69");
    EXPECT_EQ(info->stream_address, "239.69.10.20");
    EXPECT_EQ(info->stream_type, "audio");
    EXPECT_EQ(info->stream_port, 5004);
}

TEST(SdpParser, ParsesConnectionLineWithoutTtl) {
    constexpr std::string_view sdp =
        "s=Stream A\r\n"
        "c=IN IP4 239.1.2.3\r\n"
        "m=audio 6000 RTP/AVP 96\r\n";
    const auto info = parse_sdp(sdp, "10.0.0.5");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->stream_address, "239.1.2.3");
    EXPECT_EQ(info->stream_port, 6000);
}

TEST(SdpParser, ReturnsNulloptWhenSessionNameMissing) {
    constexpr std::string_view sdp =
        "v=0\r\n"
        "c=IN IP4 239.1.2.3\r\n"
        "m=audio 6000 RTP/AVP 96\r\n";
    EXPECT_FALSE(parse_sdp(sdp, "10.0.0.5").has_value());
}

TEST(SdpParser, EmptySessionNameIsRejected) {
    constexpr std::string_view sdp = "s=\r\nm=audio 6000 RTP/AVP 96\r\n";
    EXPECT_FALSE(parse_sdp(sdp, "10.0.0.5").has_value());
}

TEST(SdpParser, MissingConnectionAndMediaLinesYieldDefaults) {
    constexpr std::string_view sdp = "s=Only Name\r\n";
    const auto info = parse_sdp(sdp, "10.0.0.9");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "Only Name");
    EXPECT_TRUE(info->stream_address.empty());
    EXPECT_TRUE(info->stream_type.empty());
    EXPECT_EQ(info->stream_port, 0);
}

TEST(SdpParser, NonNumericPortFallsBackToZero) {
    constexpr std::string_view sdp =
        "s=Bad Port\r\n"
        "m=audio abc RTP/AVP 97\r\n";
    const auto info = parse_sdp(sdp, "10.0.0.1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->stream_type, "audio");
    EXPECT_EQ(info->stream_port, 0);
}

TEST(SdpParser, EmptyPayloadIsRejected) {
    EXPECT_FALSE(parse_sdp("", "10.0.0.1").has_value());
}

TEST(SdpParser, PayloadWithOnlyNewlinesIsRejected) {
    EXPECT_FALSE(parse_sdp("\r\n\r\n\r\n", "10.0.0.1").has_value());
}

TEST(SdpParser, SessionNameWithSpacesIsPreserved) {
    constexpr std::string_view sdp = "s=My Cool Device 2\r\n";
    const auto info = parse_sdp(sdp, "10.0.0.2");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "My Cool Device 2");
}

TEST(SdpParser, LfOnlyLineEndingsAlsoWork) {
    constexpr std::string_view sdp =
        "s=LF Device\n"
        "c=IN IP4 239.9.9.9/32\n"
        "m=audio 7000 RTP/AVP 97\n";
    const auto info = parse_sdp(sdp, "10.0.0.3");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "LF Device");
    EXPECT_EQ(info->stream_address, "239.9.9.9");
    EXPECT_EQ(info->stream_port, 7000);
}

TEST(SdpParser, DetectsSapDeleteFlag) {
    // SAP header byte 0: bit 0x20 = version 1 announce, bit 0x04 = T (delete) flag.
    EXPECT_FALSE(is_sap_delete(std::string_view{"\x20\x00\x00\x00", 4})); // announce
    EXPECT_TRUE(is_sap_delete(std::string_view{"\x24\x00\x00\x00", 4}));  // delete
    EXPECT_FALSE(is_sap_delete(""));
}
