#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

#include "amf/interfaces.hpp"

namespace amf {

class InterworkingModule {
public:
    InterworkingModule(IN14Interface* n14, IN26Interface* n26);

    bool transfer_n14_context(bool operational, bool ue_exists, const std::string& imsi, const std::string& request);
    bool interwork_n26(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation);

private:
    enum class N14ProcedureType {
        PrepareHandover,
        ContextTransfer,
        CompleteTransfer,
        RollbackContext,
        Unknown,
    };

    enum class N14TransferState {
        Idle,
        Prepared,
        ContextTransferred,
        Completed,
    };

    struct N14Context {
        std::string target_amf;
        std::string source_amf {"amf-local"};
        std::string transfer_id;
        std::string ue_context_version {"1"};
        std::uint32_t request_seq {0};
        N14TransferState state {N14TransferState::Idle};
    };

    struct ParsedN14Message {
        N14ProcedureType procedure {N14ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    struct N14ResponseModel {
        std::string model {"N14TransferResponse"};
        std::string version {"1"};
        std::string status;
        std::string code;
        std::string procedure;
        std::string transfer_state;
        std::string transfer_id;
        std::string ue_context_version;
        std::string cause;
        std::string correlation_id;
    };

    enum class N26ProcedureType {
        HandoverRequest,
        ContextTransfer,
        IsrActivate,
        IsrDeactivate,
        ReleaseContext,
        Unknown,
    };

    enum class N26MmeState {
        Idle,
        HandoverPrepared,
        ContextTransferred,
        IsrActive,
        Released,
    };

    struct N26MmeContext {
        std::uint32_t mme_teid {0};
        std::uint32_t enb_teid {0};
        std::string tai;
        std::uint32_t sequence {0};
        N26MmeState state {N26MmeState::Idle};
    };

    struct ParsedN26Message {
        N26ProcedureType procedure {N26ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> ies;
    };

    ParsedN14Message parse_n14_message(const std::string& request) const;
    bool validate_n14_message(const ParsedN14Message& message, const N14Context& context, std::string& error_cause) const;
    bool send_n14_error_indication(const std::string& imsi, const std::string& cause) const;
    static std::string n14_procedure_name(N14ProcedureType procedure);
    static N14ResponseModel build_n14_success_response(const ParsedN14Message& message, const N14Context& context, const std::string& result);
    static N14ResponseModel build_n14_error_response(const std::string& imsi, const std::string& cause);
    static void append_n14_response_fields(std::ostringstream& req, const N14ResponseModel& response);
    std::string build_n14_sbi_request(const std::string& imsi, const ParsedN14Message& message, const N14Context& context, const std::string& result) const;
    ParsedN26Message parse_n26_message(const std::string& operation) const;
    bool validate_n26_message(const ParsedN26Message& message, const N26MmeContext& context, std::string& error_cause) const;
    bool send_n26_error_indication(const std::string& imsi, const std::string& cause) const;
    std::string build_gtpv2c_pdu(const ParsedN26Message& message, const N26MmeContext& context, const std::string& imsi) const;
    static std::unordered_map<std::string, std::string> parse_kv_payload(const std::string& payload, char delimiter);
    static std::uint32_t derive_default_teid(const std::string& imsi);

    IN14Interface* n14_;
    IN26Interface* n26_;
    std::unordered_map<std::string, N14Context> n14_context_;
    std::unordered_map<std::string, N26MmeContext> n26_context_;
};

}  // namespace amf
