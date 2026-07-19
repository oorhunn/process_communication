//
// Tests for lib/process_communications/HeartbeatSender.hpp
//

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "ErrorTypes.hpp"
#include "Heartbeat.hpp"
#include "HeartbeatSender.hpp"

namespace {

    using lib::process_communications::error_types::TransportError;
    using lib::process_communications::heartbeat::Heartbeat;
    using lib::process_communications::heartbeat::HeartbeatSender;
    using lib::process_communications::heartbeat::HeartbeatSenderConfig;
    using lib::process_communications::heartbeat::k_heartbeat_key;

    // Minimal transport double that records what was published.
    struct RecordingTransport {
        struct Sent {
            std::string m_key;
            std::vector<std::byte> m_bytes;
        };
        std::vector<Sent> m_published{};

        auto publish(std::string_view key, std::span<const std::byte> bytes)
                -> std::expected<void, TransportError> {
            m_published.push_back({std::string{key}, {bytes.begin(), bytes.end()}});
            return {};
        }
    };

    using Clock = HeartbeatSender<RecordingTransport>::Clock;

    TEST(HeartbeatSenderTest, FirstTickSendsImmediately) {
        RecordingTransport transport;
        HeartbeatSender sender{transport, HeartbeatSenderConfig{.m_source_id = 7U}};

        const Clock::time_point t0{};
        EXPECT_TRUE(sender.tick(t0));
        ASSERT_EQ(transport.m_published.size(), 1U);
        EXPECT_EQ(transport.m_published.front().m_key, k_heartbeat_key);
    }

    TEST(HeartbeatSenderTest, DoesNotSendBeforeIntervalElapses) {
        RecordingTransport transport;
        HeartbeatSender sender{
                transport,
                HeartbeatSenderConfig{.m_source_id = 1U, .m_interval = std::chrono::milliseconds{100}}};

        const Clock::time_point t0{};
        ASSERT_TRUE(sender.tick(t0));                                  // first beat
        EXPECT_FALSE(sender.tick(t0 + std::chrono::milliseconds{50})); // too soon
        EXPECT_EQ(transport.m_published.size(), 1U);
    }

    TEST(HeartbeatSenderTest, SendsAgainAfterIntervalElapses) {
        RecordingTransport transport;
        HeartbeatSender sender{
                transport,
                HeartbeatSenderConfig{.m_source_id = 1U, .m_interval = std::chrono::milliseconds{100}}};

        const Clock::time_point t0{};
        ASSERT_TRUE(sender.tick(t0));
        EXPECT_TRUE(sender.tick(t0 + std::chrono::milliseconds{100}));
        EXPECT_TRUE(sender.tick(t0 + std::chrono::milliseconds{250}));
        EXPECT_EQ(transport.m_published.size(), 3U);
    }

    TEST(HeartbeatSenderTest, SequenceIncrementsAndCarriesSourceId) {
        RecordingTransport transport;
        HeartbeatSender sender{
                transport,
                HeartbeatSenderConfig{.m_source_id = 42U, .m_interval = std::chrono::milliseconds{10}}};

        const Clock::time_point t0{};
        ASSERT_TRUE(sender.tick(t0));
        ASSERT_TRUE(sender.tick(t0 + std::chrono::milliseconds{10}));

        ASSERT_EQ(transport.m_published.size(), 2U);
        const auto first = Heartbeat::decode(transport.m_published[0].m_bytes);
        const auto second = Heartbeat::decode(transport.m_published[1].m_bytes);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(first->m_source_id, 42U);
        EXPECT_EQ(first->m_sequence, 0U);
        EXPECT_EQ(second->m_sequence, 1U);
        EXPECT_EQ(sender.sequence(), 2U);
    }

    TEST(HeartbeatSenderTest, SendNowIgnoresInterval) {
        RecordingTransport transport;
        HeartbeatSender sender{
                transport,
                HeartbeatSenderConfig{.m_source_id = 1U, .m_interval = std::chrono::hours{1}}};

        sender.send_now();
        sender.send_now();
        EXPECT_EQ(transport.m_published.size(), 2U);
    }

} // namespace
