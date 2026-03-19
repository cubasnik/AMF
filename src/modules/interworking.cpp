#include "amf/modules/interworking.hpp"

#include <sstream>

namespace amf {
namespace {

std::string normalize_n14_procedure(const std::string& value) {
    if (value == "PrepareHandover" || value == "prepare-handover") {
        return "PrepareHandover";
    }
    if (value == "ContextTransfer" || value == "context-transfer") {
        return "ContextTransfer";
    }
    if (value == "CompleteTransfer" || value == "complete-transfer") {
        return "CompleteTransfer";
    }
    if (value == "RollbackContext" || value == "rollback-context") {
        return "RollbackContext";
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

std::string normalize_procedure(const std::string& value) {
    if (value == "HandoverRequest" || value == "handover-request" || value == "handover") {
        return "HandoverRequest";
    }
    if (value == "ContextTransfer" || value == "context-transfer") {
        return "ContextTransfer";
    }
    if (value == "IsrActivate" || value == "isr-activate") {
        return "IsrActivate";
    }
    if (value == "IsrDeactivate" || value == "isr-deactivate") {
        return "IsrDeactivate";
    }
    if (value == "ReleaseContext" || value == "release-context" || value == "release") {
        return "ReleaseContext";
    }
    return value;
}

}  // namespace

InterworkingModule::InterworkingModule(IN14Interface* n14, IN26Interface* n26)
    : n14_(n14), n26_(n26) {}

bool InterworkingModule::transfer_n14_context(bool operational, bool ue_exists, const std::string& imsi, const std::string& request) {
    if (!operational || !ue_exists || n14_ == nullptr || request.empty()) {
        return false;
    }

    auto& context = n14_context_[imsi];
    ParsedN14Message message = parse_n14_message(request);
    if (message.procedure == N14ProcedureType::Unknown) {
        return send_n14_error_indication(imsi, "unsupported-procedure");
    }

    if (message.legacy) {
        if (message.procedure == N14ProcedureType::PrepareHandover || message.procedure == N14ProcedureType::ContextTransfer) {
            if (message.fields["target-amf"].empty()) {
                message.fields["target-amf"] = context.target_amf.empty() ? "amf-target" : context.target_amf;
            }
            if (message.fields["transfer-id"].empty()) {
                message.fields["transfer-id"] = context.transfer_id.empty() ? "ctx-" + std::to_string(derive_default_teid(imsi)) : context.transfer_id;
            }
            if (message.fields["source-amf"].empty()) {
                message.fields["source-amf"] = context.source_amf.empty() ? "amf-local" : context.source_amf;
            }
            if (message.fields["ue-context-version"].empty()) {
                message.fields["ue-context-version"] = context.ue_context_version.empty() ? "1" : context.ue_context_version;
            }
        } else if (message.procedure == N14ProcedureType::CompleteTransfer || message.procedure == N14ProcedureType::RollbackContext) {
            if (message.fields["target-amf"].empty()) {
                message.fields["target-amf"] = context.target_amf;
            }
            if (message.fields["transfer-id"].empty()) {
                message.fields["transfer-id"] = context.transfer_id;
            }
        }
    }

    std::string error;
    if (!validate_n14_message(message, context, error)) {
        return send_n14_error_indication(imsi, error);
    }

    N14Context request_context = context;
    ++request_context.request_seq;
    std::string result = "accepted";

    if (message.procedure == N14ProcedureType::PrepareHandover) {
        request_context.target_amf = message.fields.at("target-amf");
        request_context.source_amf = message.fields.at("source-amf");
        request_context.transfer_id = message.fields.at("transfer-id");
        request_context.ue_context_version = message.fields.at("ue-context-version");
        request_context.state = N14TransferState::Prepared;
        result = "prepared";
        context = request_context;
    } else if (message.procedure == N14ProcedureType::ContextTransfer) {
        request_context.target_amf = message.fields.at("target-amf");
        request_context.source_amf = message.fields.at("source-amf");
        request_context.transfer_id = message.fields.at("transfer-id");
        request_context.ue_context_version = message.fields.at("ue-context-version");
        request_context.state = N14TransferState::ContextTransferred;
        result = "context-transferred";
        context = request_context;
    } else if (message.procedure == N14ProcedureType::CompleteTransfer) {
        request_context.state = N14TransferState::Completed;
        result = "completed";
        context = request_context;
    } else if (message.procedure == N14ProcedureType::RollbackContext) {
        result = "rolled-back";
        n14_->transfer_amf_context(imsi, build_n14_sbi_request(imsi, message, request_context, result));
        n14_context_.erase(imsi);
        return true;
    }

    n14_->transfer_amf_context(imsi, build_n14_sbi_request(imsi, message, context, result));
    return true;
}

InterworkingModule::ParsedN14Message InterworkingModule::parse_n14_message(const std::string& request) const {
    ParsedN14Message parsed;

    if (request == "prepare-handover") {
        parsed.procedure = N14ProcedureType::PrepareHandover;
        parsed.legacy = true;
        return parsed;
    }
    if (request == "context-transfer") {
        parsed.procedure = N14ProcedureType::ContextTransfer;
        parsed.legacy = true;
        return parsed;
    }
    if (request == "complete-transfer") {
        parsed.procedure = N14ProcedureType::CompleteTransfer;
        parsed.legacy = true;
        return parsed;
    }
    if (request == "rollback-context") {
        parsed.procedure = N14ProcedureType::RollbackContext;
        parsed.legacy = true;
        return parsed;
    }

    if (request.rfind("N14SBI|", 0) == 0) {
        parsed.fields = parse_kv_payload(request.substr(7), '|');
        const std::string procedure = normalize_n14_procedure(parsed.fields["procedure"]);
        if (procedure == "PrepareHandover") {
            parsed.procedure = N14ProcedureType::PrepareHandover;
        } else if (procedure == "ContextTransfer") {
            parsed.procedure = N14ProcedureType::ContextTransfer;
        } else if (procedure == "CompleteTransfer") {
            parsed.procedure = N14ProcedureType::CompleteTransfer;
        } else if (procedure == "RollbackContext") {
            parsed.procedure = N14ProcedureType::RollbackContext;
        }
        return parsed;
    }

    parsed.procedure = N14ProcedureType::ContextTransfer;
    parsed.fields["target-amf"] = request;
    parsed.legacy = true;
    return parsed;
}

bool InterworkingModule::validate_n14_message(const ParsedN14Message& message, const N14Context& context, std::string& error_cause) const {
    const auto require_non_empty = [&](const char* key) {
        const auto it = message.fields.find(key);
        return it != message.fields.end() && !it->second.empty();
    };

    switch (message.procedure) {
        case N14ProcedureType::PrepareHandover:
            if (context.state == N14TransferState::Prepared || context.state == N14TransferState::ContextTransferred) {
                error_cause = "handover-already-prepared";
                return false;
            }
            if (!require_non_empty("target-amf") || !require_non_empty("source-amf")
                || !require_non_empty("transfer-id") || !require_non_empty("ue-context-version")) {
                error_cause = "missing-mandatory-ie";
                return false;
            }
            return true;
        case N14ProcedureType::ContextTransfer:
            if (!require_non_empty("target-amf") || !require_non_empty("source-amf")
                || !require_non_empty("transfer-id") || !require_non_empty("ue-context-version")) {
                error_cause = "missing-mandatory-ie";
                return false;
            }
            if (!message.legacy && context.state != N14TransferState::Prepared && context.state != N14TransferState::ContextTransferred) {
                error_cause = "no-prepare-context";
                return false;
            }
            if (!context.transfer_id.empty() && message.fields.at("transfer-id") != context.transfer_id) {
                error_cause = "transfer-id-mismatch";
                return false;
            }
            if (!context.target_amf.empty() && message.fields.at("target-amf") != context.target_amf) {
                error_cause = "target-amf-mismatch";
                return false;
            }
            return true;
        case N14ProcedureType::CompleteTransfer:
            if (context.state != N14TransferState::ContextTransferred) {
                error_cause = "no-transfer-context";
                return false;
            }
            if (!require_non_empty("transfer-id") || !require_non_empty("target-amf")) {
                error_cause = "missing-mandatory-ie";
                return false;
            }
            if (message.fields.at("transfer-id") != context.transfer_id) {
                error_cause = "transfer-id-mismatch";
                return false;
            }
            return true;
        case N14ProcedureType::RollbackContext:
            if (context.state != N14TransferState::Prepared && context.state != N14TransferState::ContextTransferred) {
                error_cause = "no-transfer-context";
                return false;
            }
            if (!require_non_empty("transfer-id")) {
                error_cause = "missing-mandatory-ie";
                return false;
            }
            if (message.fields.at("transfer-id") != context.transfer_id) {
                error_cause = "transfer-id-mismatch";
                return false;
            }
            return true;
        case N14ProcedureType::Unknown:
        default:
            error_cause = "unsupported-procedure";
            return false;
    }
}

bool InterworkingModule::send_n14_error_indication(const std::string& imsi, const std::string& cause) const {
    std::ostringstream req;
    req << "N14SBI/1\n";
    req << "procedure=ErrorIndication\n";
    req << "header.method=POST\n";
    req << "header.path=/namf-comm/v1/context-transfers/errors\n";
    req << "ie.imsi=" << imsi << "\n";
    req << "ie.cause=" << cause << "\n";
    append_n14_response_fields(req, build_n14_error_response(imsi, cause));
    n14_->transfer_amf_context(imsi, req.str());
    return false;
}

std::string InterworkingModule::n14_procedure_name(N14ProcedureType procedure) {
    switch (procedure) {
        case N14ProcedureType::PrepareHandover:
            return "PrepareHandover";
        case N14ProcedureType::ContextTransfer:
            return "ContextTransfer";
        case N14ProcedureType::CompleteTransfer:
            return "CompleteTransfer";
        case N14ProcedureType::RollbackContext:
            return "RollbackContext";
        case N14ProcedureType::Unknown:
        default:
            return "Unknown";
    }
}

InterworkingModule::N14ResponseModel InterworkingModule::build_n14_success_response(
    const ParsedN14Message& message,
    const N14Context& context,
    const std::string& result) {
    N14ResponseModel response;
    response.status = "success";
    response.code = "200";
    response.procedure = n14_procedure_name(message.procedure);
    response.transfer_state = result;
    response.transfer_id = context.transfer_id;
    response.ue_context_version = context.ue_context_version;
    response.correlation_id = context.transfer_id + ":" + std::to_string(context.request_seq);
    return response;
}

InterworkingModule::N14ResponseModel InterworkingModule::build_n14_error_response(
    const std::string& imsi,
    const std::string& cause) {
    N14ResponseModel response;
    response.status = "error";
    response.code = "400";
    response.procedure = "ErrorIndication";
    response.cause = cause;
    response.correlation_id = imsi + ":error";
    return response;
}

void InterworkingModule::append_n14_response_fields(std::ostringstream& req, const N14ResponseModel& response) {
    req << "response.model=" << response.model << "\n";
    req << "response.version=" << response.version << "\n";
    req << "response.status=" << response.status << "\n";
    req << "response.code=" << response.code << "\n";
    req << "response.procedure=" << response.procedure << "\n";
    if (!response.transfer_state.empty()) {
        req << "response.transfer-state=" << response.transfer_state << "\n";
    }
    if (!response.transfer_id.empty()) {
        req << "response.transfer-id=" << response.transfer_id << "\n";
    }
    if (!response.ue_context_version.empty()) {
        req << "response.ue-context-version=" << response.ue_context_version << "\n";
    }
    if (!response.cause.empty()) {
        req << "response.cause=" << response.cause << "\n";
    }
    req << "response.correlation-id=" << response.correlation_id << "\n";
}

std::string InterworkingModule::build_n14_sbi_request(
    const std::string& imsi,
    const ParsedN14Message& message,
    const N14Context& context,
    const std::string& result) const {
    std::ostringstream req;
    req << "N14SBI/1\n";
    if (message.procedure == N14ProcedureType::PrepareHandover) {
        req << "procedure=PrepareHandover\n";
        req << "header.method=POST\n";
        req << "header.path=/namf-comm/v1/context-transfers\n";
    } else if (message.procedure == N14ProcedureType::ContextTransfer) {
        req << "procedure=ContextTransfer\n";
        req << "header.method=PUT\n";
        req << "header.path=/namf-comm/v1/context-transfers/" << context.transfer_id << "\n";
    } else if (message.procedure == N14ProcedureType::CompleteTransfer) {
        req << "procedure=CompleteTransfer\n";
        req << "header.method=POST\n";
        req << "header.path=/namf-comm/v1/context-transfers/" << context.transfer_id << "/complete\n";
    } else {
        req << "procedure=RollbackContext\n";
        req << "header.method=POST\n";
        req << "header.path=/namf-comm/v1/context-transfers/" << context.transfer_id << "/rollback\n";
    }
    req << "header.sequence=" << context.request_seq << "\n";
    req << "ie.imsi=" << imsi << "\n";
    req << "ie.source-amf=" << context.source_amf << "\n";
    req << "ie.target-amf=" << context.target_amf << "\n";
    req << "ie.transfer-id=" << context.transfer_id << "\n";
    req << "ie.ue-context-version=" << context.ue_context_version << "\n";
    req << "ie.result=" << result << "\n";
    append_n14_response_fields(req, build_n14_success_response(message, context, result));
    return req.str();
}

bool InterworkingModule::interwork_n26(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation) {
    if (!operational || !ue_exists || n26_ == nullptr || operation.empty()) {
        return false;
    }

    auto& context = n26_context_[imsi];
    ParsedN26Message message = parse_n26_message(operation);

    if (message.procedure == N26ProcedureType::Unknown) {
        return send_n26_error_indication(imsi, "unsupported-procedure");
    }

    if (message.procedure == N26ProcedureType::HandoverRequest
        && message.ies.count("legacy") != 0U
        && message.ies.count("mme-teid") == 0U) {
        message.ies["mme-teid"] = std::to_string(derive_default_teid(imsi));
    }
    if (message.procedure == N26ProcedureType::HandoverRequest
        && message.ies.count("legacy") != 0U
        && message.ies.count("enb-teid") == 0U) {
        message.ies["enb-teid"] = std::to_string(derive_default_teid(imsi) ^ 0x00ABCDEFU);
    }
    if (message.procedure == N26ProcedureType::HandoverRequest
        && message.ies.count("legacy") != 0U
        && message.ies.count("tai") == 0U) {
        message.ies["tai"] = "250-03";
    }
    if (message.procedure == N26ProcedureType::ContextTransfer
        && message.ies.count("target-mme") == 0U
        && message.ies.count("legacy") != 0U) {
        message.ies["target-mme"] = "mme-target";
    }

    std::string validation_error;
    if (!validate_n26_message(message, context, validation_error)) {
        return send_n26_error_indication(imsi, validation_error);
    }

    if (message.procedure == N26ProcedureType::HandoverRequest) {
        parse_u32_auto(message.ies.at("mme-teid"), context.mme_teid);
        parse_u32_auto(message.ies.at("enb-teid"), context.enb_teid);
        context.tai = message.ies.at("tai");
        context.sequence = 0;
        context.state = N26MmeState::HandoverPrepared;
    } else if (message.procedure == N26ProcedureType::ContextTransfer) {
        context.state = N26MmeState::ContextTransferred;
        ++context.sequence;
    } else if (message.procedure == N26ProcedureType::IsrActivate) {
        context.state = N26MmeState::IsrActive;
        ++context.sequence;
    } else if (message.procedure == N26ProcedureType::IsrDeactivate) {
        context.state = N26MmeState::ContextTransferred;
        ++context.sequence;
    } else if (message.procedure == N26ProcedureType::ReleaseContext) {
        context.state = N26MmeState::Released;
        ++context.sequence;
    }

    n26_->interwork_with_mme(imsi, build_gtpv2c_pdu(message, context, imsi));
    return true;
}

InterworkingModule::ParsedN26Message InterworkingModule::parse_n26_message(const std::string& operation) const {
    ParsedN26Message parsed;

    if (operation == "handover") {
        parsed.procedure = N26ProcedureType::HandoverRequest;
        parsed.ies["legacy"] = "1";
        return parsed;
    }
    if (operation == "context-transfer") {
        parsed.procedure = N26ProcedureType::ContextTransfer;
        parsed.ies["legacy"] = "1";
        return parsed;
    }
    if (operation == "isr-activate") {
        parsed.procedure = N26ProcedureType::IsrActivate;
        parsed.ies["legacy"] = "1";
        return parsed;
    }
    if (operation == "isr-deactivate") {
        parsed.procedure = N26ProcedureType::IsrDeactivate;
        parsed.ies["legacy"] = "1";
        return parsed;
    }
    if (operation == "release") {
        parsed.procedure = N26ProcedureType::ReleaseContext;
        parsed.ies["legacy"] = "1";
        return parsed;
    }

    if (operation.rfind("GTPV2C|", 0) == 0) {
        parsed.ies = parse_kv_payload(operation.substr(7), '|');
        const auto it = parsed.ies.find("procedure");
        if (it == parsed.ies.end()) {
            parsed.procedure = N26ProcedureType::Unknown;
            return parsed;
        }

        const std::string name = normalize_procedure(it->second);
        if (name == "HandoverRequest") {
            parsed.procedure = N26ProcedureType::HandoverRequest;
        } else if (name == "ContextTransfer") {
            parsed.procedure = N26ProcedureType::ContextTransfer;
        } else if (name == "IsrActivate") {
            parsed.procedure = N26ProcedureType::IsrActivate;
        } else if (name == "IsrDeactivate") {
            parsed.procedure = N26ProcedureType::IsrDeactivate;
        } else if (name == "ReleaseContext") {
            parsed.procedure = N26ProcedureType::ReleaseContext;
        } else {
            parsed.procedure = N26ProcedureType::Unknown;
        }
        return parsed;
    }

    parsed.procedure = N26ProcedureType::Unknown;
    return parsed;
}

bool InterworkingModule::validate_n26_message(const ParsedN26Message& message, const N26MmeContext& context, std::string& error_cause) const {
    if (message.procedure == N26ProcedureType::HandoverRequest) {
        if (context.state == N26MmeState::HandoverPrepared || context.state == N26MmeState::ContextTransferred || context.state == N26MmeState::IsrActive) {
            error_cause = "duplicate-handover-request";
            return false;
        }

        if (message.ies.count("mme-teid") == 0U || message.ies.count("enb-teid") == 0U || message.ies.count("tai") == 0U) {
            error_cause = "missing-mandatory-ie";
            return false;
        }

        std::uint32_t mme_teid = 0;
        std::uint32_t enb_teid = 0;
        if (!parse_u32_auto(message.ies.at("mme-teid"), mme_teid) || mme_teid == 0U) {
            error_cause = "invalid-mme-teid";
            return false;
        }
        if (!parse_u32_auto(message.ies.at("enb-teid"), enb_teid) || enb_teid == 0U) {
            error_cause = "invalid-enb-teid";
            return false;
        }
        return true;
    }

    if (message.procedure == N26ProcedureType::ContextTransfer) {
        if (context.state != N26MmeState::HandoverPrepared && context.state != N26MmeState::ContextTransferred) {
            error_cause = "no-handover-context";
            return false;
        }
        if (message.ies.count("target-mme") == 0U) {
            error_cause = "missing-mandatory-ie";
            return false;
        }
        return true;
    }

    if (message.procedure == N26ProcedureType::IsrActivate) {
        if (context.state != N26MmeState::ContextTransferred && context.state != N26MmeState::IsrActive) {
            error_cause = "no-context-transfer";
            return false;
        }
        return true;
    }

    if (message.procedure == N26ProcedureType::IsrDeactivate) {
        if (context.state != N26MmeState::IsrActive) {
            error_cause = "isr-not-active";
            return false;
        }
        return true;
    }

    if (message.procedure == N26ProcedureType::ReleaseContext) {
        if (context.state == N26MmeState::Idle || context.state == N26MmeState::Released) {
            error_cause = "no-context";
            return false;
        }
        return true;
    }

    error_cause = "unsupported-procedure";
    return false;
}

bool InterworkingModule::send_n26_error_indication(const std::string& imsi, const std::string& cause) const {
    std::ostringstream pdu;
    pdu << "GTPV2C/1\n";
    pdu << "procedure=ErrorIndication\n";
    pdu << "header.version=2\n";
    pdu << "header.piggyback=0\n";
    pdu << "header.teid-flag=1\n";
    pdu << "header.message-type=1\n";
    pdu << "imsi=" << imsi << "\n";
    pdu << "ie.cause=" << cause << "\n";
    n26_->interwork_with_mme(imsi, pdu.str());
    return false;
}

std::string InterworkingModule::build_gtpv2c_pdu(const ParsedN26Message& message, const N26MmeContext& context, const std::string& imsi) const {
    std::uint32_t message_type = 0;
    std::string procedure = "Unknown";
    switch (message.procedure) {
        case N26ProcedureType::HandoverRequest:
            message_type = 32;
            procedure = "HandoverRequest";
            break;
        case N26ProcedureType::ContextTransfer:
            message_type = 95;
            procedure = "ContextTransfer";
            break;
        case N26ProcedureType::IsrActivate:
            message_type = 170;
            procedure = "IsrActivate";
            break;
        case N26ProcedureType::IsrDeactivate:
            message_type = 171;
            procedure = "IsrDeactivate";
            break;
        case N26ProcedureType::ReleaseContext:
            message_type = 37;
            procedure = "ReleaseContext";
            break;
        case N26ProcedureType::Unknown:
            break;
    }

    std::ostringstream pdu;
    pdu << "GTPV2C/1\n";
    pdu << "procedure=" << procedure << "\n";
    pdu << "header.version=2\n";
    pdu << "header.piggyback=0\n";
    pdu << "header.teid-flag=1\n";
    pdu << "header.message-type=" << message_type << "\n";
    pdu << "header.sequence=" << context.sequence << "\n";
    pdu << "imsi=" << imsi << "\n";
    pdu << "ie.mme-teid=" << context.mme_teid << "\n";
    pdu << "ie.enb-teid=" << context.enb_teid << "\n";
    if (!context.tai.empty()) {
        pdu << "ie.tai=" << context.tai << "\n";
    }

    for (const auto& [key, value] : message.ies) {
        if (key == "procedure" || key == "mme-teid" || key == "enb-teid" || key == "tai" || key == "legacy") {
            continue;
        }
        pdu << "ie." << key << "=" << value << "\n";
    }

    return pdu.str();
}

std::unordered_map<std::string, std::string> InterworkingModule::parse_kv_payload(const std::string& payload, char delimiter) {
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

std::uint32_t InterworkingModule::derive_default_teid(const std::string& imsi) {
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

}  // namespace amf
