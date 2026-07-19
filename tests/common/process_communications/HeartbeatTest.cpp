//
// Tests for lib/process_communications/Heartbeat.hpp
//

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Heartbeat.hpp"

namespace {

    using lib::process_communications::heartbeat::Heartbeat;
    using lib::process_communications::heartbeat::k_heartbeat_key;

    TEST(HeartbeatTest, EncodeProducesFixedWireSize) {
        const Heartbeat beat{.m_source_id = 7U, .m_sequence = 3U, .m_timestamp_ms = 123U};

        EXPECT_EQ(beat.encode().size(), Heartbeat::k_wire_size);
    }

    TEST(HeartbeatTest, RoundTripsThroughEncodeDecode) {
        const Heartbeat original{.m_source_id = 42U, .m_sequence = 9001U, .m_timestamp_ms = 1'700'000'000'123U};

        const auto encoded = original.encode();
        const auto decoded = Heartbeat::decode(encoded);

        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->m_source_id, original.m_source_id);
        EXPECT_EQ(decoded->m_sequence, original.m_sequence);
        EXPECT_EQ(decoded->m_timestamp_ms, original.m_timestamp_ms);
    }

    TEST(HeartbeatTest, EncodingIsLittleEndian) {
        const Heartbeat beat{.m_source_id = 0x01020304U, .m_sequence = 0U, .m_timestamp_ms = 0U};

        const auto encoded = beat.encode();

        EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x04U);
        EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[1]), 0x03U);
        EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[2]), 0x02U);
        EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[3]), 0x01U);
    }

    TEST(HeartbeatTest, DecodeRejectsWrongSizedPayload) {
        const std::vector<std::byte> too_short(Heartbeat::k_wire_size - 1U);
        const std::vector<std::byte> too_long(Heartbeat::k_wire_size + 1U);

        EXPECT_FALSE(Heartbeat::decode(too_short).has_value());
        EXPECT_FALSE(Heartbeat::decode(too_long).has_value());
    }

    TEST(HeartbeatTest, MakeStampsTimeAndPreservesFields) {
        const auto beat = Heartbeat::make(5U, 12U);

        EXPECT_EQ(beat.m_source_id, 5U);
        EXPECT_EQ(beat.m_sequence, 12U);
        EXPECT_GT(beat.m_timestamp_ms, 0U);
    }

    TEST(HeartbeatTest, WellKnownKeyIsHeartbeat) {
        EXPECT_EQ(k_heartbeat_key, "heartbeat");
    }

} // namespace
