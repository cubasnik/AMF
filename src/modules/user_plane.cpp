#include "amf/modules/user_plane.hpp"

#include <sstream>

namespace amf {
namespace {

std::string normalize_gtpu_message(const std::string& value) {
    if (value == "TunnelEstablish" || value == "tunnel-establish") {
        return "TunnelEstablish";
    }
    if (value == "UplinkTpdu" || value == "uplink-tpdu" || value == "TPDU-UL") {
        return "UplinkTpdu";
    }
    if (value == "DownlinkTpdu" || value == "downlink-tpdu" || value == "TPDU-DL") {
        return "DownlinkTpdu";
    }
    if (value == "TunnelRelease" || value == "tunnel-release") {
        return "TunnelRelease";
    }
    return value;
}

bool parse_u32_auto(const std::string& value, std::uint32_t& out) {
    if (value.empty()) {
        return false;
    }

    try {
        std::size_t idx = 0;
        const unsigned long parsed = std::stoul(value, &idx, 0);
        if (idx != value.size()) {
            return false;
        }
        out = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

UserPlaneModule::UserPlaneModule(IN3Interface* n3)
    : n3_(n3) {}

bool UserPlaneModule::forward_n3_user_plane(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n3_ == nullptr) {
        return false;
    }

    auto& context = n3_tunnels_[imsi];
    ParsedN3Message message = parse_n3_message(payload);

    if (message.type == N3MessageType::Unknown) {
        return send_n3_error_indication(imsi, "unsupported-message");
    }

    if ((message.type == N3MessageType::UplinkTpdu || message.type == N3MessageType::DownlinkTpdu)
        && context.state == N3TunnelState::Released
        && message.ies.count("legacy") != 0U) {
        context.teid = derive_default_teid(imsi);
        context.qfi = 9;
        context.ul_seq = 0;
        context.dl_seq = 0;
        context.state = N3TunnelState::Established;
    }

    if (message.legacy && message.type == N3MessageType::TunnelEstablish && message.ies.count("teid") == 0U) {
        message.ies["teid"] = std::to_string(derive_default_teid(imsi));
    }
    if (message.legacy && message.type == N3MessageType::TunnelEstablish && message.ies.count("qfi") == 0U) {
        message.ies["qfi"] = "9";
    }

    std::string validation_error;
    if (!validate_n3_message(message, context, validation_error)) {
        return send_n3_error_indication(imsi, validation_error);
    }

    if (message.type == N3MessageType::TunnelEstablish) {
        parse_u32_auto(message.ies.at("teid"), context.teid);
        std::uint32_t qfi = 0;
        parse_u32_auto(message.ies.at("qfi"), qfi);
        context.qfi = static_cast<std::uint8_t>(qfi);
        context.ul_seq = 0;
        context.dl_seq = 0;
        context.state = N3TunnelState::Established;
    } else if (message.type == N3MessageType::UplinkTpdu) {
        ++context.ul_seq;
    } else if (message.type == N3MessageType::DownlinkTpdu) {
        ++context.dl_seq;
    } else if (message.type == N3MessageType::TunnelRelease) {
        context.state = N3TunnelState::Released;
        context.ul_seq = 0;
        context.dl_seq = 0;
    }

    n3_->forward_user_plane(imsi, build_gtpu_pdu(message, context, imsi));
    return true;
}

UserPlaneModule::ParsedN3Message UserPlaneModule::parse_n3_message(const std::string& payload) const {
    ParsedN3Message parsed;

    if (payload == "tunnel-establish") {
        parsed.legacy = true;
        parsed.type = N3MessageType::TunnelEstablish;
        return parsed;
    }
    if (payload == "tunnel-release") {
        parsed.legacy = true;
        parsed.type = N3MessageType::TunnelRelease;
        return parsed;
    }
    if (payload == "uplink-data") {
        parsed.legacy = true;
        parsed.type = N3MessageType::UplinkTpdu;
        parsed.ies["payload"] = "sample-ul-data";
        parsed.ies["legacy"] = "1";
        return parsed;
    }
    if (payload == "downlink-data") {
        parsed.legacy = true;
        parsed.type = N3MessageType::DownlinkTpdu;
        parsed.ies["payload"] = "sample-dl-data";
        parsed.ies["legacy"] = "1";
        return parsed;
    }

    if (payload.rfind("GTPU|", 0) == 0) {
        parsed.ies = parse_kv_payload(payload.substr(5), '|');
        const auto it = parsed.ies.find("message");
        if (it == parsed.ies.end()) {
            parsed.type = N3MessageType::Unknown;
            return parsed;
        }

        const std::string message = normalize_gtpu_message(it->second);
        if (message == "TunnelEstablish") {
            parsed.type = N3MessageType::TunnelEstablish;
        } else if (message == "UplinkTpdu") {
            parsed.type = N3MessageType::UplinkTpdu;
        } else if (message == "DownlinkTpdu") {
            parsed.type = N3MessageType::DownlinkTpdu;
        } else if (message == "TunnelRelease") {
            parsed.type = N3MessageType::TunnelRelease;
        } else {
            parsed.type = N3MessageType::Unknown;
        }
        return parsed;
    }

    parsed.type = N3MessageType::UplinkTpdu;
    parsed.legacy = true;
    parsed.ies["payload"] = payload;
    parsed.ies["legacy"] = "1";
    return parsed;
}

bool UserPlaneModule::validate_n3_message(const ParsedN3Message& message, const N3TunnelContext& context, std::string& error_cause) const {
    if (message.type == N3MessageType::TunnelEstablish) {
        if (context.state == N3TunnelState::Established) {
            error_cause = "duplicate-tunnel-establish";
            return false;
        }
        if (message.ies.count("teid") == 0U || message.ies.count("qfi") == 0U) {
            error_cause = "missing-mandatory-ie";
            return false;
        }

        std::uint32_t teid = 0;
        if (!parse_u32_auto(message.ies.at("teid"), teid) || teid == 0U) {
            error_cause = "invalid-teid";
            return false;
        }

        std::uint32_t qfi = 0;
        if (!parse_u32_auto(message.ies.at("qfi"), qfi) || qfi == 0U || qfi > 63U) {
            error_cause = "invalid-qfi";
            return false;
        }

        return true;
    }

    if (message.type == N3MessageType::UplinkTpdu || message.type == N3MessageType::DownlinkTpdu) {
        if (context.state != N3TunnelState::Established) {
            error_cause = "no-tunnel-context";
            return false;
        }

        if (message.ies.count("payload") == 0U || message.ies.at("payload").empty()) {
            error_cause = "missing-mandatory-ie";
            return false;
        }

        if (message.ies.count("teid") != 0U) {
            std::uint32_t teid = 0;
            if (!parse_u32_auto(message.ies.at("teid"), teid) || teid != context.teid) {
                error_cause = "teid-mismatch";
                return false;
            }
        }

        return true;
    }

    if (message.type == N3MessageType::TunnelRelease) {
        if (context.state != N3TunnelState::Established) {
            error_cause = "no-tunnel-context";
            return false;
        }

        if (message.ies.count("teid") != 0U) {
            std::uint32_t teid = 0;
            if (!parse_u32_auto(message.ies.at("teid"), teid) || teid != context.teid) {
                error_cause = "teid-mismatch";
                return false;
            }
        }

        return true;
    }

    error_cause = "unsupported-message";
    return false;
}

bool UserPlaneModule::send_n3_error_indication(const std::string& imsi, const std::string& cause) const {
    std::ostringstream pdu;
    pdu << "GTPU/1\n";
    pdu << "message=ErrorIndication\n";
    pdu << "imsi=" << imsi << "\n";
    pdu << "cause=" << cause << "\n";
    n3_->forward_user_plane(imsi, pdu.str());
    return false;
}

std::string UserPlaneModule::build_gtpu_pdu(const ParsedN3Message& message, const N3TunnelContext& context, const std::string& imsi) const {
    std::string message_name = "TPDU";
    std::uint32_t message_type = 255;
    std::uint32_t sequence = 0;
    std::string payload_text;
    std::string direction;

    if (message.type == N3MessageType::TunnelEstablish) {
        message_name = "TunnelEstablishAck";
        message_type = 1;
    } else if (message.type == N3MessageType::TunnelRelease) {
        message_name = "TunnelReleaseAck";
        message_type = 2;
    } else if (message.type == N3MessageType::UplinkTpdu) {
        direction = "UL";
        sequence = context.ul_seq;
        payload_text = message.ies.at("payload");
    } else if (message.type == N3MessageType::DownlinkTpdu) {
        direction = "DL";
        sequence = context.dl_seq;
        payload_text = message.ies.at("payload");
    }

    const std::uint32_t header_length = 12;
    const std::uint32_t message_length = header_length + static_cast<std::uint32_t>(payload_text.size());

    std::ostringstream pdu;
    pdu << "GTPU/1\n";
    pdu << "message=" << message_name << "\n";
    pdu << "header.version=1\n";
    pdu << "header.pt=1\n";
    pdu << "header.e=0\n";
    pdu << "header.s=1\n";
    pdu << "header.pn=0\n";
    pdu << "header.message-type=" << message_type << "\n";
    pdu << "header.length=" << message_length << "\n";
    pdu << "header.seq=" << sequence << "\n";

    pdu << "imsi=" << imsi << "\n";
    pdu << "teid=" << context.teid << "\n";
    pdu << "qfi=" << static_cast<unsigned>(context.qfi) << "\n";

    if (!direction.empty()) {
        pdu << "dir=" << direction << "\n";
        pdu << "seq=" << sequence << "\n";
        pdu << "payload=" << payload_text << "\n";
    }

    return pdu.str();
}

std::uint32_t UserPlaneModule::derive_default_teid(const std::string& imsi) {
    std::uint32_t h = 2166136261U;
    for (const unsigned char ch : imsi) {
        h ^= static_cast<std::uint32_t>(ch);
        h *= 16777619U;
    }

    if (h == 0U) {
        h = 1U;
    }
    return h;
}

std::unordered_map<std::string, std::string> UserPlaneModule::parse_kv_payload(const std::string& payload, char delimiter) {
    std::unordered_map<std::string, std::string> out;
    std::size_t offset = 0;
    while (offset <= payload.size()) {
        const std::size_t next = payload.find(delimiter, offset);
        const std::string token = payload.substr(offset, next == std::string::npos ? std::string::npos : next - offset);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0U) {
            out[token.substr(0, eq)] = token.substr(eq + 1);
        }
        if (next == std::string::npos) {
            break;
        }
        offset = next + 1;
    }

    return out;
}

}  // namespace amf
