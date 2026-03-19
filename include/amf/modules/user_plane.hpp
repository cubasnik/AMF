#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "amf/interfaces.hpp"

namespace amf {

class UserPlaneModule {
public:
    explicit UserPlaneModule(IN3Interface* n3);

    bool forward_n3_user_plane(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);

private:
    enum class N3MessageType {
        TunnelEstablish,
        UplinkTpdu,
        DownlinkTpdu,
        TunnelRelease,
        Unknown,
    };

    enum class N3TunnelState {
        Released,
        Established,
    };

    struct N3TunnelContext {
        std::uint32_t teid {0};
        std::uint8_t qfi {9};
        std::uint32_t ul_seq {0};
        std::uint32_t dl_seq {0};
        N3TunnelState state {N3TunnelState::Released};
    };

    struct ParsedN3Message {
        N3MessageType type {N3MessageType::Unknown};
        std::unordered_map<std::string, std::string> ies;
    };

    ParsedN3Message parse_n3_message(const std::string& payload) const;
    bool validate_n3_message(const ParsedN3Message& message, const N3TunnelContext& context, std::string& error_cause) const;
    bool send_n3_error_indication(const std::string& imsi, const std::string& cause) const;
    std::string build_gtpu_pdu(const ParsedN3Message& message, const N3TunnelContext& context, const std::string& imsi) const;
    static std::uint32_t derive_default_teid(const std::string& imsi);
    static std::unordered_map<std::string, std::string> parse_kv_payload(const std::string& payload, char delimiter);

    IN3Interface* n3_;
    std::unordered_map<std::string, N3TunnelContext> n3_tunnels_;
};

}  // namespace amf
