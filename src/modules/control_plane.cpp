#include "amf/modules/control_plane.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace amf {

ControlPlaneModule::ControlPlaneModule(
    IN1Interface* n1,
    IN2Interface* n2,
    ISbiInterface* sbi,
    IN8Interface* n8,
    IN11Interface* n11,
    IN12Interface* n12,
    IN15Interface* n15,
    IN22Interface* n22)
    : n1_(n1), n2_(n2), sbi_(sbi), n8_(n8), n11_(n11), n12_(n12), n15_(n15), n22_(n22) {}

bool ControlPlaneModule::send_n1_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n1_ == nullptr) {
        return false;
    }

    ParsedNasMessage msg = parse_n1_message(payload);
    const auto now = std::chrono::steady_clock::now();
    auto& state = n1_procedure_state_[imsi];
    auto& ctx = n1_security_context_[imsi];

    if (ctx.k_amf == 0) {
        ctx.k_amf = fnv1a32(imsi + ":k-amf");
    }

    if ((state == N1ProcedureState::AuthenticationPending || state == N1ProcedureState::SecurityModePending)
        && ctx.state_entered_at.time_since_epoch().count() != 0
        && now - ctx.state_entered_at > kN1ProcedureTimeout) {
        state = N1ProcedureState::Deregistered;
        ctx.authenticated = false;
        ctx.security_mode_complete = false;
        ctx.rand.clear();
        ctx.autn.clear();
        ctx.state_entered_at = now;
        return send_n1_service_reject(imsi, ctx, "timer-expiry");
    }

    switch (msg.type) {
        case N1NasMessageType::RegistrationRequest: {
            if (state == N1ProcedureState::AuthenticationPending || state == N1ProcedureState::SecurityModePending) {
                return send_n1_service_reject(imsi, ctx, "procedure-collision");
            }

            ctx.rand = derive_rand(imsi, ctx.dl_count + 1);
            ctx.autn = derive_autn(imsi, ctx.rand, ctx.k_amf);
            ctx.authenticated = false;
            ctx.security_mode_complete = false;
            state = N1ProcedureState::AuthenticationPending;
            ctx.state_entered_at = now;

            std::unordered_map<std::string, std::string> fields;
            fields["rand"] = ctx.rand;
            fields["autn"] = ctx.autn;
            const std::string pdu = build_nas_downlink_pdu("AuthenticationRequest", fields, ++ctx.dl_count, ctx.k_amf, false);
            n1_->send_nas_to_ue(imsi, pdu);
            return true;
        }

        case N1NasMessageType::AuthenticationResponse: {
            if (state != N1ProcedureState::AuthenticationPending) {
                return send_n1_service_reject(imsi, ctx, "unexpected-auth-response");
            }

            if (msg.res_star.empty() || msg.res_star != expected_res_star(imsi, ctx.rand)) {
                return send_n1_service_reject(imsi, ctx, "auth-failed");
            }

            ctx.authenticated = true;
            state = N1ProcedureState::SecurityModePending;
            ctx.state_entered_at = now;

            std::unordered_map<std::string, std::string> fields;
            fields["algorithm"] = "128-5G-IA2";
            fields["ksi"] = "1";
            const std::string pdu = build_nas_downlink_pdu("SecurityModeCommand", fields, ++ctx.dl_count, ctx.k_amf, true);
            n1_->send_nas_to_ue(imsi, pdu);
            return true;
        }

        case N1NasMessageType::SecurityModeComplete: {
            if (state != N1ProcedureState::SecurityModePending || !ctx.authenticated) {
                return send_n1_service_reject(imsi, ctx, "unexpected-security-mode-complete");
            }

            ctx.security_mode_complete = true;
            state = N1ProcedureState::Registered;
            ctx.state_entered_at = now;

            std::unordered_map<std::string, std::string> fields;
            fields["result"] = "accepted";
            fields["guti"] = "guti-" + imsi.substr(imsi.size() > 8 ? imsi.size() - 8 : 0);
            const std::string pdu = build_nas_downlink_pdu("RegistrationAccept", fields, ++ctx.dl_count, ctx.k_amf, true);
            n1_->send_nas_to_ue(imsi, pdu);
            return true;
        }

        case N1NasMessageType::DeregistrationRequest: {
            state = N1ProcedureState::Deregistered;
            ctx.authenticated = false;
            ctx.security_mode_complete = false;
            ctx.rand.clear();
            ctx.autn.clear();
            ctx.state_entered_at = now;

            std::unordered_map<std::string, std::string> fields;
            fields["result"] = "accepted";
            const std::string pdu = build_nas_downlink_pdu("DeregistrationAccept", fields, ++ctx.dl_count, ctx.k_amf, true);
            n1_->send_nas_to_ue(imsi, pdu);
            return true;
        }

        case N1NasMessageType::Unknown:
        default:
            return send_n1_service_reject(imsi, ctx, "unsupported-nas-message");
    }
}

bool ControlPlaneModule::send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n2_ == nullptr) {
        return false;
    }

    auto& ctx = n2_ue_context_[imsi];
    ParsedN2Message message = parse_n2_message(imsi, payload);

    if (message.procedure == N2ProcedureType::InitialUEMessage) {
        if (ctx.state == N2UeContextState::InitialUeAssociated || ctx.state == N2UeContextState::ContextSetupComplete) {
            return send_n2_error_indication(imsi, "duplicate-initial-ue-message");
        }

        if (message.ies["ran-ue-ngap-id"].empty()) {
            message.ies["ran-ue-ngap-id"] = next_ran_ue_ngap_id(imsi);
        }
        if (message.ies["user-location-tai"].empty()) {
            message.ies["user-location-tai"] = mcc_ + "-" + mnc_;
        }
    }

    if (message.procedure == N2ProcedureType::InitialContextSetupRequest || message.procedure == N2ProcedureType::UEContextReleaseCommand) {
        if (message.ies["amf-ue-ngap-id"].empty() && ctx.amf_ue_ngap_id != 0) {
            message.ies["amf-ue-ngap-id"] = to_hex8(ctx.amf_ue_ngap_id);
        }
        if (message.ies["ran-ue-ngap-id"].empty() && !ctx.ran_ue_ngap_id.empty()) {
            message.ies["ran-ue-ngap-id"] = ctx.ran_ue_ngap_id;
        }
    }

    if (message.procedure == N2ProcedureType::Paging) {
        if (message.ies["ue-paging-id"].empty()) {
            message.ies["ue-paging-id"] = imsi;
        }
        if (message.ies["tai"].empty()) {
            message.ies["tai"] = ctx.last_tai.empty() ? (mcc_ + "-" + mnc_) : ctx.last_tai;
        }
        if (message.ies["paging-priority"].empty()) {
            message.ies["paging-priority"] = "normal";
        }
    }

    std::string validation_error;
    if (!validate_n2_message(message, ctx, validation_error)) {
        return send_n2_error_indication(imsi, validation_error);
    }

    switch (message.procedure) {
        case N2ProcedureType::InitialUEMessage:
            ctx.amf_ue_ngap_id = static_cast<std::uint32_t>(std::stoul(next_amf_ue_ngap_id(imsi), nullptr, 16));
            ctx.ran_ue_ngap_id = message.ies["ran-ue-ngap-id"];
            ctx.last_tai = message.ies["user-location-tai"];
            ctx.state = N2UeContextState::InitialUeAssociated;
            break;
        case N2ProcedureType::InitialContextSetupRequest:
            ctx.state = N2UeContextState::ContextSetupComplete;
            break;
        case N2ProcedureType::UEContextReleaseCommand:
            ctx.state = N2UeContextState::Released;
            break;
        case N2ProcedureType::Paging:
            ctx.last_tai = message.ies["tai"];
            break;
        case N2ProcedureType::Unknown:
        default:
            return send_n2_error_indication(imsi, "unsupported-procedure");
    }

    n2_->deliver_nas(imsi, build_ngap_pdu(message));
    return true;
}

bool ControlPlaneModule::notify_sbi(bool operational, const std::string& service_name, const std::string& payload) {
    if (!operational || sbi_ == nullptr) {
        return false;
    }

    return sbi_->notify_service(service_name, payload);
}

bool ControlPlaneModule::query_n8_subscription(bool operational, bool ue_exists, const std::string& imsi, const std::string& request) {
    if (!operational || !ue_exists || n8_ == nullptr || request.empty()) {
        return false;
    }

    auto& ctx = n8_subscription_context_[imsi];
    ParsedN8Message message = parse_n8_message(request);
    if (message.procedure == N8ProcedureType::Unknown) {
        return send_n8_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy && message.fields["dataset"].empty()) {
        if (message.procedure == N8ProcedureType::GetAmData) {
            message.fields["dataset"] = "am-data";
        } else if (message.procedure == N8ProcedureType::GetSmfSelectionData) {
            message.fields["dataset"] = "smf-selection-data";
        } else if (message.procedure == N8ProcedureType::GetUeContextInSmfData) {
            message.fields["dataset"] = "ue-context-in-smf-data";
        }
    }

    std::string error;
    if (!validate_n8_message(message, ctx, error)) {
        return send_n8_error_indication(imsi, error);
    }

    ++ctx.request_seq;
    ctx.last_dataset = message.fields["dataset"];
    ctx.available = true;
    n8_->query_subscription(imsi, build_n8_sbi_request(imsi, message, ctx));
    return true;
}

bool ControlPlaneModule::manage_n11_pdu_session(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation) {
    if (!operational || !ue_exists || n11_ == nullptr || operation.empty()) {
        return false;
    }

    auto& ctx = n11_session_context_[imsi];
    ParsedN11Message message = parse_n11_message(operation);
    if (message.procedure == N11ProcedureType::Unknown) {
        return send_n11_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy) {
        if (message.procedure == N11ProcedureType::Create) {
            if (message.fields["pdu-session-id"].empty()) {
                message.fields["pdu-session-id"] = "10";
            }
            if (message.fields["dnn"].empty()) {
                message.fields["dnn"] = "internet";
            }
            if (message.fields["snssai"].empty()) {
                message.fields["snssai"] = "1-010203";
            }
        } else {
            if (message.fields["pdu-session-id"].empty() && ctx.pdu_session_id != 0) {
                message.fields["pdu-session-id"] = std::to_string(ctx.pdu_session_id);
            }
        }
    }

    std::string error;
    if (!validate_n11_message(message, ctx, error)) {
        return send_n11_error_indication(imsi, error);
    }

    std::uint32_t pdu_session_id = 0;
    if (try_parse_u32(message.fields["pdu-session-id"], pdu_session_id)) {
        ctx.pdu_session_id = pdu_session_id;
    }

    if (message.procedure == N11ProcedureType::Create) {
        ctx.dnn = message.fields["dnn"];
        ctx.snssai = message.fields["snssai"];
        ctx.state = N11SessionState::Active;
    } else if (message.procedure == N11ProcedureType::Modify) {
        if (!message.fields["dnn"].empty()) {
            ctx.dnn = message.fields["dnn"];
        }
        if (!message.fields["snssai"].empty()) {
            ctx.snssai = message.fields["snssai"];
        }
        ctx.state = N11SessionState::Active;
    } else if (message.procedure == N11ProcedureType::Release) {
        ctx.state = N11SessionState::None;
    }

    ++ctx.sequence;
    n11_->manage_pdu_session(imsi, build_n11_sbi_request(imsi, message, ctx));
    return true;
}

bool ControlPlaneModule::authenticate_n12(bool operational, bool ue_exists, const std::string& imsi, const std::string& request) {
    if (!operational || !ue_exists || n12_ == nullptr || request.empty()) {
        return false;
    }

    auto& ctx = n12_auth_context_[imsi];
    ParsedN12Message message = parse_n12_message(request);
    if (message.procedure == N12ProcedureType::Unknown) {
        return send_n12_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy) {
        if (message.procedure == N12ProcedureType::AuthRequest) {
            if (message.fields["serving-network-name"].empty()) {
                message.fields["serving-network-name"] = "5G:mnc" + mnc_ + ".mcc" + mcc_ + ".3gppnetwork.org";
            }
        } else if (message.procedure == N12ProcedureType::AuthResponse) {
            if (message.fields["res-star"].empty()) {
                message.fields["res-star"] = ctx.xres_star;
            }
        }
    }

    std::string error;
    if (!validate_n12_message(message, ctx, error)) {
        return send_n12_error_indication(imsi, error);
    }

    if (message.procedure == N12ProcedureType::AuthRequest) {
        ++ctx.request_seq;
        ctx.rand = derive_rand(imsi, ctx.request_seq);
        ctx.autn = derive_autn(imsi, ctx.rand, fnv1a32(imsi + ":n12"));
        ctx.xres_star = expected_res_star(imsi, ctx.rand);
        ctx.auth_method = "5g-aka";
        ctx.state = N12AuthState::ChallengeSent;
    } else if (message.procedure == N12ProcedureType::AuthResponse) {
        ctx.state = N12AuthState::Authenticated;
        ++ctx.request_seq;
    }

    n12_->authenticate_ue(imsi, build_n12_sbi_request(imsi, message, ctx));
    return true;
}

bool ControlPlaneModule::query_n15_policy(bool operational, bool ue_exists, const std::string& imsi, const std::string& request) {
    if (!operational || !ue_exists || n15_ == nullptr || request.empty()) {
        return false;
    }

    auto& ctx = n15_policy_context_[imsi];
    ParsedN15Message message = parse_n15_message(request);
    if (message.procedure == N15ProcedureType::Unknown) {
        return send_n15_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy) {
        if (message.procedure == N15ProcedureType::GetAmPolicy) {
            if (message.fields["policy-type"].empty()) {
                message.fields["policy-type"] = "am-policy";
            }
        } else if (message.procedure == N15ProcedureType::GetSmPolicy) {
            if (message.fields["policy-type"].empty()) {
                message.fields["policy-type"] = "sm-policy";
            }
        } else if (message.procedure == N15ProcedureType::UpdatePolicyAssociation) {
            if (message.fields["association-id"].empty()) {
                message.fields["association-id"] = ctx.association_id;
            }
            if (message.fields["policy-rule"].empty()) {
                message.fields["policy-rule"] = "allow-default";
            }
        }
    }

    std::string error;
    if (!validate_n15_message(message, ctx, error)) {
        return send_n15_error_indication(imsi, error);
    }

    ++ctx.request_seq;
    if (message.procedure == N15ProcedureType::GetAmPolicy || message.procedure == N15ProcedureType::GetSmPolicy) {
        if (ctx.association_id.empty()) {
            ctx.association_id = "pcf-" + to_hex8(fnv1a32(imsi + ":pcf-assoc"));
        }
        ctx.last_policy_type = message.fields["policy-type"];
        if (message.fields.count("snssai") != 0U && !message.fields["snssai"].empty()) {
            ctx.last_snssai = message.fields["snssai"];
        }
        ctx.state = N15PolicyState::Associated;
    } else if (message.procedure == N15ProcedureType::UpdatePolicyAssociation) {
        ctx.last_policy_type = message.fields["policy-type"].empty() ? ctx.last_policy_type : message.fields["policy-type"];
        if (message.fields.count("snssai") != 0U && !message.fields["snssai"].empty()) {
            ctx.last_snssai = message.fields["snssai"];
        }
        ctx.state = N15PolicyState::Associated;
    }

    n15_->query_policy(imsi, build_n15_sbi_request(imsi, message, ctx));
    return true;
}

bool ControlPlaneModule::select_n22_slice(bool operational, bool ue_exists, const std::string& imsi, const std::string& request) {
    if (!operational || !ue_exists || n22_ == nullptr || request.empty()) {
        return false;
    }

    auto& ctx = n22_selection_context_[imsi];
    ParsedN22Message message = parse_n22_message(request);
    if (message.procedure == N22ProcedureType::Unknown) {
        return send_n22_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy) {
        if (message.procedure == N22ProcedureType::SelectSlice || message.procedure == N22ProcedureType::UpdateSelection) {
            if (message.fields["allowed-snssai"].empty()) {
                message.fields["allowed-snssai"] = ctx.allowed_snssai.empty() ? "1-010203,1-112233" : ctx.allowed_snssai;
            }
            if (message.fields["requested-snssai"].empty() && !ctx.selected_snssai.empty()) {
                message.fields["requested-snssai"] = ctx.selected_snssai;
            }
        }
        if (message.procedure == N22ProcedureType::ReleaseSelection && message.fields["selection-id"].empty()) {
            message.fields["selection-id"] = ctx.selection_id;
        }
    }

    std::string error;
    if (!validate_n22_message(message, ctx, error)) {
        return send_n22_error_indication(imsi, error);
    }

    std::string selection_result = "selected";
    N22SelectionContext request_context = ctx;
    ++request_context.request_seq;

    if (message.procedure == N22ProcedureType::SelectSlice || message.procedure == N22ProcedureType::UpdateSelection) {
        const std::string requested = message.fields.at("requested-snssai");
        const std::string allowed = message.fields.at("allowed-snssai");
        const bool direct_match = list_contains_csv_token(allowed, requested);
        const std::string effective_snssai = direct_match ? requested : message.fields.at("fallback-snssai");
        selection_result = direct_match ? "selected" : "fallback";

        if (request_context.selection_id.empty()) {
            request_context.selection_id = "nssf-" + to_hex8(fnv1a32(imsi + ":n22-selection"));
        }
        request_context.selected_snssai = effective_snssai;
        request_context.allowed_snssai = allowed;
        request_context.state = N22SelectionState::Selected;
        ctx = request_context;
    } else {
        request_context.state = N22SelectionState::None;
        selection_result = "released";
    }

    n22_->select_network_slice(imsi, build_n22_sbi_request(imsi, message, request_context, selection_result));

    if (message.procedure == N22ProcedureType::ReleaseSelection) {
        ctx = {};
    }

    return true;
}

bool ControlPlaneModule::set_plmn(const std::string& mcc, const std::string& mnc) {
    if (mcc.size() != 3 || mnc.size() != 2 || !is_all_digits(mcc) || !is_all_digits(mnc)) {
        return false;
    }

    mcc_ = mcc;
    mnc_ = mnc;
    return true;
}

const std::string& ControlPlaneModule::mcc() const {
    return mcc_;
}

const std::string& ControlPlaneModule::mnc() const {
    return mnc_;
}

bool ControlPlaneModule::is_all_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

ControlPlaneModule::ParsedNasMessage ControlPlaneModule::parse_n1_message(const std::string& payload) {
    ParsedNasMessage out;
    out.raw = payload;

    if (payload.empty()) {
        return out;
    }

    if (payload == "registration-request") {
        out.type = N1NasMessageType::RegistrationRequest;
        return out;
    }
    if (payload == "authentication-response") {
        out.type = N1NasMessageType::AuthenticationResponse;
        return out;
    }
    if (payload == "security-mode-complete") {
        out.type = N1NasMessageType::SecurityModeComplete;
        return out;
    }
    if (payload == "deregistration-request") {
        out.type = N1NasMessageType::DeregistrationRequest;
        return out;
    }

    const auto kv = parse_nas_kv(payload);
    const auto type_it = kv.find("message");
    if (type_it == kv.end()) {
        return out;
    }

    const std::string message = trim_copy(type_it->second);
    if (message == "RegistrationRequest") {
        out.type = N1NasMessageType::RegistrationRequest;
    } else if (message == "AuthenticationResponse") {
        out.type = N1NasMessageType::AuthenticationResponse;
    } else if (message == "SecurityModeComplete") {
        out.type = N1NasMessageType::SecurityModeComplete;
    } else if (message == "DeregistrationRequest") {
        out.type = N1NasMessageType::DeregistrationRequest;
    } else {
        out.type = N1NasMessageType::Unknown;
    }

    const auto res_it = kv.find("res*");
    if (res_it != kv.end()) {
        out.res_star = trim_copy(res_it->second);
    }

    return out;
}

std::unordered_map<std::string, std::string> ControlPlaneModule::parse_nas_kv(const std::string& payload) {
    std::unordered_map<std::string, std::string> out;
    if (payload.rfind("NAS5G|", 0) != 0) {
        return out;
    }

    std::size_t start = 6;
    while (start < payload.size()) {
        const std::size_t next = payload.find('|', start);
        const std::string token = payload.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            const std::string key = trim_copy(token.substr(0, eq));
            const std::string value = trim_copy(token.substr(eq + 1));
            out[key] = value;
        }

        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    return out;
}

std::string ControlPlaneModule::to_hex8(std::uint32_t value) {
    std::ostringstream oss;
    oss.setf(std::ios::hex, std::ios::basefield);
    oss.setf(std::ios::uppercase);
    oss.fill('0');
    oss.width(8);
    oss << value;
    return oss.str();
}

std::uint32_t ControlPlaneModule::fnv1a32(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

std::string ControlPlaneModule::derive_rand(const std::string& imsi, std::uint32_t dl_count) {
    return to_hex8(fnv1a32(imsi + ":rand:" + std::to_string(dl_count)));
}

std::string ControlPlaneModule::derive_autn(const std::string& imsi, const std::string& rand, std::uint32_t k_amf) {
    return to_hex8(fnv1a32(imsi + ":autn:" + rand + ':' + std::to_string(k_amf)));
}

std::string ControlPlaneModule::expected_res_star(const std::string& imsi, const std::string& rand) {
    return to_hex8(fnv1a32(imsi + ":res*:" + rand));
}

std::string ControlPlaneModule::build_nas_downlink_pdu(
    const std::string& message,
    const std::unordered_map<std::string, std::string>& fields,
    std::uint32_t dl_count,
    std::uint32_t k_amf,
    bool integrity_protected) {
    std::ostringstream oss;
    oss << "NAS5G|dir=DL|message=" << message << "|count=" << dl_count;
    std::vector<std::string> keys;
    keys.reserve(fields.size());
    for (const auto& kv : fields) {
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto it = fields.find(key);
        if (it == fields.end()) {
            continue;
        }
        oss << '|' << it->first << '=' << it->second;
    }

    if (integrity_protected) {
        const std::string pdu_without_mac = oss.str();
        oss << "|mac=" << compute_nas_mac(pdu_without_mac, k_amf, dl_count);
    }

    return oss.str();
}

ControlPlaneModule::ParsedN2Message ControlPlaneModule::parse_n2_message(const std::string& imsi, const std::string& payload) {
    ParsedN2Message out;
    out.raw = payload;

    if (payload == "registration-request") {
        out.procedure = N2ProcedureType::InitialUEMessage;
        out.ies["nas-pdu"] = "NAS5G|dir=UL|message=RegistrationRequest";
        out.ies["rrc-establishment-cause"] = "mo-Signalling";
        out.ies["ran-ue-ngap-id"] = next_ran_ue_ngap_id(imsi);
        out.ies["user-location-tai"] = "250-03";
        return out;
    }

    if (payload == "initial-context-setup") {
        out.procedure = N2ProcedureType::InitialContextSetupRequest;
        out.ies["pdu-session-id"] = "10";
        out.ies["qos-flow-id"] = "5";
        out.ies["allowed-snssai"] = "1-010203";
        return out;
    }

    if (payload == "ue-context-release") {
        out.procedure = N2ProcedureType::UEContextReleaseCommand;
        out.ies["cause"] = "user-inactivity";
        return out;
    }

    if (payload == "paging") {
        out.procedure = N2ProcedureType::Paging;
        out.ies["ue-paging-id"] = imsi;
        out.ies["tai"] = "250-03";
        out.ies["paging-priority"] = "normal";
        return out;
    }

    if (payload.rfind("NGAP|", 0) != 0) {
        return out;
    }

    std::size_t start = 5;
    while (start < payload.size()) {
        const std::size_t next = payload.find('|', start);
        const std::string token = payload.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.ies[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const auto proc_it = out.ies.find("procedure");
    if (proc_it == out.ies.end()) {
        out.procedure = N2ProcedureType::Unknown;
        return out;
    }

    const std::string procedure = normalize_ngap_procedure(proc_it->second);
    if (procedure == "InitialUEMessage") {
        out.procedure = N2ProcedureType::InitialUEMessage;
    } else if (procedure == "InitialContextSetupRequest") {
        out.procedure = N2ProcedureType::InitialContextSetupRequest;
    } else if (procedure == "UEContextReleaseCommand") {
        out.procedure = N2ProcedureType::UEContextReleaseCommand;
    } else if (procedure == "Paging") {
        out.procedure = N2ProcedureType::Paging;
    }

    return out;
}

bool ControlPlaneModule::validate_n2_message(const ParsedN2Message& message, const N2UeContext& ctx, std::string& error) {
    const auto require_non_empty = [&](const char* key) {
        const auto it = message.ies.find(key);
        return it != message.ies.end() && !trim_copy(it->second).empty();
    };

    switch (message.procedure) {
        case N2ProcedureType::InitialUEMessage:
            if (!require_non_empty("ran-ue-ngap-id") || !require_non_empty("nas-pdu")
                || !require_non_empty("rrc-establishment-cause") || !require_non_empty("user-location-tai")) {
                error = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N2ProcedureType::InitialContextSetupRequest:
            if (ctx.state != N2UeContextState::InitialUeAssociated && ctx.state != N2UeContextState::ContextSetupComplete) {
                error = "no-ue-context";
                return false;
            }
            if (!require_non_empty("amf-ue-ngap-id") || !require_non_empty("ran-ue-ngap-id")
                || !require_non_empty("pdu-session-id") || !require_non_empty("qos-flow-id")
                || !require_non_empty("allowed-snssai")) {
                error = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N2ProcedureType::UEContextReleaseCommand:
            if (ctx.state != N2UeContextState::InitialUeAssociated && ctx.state != N2UeContextState::ContextSetupComplete) {
                error = "no-ue-context";
                return false;
            }
            if (!require_non_empty("amf-ue-ngap-id") || !require_non_empty("ran-ue-ngap-id") || !require_non_empty("cause")) {
                error = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N2ProcedureType::Paging:
            if (!require_non_empty("ue-paging-id") || !require_non_empty("tai") || !require_non_empty("paging-priority")) {
                error = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N2ProcedureType::Unknown:
        default:
            error = "unsupported-procedure";
            return false;
    }
}

std::string ControlPlaneModule::build_ngap_pdu(const ParsedN2Message& message) {
    std::string procedure;
    switch (message.procedure) {
        case N2ProcedureType::InitialUEMessage:
            procedure = "InitialUEMessage";
            break;
        case N2ProcedureType::InitialContextSetupRequest:
            procedure = "InitialContextSetupRequest";
            break;
        case N2ProcedureType::UEContextReleaseCommand:
            procedure = "UEContextReleaseCommand";
            break;
        case N2ProcedureType::Paging:
            procedure = "Paging";
            break;
        default:
            procedure = "Unknown";
            break;
    }

    std::ostringstream pdu;
    pdu << "NGAP/1\n";
    pdu << "procedure=" << procedure << "\n";

    std::vector<std::string> keys;
    keys.reserve(message.ies.size());
    for (const auto& kv : message.ies) {
        if (kv.first == "procedure") {
            continue;
        }
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto it = message.ies.find(key);
        if (it == message.ies.end()) {
            continue;
        }
        pdu << "ie." << it->first << '=' << it->second << "\n";
    }

    return pdu.str();
}

ControlPlaneModule::ParsedN8Message ControlPlaneModule::parse_n8_message(const std::string& request) {
    ParsedN8Message out;
    const std::string trimmed = trim_copy(request);

    if (trimmed == "get-am-data") {
        out.procedure = N8ProcedureType::GetAmData;
        out.legacy = true;
        return out;
    }
    if (trimmed == "get-smf-selection-data") {
        out.procedure = N8ProcedureType::GetSmfSelectionData;
        out.legacy = true;
        return out;
    }
    if (trimmed == "get-ue-context-in-smf-data") {
        out.procedure = N8ProcedureType::GetUeContextInSmfData;
        out.legacy = true;
        return out;
    }

    if (trimmed.rfind("N8SBI|", 0) != 0) {
        out.procedure = N8ProcedureType::Unknown;
        return out;
    }

    std::size_t start = 6;
    while (start < trimmed.size()) {
        const std::size_t next = trimmed.find('|', start);
        const std::string token = trimmed.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.fields[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const std::string procedure = normalize_n8_procedure(out.fields["procedure"]);
    if (procedure == "GetAmData") {
        out.procedure = N8ProcedureType::GetAmData;
    } else if (procedure == "GetSmfSelectionData") {
        out.procedure = N8ProcedureType::GetSmfSelectionData;
    } else if (procedure == "GetUeContextInSmfData") {
        out.procedure = N8ProcedureType::GetUeContextInSmfData;
    }

    return out;
}

bool ControlPlaneModule::validate_n8_message(const ParsedN8Message& message, const N8SubscriptionContext&, std::string& error) {
    const auto it = message.fields.find("dataset");
    if (it == message.fields.end() || trim_copy(it->second).empty()) {
        error = "missing-mandatory-ie";
        return false;
    }
    return true;
}

std::string ControlPlaneModule::build_n8_sbi_request(const std::string& imsi, const ParsedN8Message& message, const N8SubscriptionContext& ctx) {
    std::string procedure = "Unknown";
    if (message.procedure == N8ProcedureType::GetAmData) {
        procedure = "GetAmData";
    } else if (message.procedure == N8ProcedureType::GetSmfSelectionData) {
        procedure = "GetSmfSelectionData";
    } else if (message.procedure == N8ProcedureType::GetUeContextInSmfData) {
        procedure = "GetUeContextInSmfData";
    }

    std::ostringstream req;
    req << "N8SBI/1\n";
    req << "procedure=" << procedure << "\n";
    req << "header.method=GET\n";
    req << "header.path=/nudm-uecm/v1/" << imsi << "/" << message.fields.at("dataset") << "\n";
    req << "header.accept=application/json\n";
    req << "header.sequence=" << ctx.request_seq << "\n";
    req << "ie.dataset=" << message.fields.at("dataset") << "\n";
    return req.str();
}

ControlPlaneModule::ParsedN11Message ControlPlaneModule::parse_n11_message(const std::string& operation) {
    ParsedN11Message out;
    const std::string trimmed = trim_copy(operation);

    if (trimmed == "create") {
        out.procedure = N11ProcedureType::Create;
        out.legacy = true;
        return out;
    }
    if (trimmed == "modify") {
        out.procedure = N11ProcedureType::Modify;
        out.legacy = true;
        return out;
    }
    if (trimmed == "release") {
        out.procedure = N11ProcedureType::Release;
        out.legacy = true;
        return out;
    }

    if (trimmed.rfind("N11SBI|", 0) != 0) {
        out.procedure = N11ProcedureType::Unknown;
        return out;
    }

    std::size_t start = 7;
    while (start < trimmed.size()) {
        const std::size_t next = trimmed.find('|', start);
        const std::string token = trimmed.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.fields[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const std::string procedure = normalize_n11_procedure(out.fields["procedure"]);
    if (procedure == "Create") {
        out.procedure = N11ProcedureType::Create;
    } else if (procedure == "Modify") {
        out.procedure = N11ProcedureType::Modify;
    } else if (procedure == "Release") {
        out.procedure = N11ProcedureType::Release;
    }

    return out;
}

bool ControlPlaneModule::validate_n11_message(const ParsedN11Message& message, const N11SessionContext& ctx, std::string& error) {
    std::uint32_t pdu_session_id = 0;
    const bool has_pdu_id = try_parse_u32(message.fields.count("pdu-session-id") != 0 ? message.fields.at("pdu-session-id") : std::string(), pdu_session_id);

    switch (message.procedure) {
        case N11ProcedureType::Create:
            if (ctx.state == N11SessionState::Active) {
                error = "duplicate-session-create";
                return false;
            }
            if (!has_pdu_id || pdu_session_id == 0 || message.fields.count("dnn") == 0 || trim_copy(message.fields.at("dnn")).empty()
                || message.fields.count("snssai") == 0 || trim_copy(message.fields.at("snssai")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N11ProcedureType::Modify:
            if (ctx.state != N11SessionState::Active) {
                error = "no-session-context";
                return false;
            }
            if (!has_pdu_id || pdu_session_id != ctx.pdu_session_id) {
                error = "session-id-mismatch";
                return false;
            }
            return true;
        case N11ProcedureType::Release:
            if (ctx.state != N11SessionState::Active) {
                error = "no-session-context";
                return false;
            }
            if (!has_pdu_id || pdu_session_id != ctx.pdu_session_id) {
                error = "session-id-mismatch";
                return false;
            }
            return true;
        case N11ProcedureType::Unknown:
        default:
            error = "unsupported-procedure";
            return false;
    }
}

std::string ControlPlaneModule::build_n11_sbi_request(const std::string& imsi, const ParsedN11Message& message, const N11SessionContext& ctx) {
    std::string procedure = "Unknown";
    std::string method = "POST";
    std::string path = "/nsmf-pdusession/v1/sm-contexts";
    if (message.procedure == N11ProcedureType::Create) {
        procedure = "Create";
        method = "POST";
        path = "/nsmf-pdusession/v1/sm-contexts";
    } else if (message.procedure == N11ProcedureType::Modify) {
        procedure = "Modify";
        method = "PATCH";
        path = "/nsmf-pdusession/v1/sm-contexts/" + std::to_string(ctx.pdu_session_id);
    } else if (message.procedure == N11ProcedureType::Release) {
        procedure = "Release";
        method = "DELETE";
        path = "/nsmf-pdusession/v1/sm-contexts/" + std::to_string(ctx.pdu_session_id);
    }

    std::ostringstream req;
    req << "N11SBI/1\n";
    req << "procedure=" << procedure << "\n";
    req << "header.method=" << method << "\n";
    req << "header.path=" << path << "\n";
    req << "header.sequence=" << ctx.sequence << "\n";
    req << "ie.imsi=" << imsi << "\n";
    req << "ie.pdu-session-id=" << ctx.pdu_session_id << "\n";
    req << "ie.dnn=" << ctx.dnn << "\n";
    req << "ie.snssai=" << ctx.snssai << "\n";
    return req.str();
}

ControlPlaneModule::ParsedN12Message ControlPlaneModule::parse_n12_message(const std::string& request) {
    ParsedN12Message out;
    const std::string trimmed = trim_copy(request);

    if (trimmed == "auth-request") {
        out.procedure = N12ProcedureType::AuthRequest;
        out.legacy = true;
        return out;
    }
    if (trimmed == "auth-response") {
        out.procedure = N12ProcedureType::AuthResponse;
        out.legacy = true;
        return out;
    }

    if (trimmed.rfind("N12SBI|", 0) != 0) {
        return out;
    }

    std::size_t start = 7;
    while (start < trimmed.size()) {
        const std::size_t next = trimmed.find('|', start);
        const std::string token = trimmed.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.fields[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const std::string procedure = normalize_n12_procedure(out.fields["procedure"]);
    if (procedure == "AuthRequest") {
        out.procedure = N12ProcedureType::AuthRequest;
    } else if (procedure == "AuthResponse") {
        out.procedure = N12ProcedureType::AuthResponse;
    }

    return out;
}

bool ControlPlaneModule::validate_n12_message(const ParsedN12Message& message, const N12AuthContext& ctx, std::string& error) {
    switch (message.procedure) {
        case N12ProcedureType::AuthRequest:
            if (ctx.state == N12AuthState::ChallengeSent) {
                error = "auth-already-pending";
                return false;
            }
            if (message.fields.count("serving-network-name") == 0 || trim_copy(message.fields.at("serving-network-name")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (message.fields.count("auth-method") != 0U && trim_copy(message.fields.at("auth-method")) != "5g-aka") {
                error = "unsupported-auth-method";
                return false;
            }
            return true;
        case N12ProcedureType::AuthResponse:
            if (ctx.state == N12AuthState::Authenticated) {
                error = "auth-already-complete";
                return false;
            }
            if (ctx.state != N12AuthState::ChallengeSent) {
                error = "no-auth-context";
                return false;
            }
            if (message.fields.count("res-star") == 0 || trim_copy(message.fields.at("res-star")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (trim_copy(message.fields.at("res-star")) != ctx.xres_star) {
                error = "auth-failed";
                return false;
            }
            return true;
        case N12ProcedureType::Unknown:
        default:
            error = "unsupported-procedure";
            return false;
    }
}

std::string ControlPlaneModule::build_n12_sbi_request(const std::string& imsi, const ParsedN12Message& message, const N12AuthContext& ctx) {
    std::ostringstream req;
    req << "N12SBI/1\n";
    if (message.procedure == N12ProcedureType::AuthRequest) {
        req << "procedure=AuthRequest\n";
        req << "header.method=POST\n";
        req << "header.path=/nausf-auth/v1/ue-authentications\n";
        req << "header.sequence=" << ctx.request_seq << "\n";
        req << "ie.imsi=" << imsi << "\n";
        req << "ie.serving-network-name=" << message.fields.at("serving-network-name") << "\n";
        req << "ie.auth-method=" << ctx.auth_method << "\n";
        req << "ie.rand=" << ctx.rand << "\n";
        req << "ie.autn=" << ctx.autn << "\n";
    } else {
        req << "procedure=AuthResponse\n";
        req << "header.method=PUT\n";
        req << "header.path=/nausf-auth/v1/ue-authentications/" << imsi << "\n";
        req << "header.sequence=" << ctx.request_seq << "\n";
        req << "ie.imsi=" << imsi << "\n";
        req << "ie.res-star=" << message.fields.at("res-star") << "\n";
        req << "ie.result=authenticated\n";
    }
    return req.str();
}

ControlPlaneModule::ParsedN15Message ControlPlaneModule::parse_n15_message(const std::string& request) {
    ParsedN15Message out;
    const std::string trimmed = trim_copy(request);

    if (trimmed == "get-am-policy") {
        out.procedure = N15ProcedureType::GetAmPolicy;
        out.legacy = true;
        return out;
    }
    if (trimmed == "get-sm-policy") {
        out.procedure = N15ProcedureType::GetSmPolicy;
        out.legacy = true;
        return out;
    }
    if (trimmed == "update-policy-association") {
        out.procedure = N15ProcedureType::UpdatePolicyAssociation;
        out.legacy = true;
        return out;
    }

    if (trimmed.rfind("N15SBI|", 0) != 0) {
        return out;
    }

    std::size_t start = 7;
    while (start < trimmed.size()) {
        const std::size_t next = trimmed.find('|', start);
        const std::string token = trimmed.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.fields[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const std::string procedure = normalize_n15_procedure(out.fields["procedure"]);
    if (procedure == "GetAmPolicy") {
        out.procedure = N15ProcedureType::GetAmPolicy;
    } else if (procedure == "GetSmPolicy") {
        out.procedure = N15ProcedureType::GetSmPolicy;
    } else if (procedure == "UpdatePolicyAssociation") {
        out.procedure = N15ProcedureType::UpdatePolicyAssociation;
    }

    return out;
}

bool ControlPlaneModule::validate_n15_message(const ParsedN15Message& message, const N15PolicyContext& ctx, std::string& error) {
    switch (message.procedure) {
        case N15ProcedureType::GetAmPolicy:
            if (message.fields.count("policy-type") == 0 || trim_copy(message.fields.at("policy-type")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (trim_copy(message.fields.at("policy-type")) != "am-policy") {
                error = "policy-type-mismatch";
                return false;
            }
            if (message.fields.count("snssai") != 0U && !trim_copy(message.fields.at("snssai")).empty()
                && !is_valid_snssai(message.fields.at("snssai"))) {
                error = "invalid-snssai";
                return false;
            }
            return true;
        case N15ProcedureType::GetSmPolicy:
            if (message.fields.count("policy-type") == 0 || trim_copy(message.fields.at("policy-type")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (trim_copy(message.fields.at("policy-type")) != "sm-policy") {
                error = "policy-type-mismatch";
                return false;
            }
            if (message.fields.count("snssai") != 0U && !trim_copy(message.fields.at("snssai")).empty()
                && !is_valid_snssai(message.fields.at("snssai"))) {
                error = "invalid-snssai";
                return false;
            }
            return true;
        case N15ProcedureType::UpdatePolicyAssociation:
            if (ctx.state != N15PolicyState::Associated || ctx.association_id.empty()) {
                error = "no-policy-context";
                return false;
            }
            if (message.fields.count("association-id") == 0 || trim_copy(message.fields.at("association-id")).empty()
                || message.fields.count("policy-rule") == 0 || trim_copy(message.fields.at("policy-rule")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (trim_copy(message.fields.at("association-id")) != ctx.association_id) {
                error = "association-id-mismatch";
                return false;
            }
            if (message.fields.count("policy-type") != 0U && !trim_copy(message.fields.at("policy-type")).empty()
                && trim_copy(message.fields.at("policy-type")) != ctx.last_policy_type) {
                error = "policy-type-mismatch";
                return false;
            }
            if (message.fields.count("snssai") != 0U && !trim_copy(message.fields.at("snssai")).empty()
                && !is_valid_snssai(message.fields.at("snssai"))) {
                error = "invalid-snssai";
                return false;
            }
            return true;
        case N15ProcedureType::Unknown:
        default:
            error = "unsupported-procedure";
            return false;
    }
}

std::string ControlPlaneModule::build_n15_sbi_request(const std::string& imsi, const ParsedN15Message& message, const N15PolicyContext& ctx) {
    std::ostringstream req;
    req << "N15SBI/1\n";
    if (message.procedure == N15ProcedureType::GetAmPolicy || message.procedure == N15ProcedureType::GetSmPolicy) {
        const bool sm = message.procedure == N15ProcedureType::GetSmPolicy;
        req << "procedure=" << (sm ? "GetSmPolicy" : "GetAmPolicy") << "\n";
        req << "header.method=GET\n";
        req << "header.path=/npcf-policyauthorization/v1/app-sessions/" << imsi << "\n";
        req << "header.sequence=" << ctx.request_seq << "\n";
        req << "ie.imsi=" << imsi << "\n";
        req << "ie.policy-type=" << message.fields.at("policy-type") << "\n";
        if (!ctx.association_id.empty()) {
            req << "ie.association-id=" << ctx.association_id << "\n";
        }
        req << "ie.snssai=" << ctx.last_snssai << "\n";
    } else {
        req << "procedure=UpdatePolicyAssociation\n";
        req << "header.method=PATCH\n";
        req << "header.path=/npcf-policyauthorization/v1/app-sessions/" << ctx.association_id << "\n";
        req << "header.sequence=" << ctx.request_seq << "\n";
        req << "ie.imsi=" << imsi << "\n";
        req << "ie.association-id=" << ctx.association_id << "\n";
        req << "ie.policy-type=" << ctx.last_policy_type << "\n";
        req << "ie.policy-rule=" << message.fields.at("policy-rule") << "\n";
        req << "ie.snssai=" << ctx.last_snssai << "\n";
    }
    return req.str();
}

ControlPlaneModule::ParsedN22Message ControlPlaneModule::parse_n22_message(const std::string& request) {
    ParsedN22Message out;
    const std::string trimmed = trim_copy(request);

    if (trimmed == "select-slice") {
        out.procedure = N22ProcedureType::SelectSlice;
        out.legacy = true;
        return out;
    }
    if (trimmed == "update-selection") {
        out.procedure = N22ProcedureType::UpdateSelection;
        out.legacy = true;
        return out;
    }
    if (trimmed == "release-selection") {
        out.procedure = N22ProcedureType::ReleaseSelection;
        out.legacy = true;
        return out;
    }

    if (trimmed.rfind("N22SBI|", 0) != 0) {
        out.procedure = N22ProcedureType::SelectSlice;
        out.legacy = true;
        out.fields["requested-snssai"] = trimmed;
        return out;
    }

    std::size_t start = 7;
    while (start < trimmed.size()) {
        const std::size_t next = trimmed.find('|', start);
        const std::string token = trimmed.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.fields[trim_copy(token.substr(0, eq))] = trim_copy(token.substr(eq + 1));
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const std::string procedure = normalize_n22_procedure(out.fields["procedure"]);
    if (procedure == "SelectSlice") {
        out.procedure = N22ProcedureType::SelectSlice;
    } else if (procedure == "UpdateSelection") {
        out.procedure = N22ProcedureType::UpdateSelection;
    } else if (procedure == "ReleaseSelection") {
        out.procedure = N22ProcedureType::ReleaseSelection;
    }

    return out;
}

bool ControlPlaneModule::validate_n22_message(const ParsedN22Message& message, const N22SelectionContext& ctx, std::string& error) {
    switch (message.procedure) {
        case N22ProcedureType::SelectSlice:
        case N22ProcedureType::UpdateSelection: {
            if (message.procedure == N22ProcedureType::UpdateSelection && (ctx.state != N22SelectionState::Selected || ctx.selection_id.empty())) {
                error = "no-selection-context";
                return false;
            }
            if (message.fields.count("requested-snssai") == 0U || trim_copy(message.fields.at("requested-snssai")).empty()
                || message.fields.count("allowed-snssai") == 0U || trim_copy(message.fields.at("allowed-snssai")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (!is_valid_snssai(message.fields.at("requested-snssai"))) {
                error = "invalid-snssai";
                return false;
            }
            if (message.fields.count("fallback-snssai") != 0U && !trim_copy(message.fields.at("fallback-snssai")).empty()
                && !is_valid_snssai(message.fields.at("fallback-snssai"))) {
                error = "invalid-fallback-snssai";
                return false;
            }

            const bool direct_match = list_contains_csv_token(message.fields.at("allowed-snssai"), message.fields.at("requested-snssai"));
            if (!direct_match) {
                if (message.fields.count("fallback-snssai") == 0U || trim_copy(message.fields.at("fallback-snssai")).empty()) {
                    error = "unsupported-snssai";
                    return false;
                }
                if (!list_contains_csv_token(message.fields.at("allowed-snssai"), message.fields.at("fallback-snssai"))) {
                    error = "fallback-not-allowed";
                    return false;
                }
            }
            return true;
        }
        case N22ProcedureType::ReleaseSelection:
            if (ctx.state != N22SelectionState::Selected || ctx.selection_id.empty()) {
                error = "no-selection-context";
                return false;
            }
            if (message.fields.count("selection-id") == 0U || trim_copy(message.fields.at("selection-id")).empty()) {
                error = "missing-mandatory-ie";
                return false;
            }
            if (trim_copy(message.fields.at("selection-id")) != ctx.selection_id) {
                error = "selection-id-mismatch";
                return false;
            }
            return true;
        case N22ProcedureType::Unknown:
        default:
            error = "unsupported-procedure";
            return false;
    }
}

std::string ControlPlaneModule::build_n22_sbi_request(
    const std::string& imsi,
    const ParsedN22Message& message,
    const N22SelectionContext& ctx,
    const std::string& selection_result) {
    std::ostringstream req;
    req << "N22SBI/1\n";
    if (message.procedure == N22ProcedureType::SelectSlice) {
        req << "procedure=SelectSlice\n";
        req << "header.method=POST\n";
        req << "header.path=/nnssf-nsselection/v1/network-slice-information\n";
    } else if (message.procedure == N22ProcedureType::UpdateSelection) {
        req << "procedure=UpdateSelection\n";
        req << "header.method=PATCH\n";
        req << "header.path=/nnssf-nsselection/v1/network-slice-information/" << ctx.selection_id << "\n";
    } else {
        req << "procedure=ReleaseSelection\n";
        req << "header.method=DELETE\n";
        req << "header.path=/nnssf-nsselection/v1/network-slice-information/" << ctx.selection_id << "\n";
    }
    req << "header.sequence=" << ctx.request_seq << "\n";
    req << "ie.imsi=" << imsi << "\n";
    req << "ie.selection-id=" << ctx.selection_id << "\n";
    if (message.fields.count("requested-snssai") != 0U && !trim_copy(message.fields.at("requested-snssai")).empty()) {
        req << "ie.requested-snssai=" << message.fields.at("requested-snssai") << "\n";
    }
    if (!ctx.selected_snssai.empty()) {
        req << "ie.selected-snssai=" << ctx.selected_snssai << "\n";
    }
    if (!ctx.allowed_snssai.empty()) {
        req << "ie.allowed-snssai=" << ctx.allowed_snssai << "\n";
    }
    req << "ie.selection-result=" << selection_result << "\n";
    append_n22_response_fields(req, build_n22_success_response(message, ctx, selection_result));
    return req.str();
}

std::string ControlPlaneModule::n22_procedure_name(N22ProcedureType procedure) {
    switch (procedure) {
        case N22ProcedureType::SelectSlice:
            return "SelectSlice";
        case N22ProcedureType::UpdateSelection:
            return "UpdateSelection";
        case N22ProcedureType::ReleaseSelection:
            return "ReleaseSelection";
        case N22ProcedureType::Unknown:
        default:
            return "Unknown";
    }
}

ControlPlaneModule::N22ResponseModel ControlPlaneModule::build_n22_success_response(
    const ParsedN22Message& message,
    const N22SelectionContext& ctx,
    const std::string& selection_result) {
    N22ResponseModel response;
    response.status = "success";
    response.code = "200";
    response.procedure = n22_procedure_name(message.procedure);
    response.selection_state = selection_result;
    response.selection_id = ctx.selection_id;
    response.selected_snssai = ctx.selected_snssai;
    response.correlation_id = ctx.selection_id + ":" + std::to_string(ctx.request_seq);
    return response;
}

ControlPlaneModule::N22ResponseModel ControlPlaneModule::build_n22_error_response(
    const std::string& imsi,
    const std::string& cause) {
    N22ResponseModel response;
    response.status = "error";
    response.code = "400";
    response.procedure = "ErrorIndication";
    response.cause = cause;
    response.correlation_id = imsi + ":error";
    return response;
}

void ControlPlaneModule::append_n22_response_fields(std::ostringstream& req, const N22ResponseModel& response) {
    req << "response.model=" << response.model << "\n";
    req << "response.version=" << response.version << "\n";
    req << "response.status=" << response.status << "\n";
    req << "response.code=" << response.code << "\n";
    req << "response.procedure=" << response.procedure << "\n";
    if (!response.selection_state.empty()) {
        req << "response.selection-state=" << response.selection_state << "\n";
    }
    if (!response.selection_id.empty()) {
        req << "response.selection-id=" << response.selection_id << "\n";
    }
    if (!response.selected_snssai.empty()) {
        req << "response.selected-snssai=" << response.selected_snssai << "\n";
    }
    if (!response.cause.empty()) {
        req << "response.cause=" << response.cause << "\n";
    }
    req << "response.correlation-id=" << response.correlation_id << "\n";
}

std::string ControlPlaneModule::next_ran_ue_ngap_id(const std::string& imsi) {
    return to_hex8(fnv1a32(imsi + ":ran-ue-ngap-id"));
}

std::string ControlPlaneModule::next_amf_ue_ngap_id(const std::string& imsi) {
    return to_hex8(fnv1a32(imsi + ":amf-ue-ngap-id"));
}

std::string ControlPlaneModule::normalize_ngap_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "InitialUEMessage" || trimmed == "initial-ue-message") {
        return "InitialUEMessage";
    }
    if (trimmed == "InitialContextSetupRequest" || trimmed == "initial-context-setup") {
        return "InitialContextSetupRequest";
    }
    if (trimmed == "UEContextReleaseCommand" || trimmed == "ue-context-release") {
        return "UEContextReleaseCommand";
    }
    if (trimmed == "Paging" || trimmed == "paging") {
        return "Paging";
    }
    return trimmed;
}

std::string ControlPlaneModule::normalize_n8_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "GetAmData" || trimmed == "get-am-data") {
        return "GetAmData";
    }
    if (trimmed == "GetSmfSelectionData" || trimmed == "get-smf-selection-data") {
        return "GetSmfSelectionData";
    }
    if (trimmed == "GetUeContextInSmfData" || trimmed == "get-ue-context-in-smf-data") {
        return "GetUeContextInSmfData";
    }
    return trimmed;
}

std::string ControlPlaneModule::normalize_n11_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "Create" || trimmed == "create") {
        return "Create";
    }
    if (trimmed == "Modify" || trimmed == "modify") {
        return "Modify";
    }
    if (trimmed == "Release" || trimmed == "release") {
        return "Release";
    }
    return trimmed;
}

std::string ControlPlaneModule::normalize_n12_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "AuthRequest" || trimmed == "auth-request") {
        return "AuthRequest";
    }
    if (trimmed == "AuthResponse" || trimmed == "auth-response") {
        return "AuthResponse";
    }
    return trimmed;
}

std::string ControlPlaneModule::normalize_n15_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "GetAmPolicy" || trimmed == "get-am-policy") {
        return "GetAmPolicy";
    }
    if (trimmed == "GetSmPolicy" || trimmed == "get-sm-policy") {
        return "GetSmPolicy";
    }
    if (trimmed == "UpdatePolicyAssociation" || trimmed == "update-policy-association") {
        return "UpdatePolicyAssociation";
    }
    return trimmed;
}

std::string ControlPlaneModule::normalize_n22_procedure(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    if (trimmed == "SelectSlice" || trimmed == "select-slice") {
        return "SelectSlice";
    }
    if (trimmed == "UpdateSelection" || trimmed == "update-selection") {
        return "UpdateSelection";
    }
    if (trimmed == "ReleaseSelection" || trimmed == "release-selection") {
        return "ReleaseSelection";
    }
    return trimmed;
}

bool ControlPlaneModule::try_parse_u32(const std::string& value, std::uint32_t& out) {
    if (trim_copy(value).empty()) {
        return false;
    }
    try {
        std::size_t idx = 0;
        const auto parsed = std::stoul(trim_copy(value), &idx, 0);
        if (idx != trim_copy(value).size()) {
            return false;
        }
        out = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ControlPlaneModule::is_valid_snssai(const std::string& value) {
    const std::string trimmed = trim_copy(value);
    const std::size_t dash = trimmed.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= trimmed.size()) {
        return false;
    }

    const std::string sst = trimmed.substr(0, dash);
    const std::string sd = trimmed.substr(dash + 1);
    if (!is_all_digits(sst) || sd.size() != 6) {
        return false;
    }

    return std::all_of(sd.begin(), sd.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0 || (std::tolower(ch) >= 'a' && std::tolower(ch) <= 'f');
    });
}

bool ControlPlaneModule::list_contains_csv_token(const std::string& csv, const std::string& token) {
    std::istringstream stream(csv);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (trim_copy(item) == trim_copy(token)) {
            return true;
        }
    }
    return false;
}

std::string ControlPlaneModule::compute_nas_mac(const std::string& pdu_without_mac, std::uint32_t k_amf, std::uint32_t count) {
    return to_hex8(fnv1a32(pdu_without_mac + ":mac:" + std::to_string(k_amf) + ':' + std::to_string(count)));
}

std::string ControlPlaneModule::trim_copy(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

bool ControlPlaneModule::send_n1_service_reject(const std::string& imsi, N1SecurityContext& ctx, const std::string& cause) {
    std::unordered_map<std::string, std::string> fields;
    fields["cause"] = cause;
    const std::string pdu = build_nas_downlink_pdu("ServiceReject", fields, ++ctx.dl_count, ctx.k_amf, ctx.authenticated);
    n1_->send_nas_to_ue(imsi, pdu);
    return false;
}

bool ControlPlaneModule::send_n2_error_indication(const std::string& imsi, const std::string& cause) {
    ParsedN2Message message;
    message.procedure = N2ProcedureType::Unknown;
    message.ies["cause"] = cause;

    std::ostringstream pdu;
    pdu << "NGAP/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    n2_->deliver_nas(imsi, pdu.str());
    return false;
}

bool ControlPlaneModule::send_n8_error_indication(const std::string& imsi, const std::string& cause) {
    std::ostringstream pdu;
    pdu << "N8SBI/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    n8_->query_subscription(imsi, pdu.str());
    return false;
}

bool ControlPlaneModule::send_n11_error_indication(const std::string& imsi, const std::string& cause) {
    std::ostringstream pdu;
    pdu << "N11SBI/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    n11_->manage_pdu_session(imsi, pdu.str());
    return false;
}

bool ControlPlaneModule::send_n12_error_indication(const std::string& imsi, const std::string& cause) {
    std::ostringstream pdu;
    pdu << "N12SBI/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    n12_->authenticate_ue(imsi, pdu.str());
    return false;
}

bool ControlPlaneModule::send_n15_error_indication(const std::string& imsi, const std::string& cause) {
    std::ostringstream pdu;
    pdu << "N15SBI/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    n15_->query_policy(imsi, pdu.str());
    return false;
}

bool ControlPlaneModule::send_n22_error_indication(const std::string& imsi, const std::string& cause) {
    std::ostringstream pdu;
    pdu << "N22SBI/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "ie.cause=" << cause << "\n";
    append_n22_response_fields(pdu, build_n22_error_response(imsi, cause));
    n22_->select_network_slice(imsi, pdu.str());
    return false;
}

}  // namespace amf
