//
// Created by Anıl Orhun Demiroğlu.
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Heartbeat.hpp"

namespace lib::process_communications::heartbeat {

    template<typename T>
    concept HeartbeatPublisher = requires(T transport, std::string_view key, std::span<const std::byte> bytes) {

        transport.publish(key, bytes);
    };

    struct HeartbeatSenderConfig {

        std::uint32_t m_source_id{0U};                                  // identifies who is alive
        std::chrono::milliseconds m_interval{std::chrono::seconds{1}};  // minimum gap between beats
    };

    template<HeartbeatPublisher Transport>
    class HeartbeatSender {
    public:
        using Clock = std::chrono::steady_clock;

        HeartbeatSender(Transport &transport, HeartbeatSenderConfig config) :
                m_transport{transport},
                m_config{config} {
        }

        auto tick() -> bool {

            return tick(Clock::now());
        }

        auto tick(Clock::time_point now) -> bool {

            if (m_sent_once && now - m_last_sent < m_config.m_interval) {

                return false;
            }
            send(now);
            return true;
        }

        auto send_now() -> void {

            send(Clock::now());
        }

        [[nodiscard]] auto sequence() const noexcept -> std::uint64_t {

            return m_sequence;
        }

    private:
        auto send(Clock::time_point now) -> void {

            const Heartbeat beat = Heartbeat::make(m_config.m_source_id, m_sequence++);
            (void) m_transport.publish(k_heartbeat_key, beat.encode());
            m_last_sent = now;
            m_sent_once = true;
        }

        Transport &m_transport;
        HeartbeatSenderConfig m_config;
        Clock::time_point m_last_sent{};
        std::uint64_t m_sequence{0U};
        bool m_sent_once{false};
    };

} // namespace lib::process_communications::heartbeat
