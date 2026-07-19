//
// Created by Anıl Orhun Demiroğlu.
//

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace lib::process_communications::heartbeat {

    inline constexpr std::string_view k_heartbeat_key{"heartbeat"};

    struct Heartbeat {
        std::uint32_t m_source_id{0U};    // who is alive
        std::uint64_t m_sequence{0U};     // monotonically increasing per source
        std::uint64_t m_timestamp_ms{0U}; // unix epoch milliseconds when stamped

        static constexpr std::size_t k_wire_size{sizeof(std::uint32_t) + 2U * sizeof(std::uint64_t)};

        [[nodiscard]] static auto make(std::uint32_t source_id, std::uint64_t sequence) -> Heartbeat {

            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Heartbeat{
                    .m_source_id = source_id,
                    .m_sequence = sequence,
                    .m_timestamp_ms = static_cast<std::uint64_t>(millis)};
        }

        [[nodiscard]] auto encode() const -> std::array<std::byte, k_wire_size> {

            std::array<std::byte, k_wire_size> out{};
            std::size_t offset{0U};
            write_le(out, offset, m_source_id);
            write_le(out, offset, m_sequence);
            write_le(out, offset, m_timestamp_ms);
            return out;
        }

        [[nodiscard]] static auto decode(std::span<const std::byte> bytes) -> std::optional<Heartbeat> {

            if (bytes.size() != k_wire_size) {

                return std::nullopt;
            }

            Heartbeat out{};
            std::size_t offset{0U};
            out.m_source_id = read_le<std::uint32_t>(bytes, offset);
            out.m_sequence = read_le<std::uint64_t>(bytes, offset);
            out.m_timestamp_ms = read_le<std::uint64_t>(bytes, offset);
            return out;
        }

    private:
        template<typename T>
        static auto write_le(std::span<std::byte> dst, std::size_t &offset, T value) -> void {

            for (std::size_t i{0U}; i < sizeof(T); ++i) {

                dst[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
            }
            offset += sizeof(T);
        }

        template<typename T>
        static auto read_le(std::span<const std::byte> src, std::size_t &offset) -> T {

            T value{0};
            for (std::size_t i{0U}; i < sizeof(T); ++i) {

                value |= static_cast<T>(static_cast<std::uint8_t>(src[offset + i])) << (8U * i);
            }
            offset += sizeof(T);
            return value;
        }
    };

} // namespace lib::process_communications::heartbeat
