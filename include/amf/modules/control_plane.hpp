#pragma once

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

#include "amf/interfaces.hpp"

namespace amf {

class ControlPlaneModule {
public:
    ControlPlaneModule(
        IN1Interface* n1,
        IN2Interface* n2,
        ISbiInterface* sbi,
        IN8Interface* n8,
        IN11Interface* n11,
        IN12Interface* n12,
        IN15Interface* n15,
        IN22Interface* n22);

    bool send_n1_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);
    bool send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);
    bool notify_sbi(bool operational, const std::string& service_name, const std::string& payload);

    bool query_n8_subscription(bool operational, bool ue_exists, const std::string& imsi, const std::string& request);
    bool manage_n11_pdu_session(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation);
    bool authenticate_n12(bool operational, bool ue_exists, const std::string& imsi, const std::string& request);
    bool query_n15_policy(bool operational, bool ue_exists, const std::string& imsi, const std::string& request);
    bool select_n22_slice(bool operational, bool ue_exists, const std::string& imsi, const std::string& request);

    bool set_plmn(const std::string& mcc, const std::string& mnc);
    const std::string& mcc() const;
    const std::string& mnc() const;

private:
    enum class N1NasMessageType {
        RegistrationRequest,
        AuthenticationResponse,
        SecurityModeComplete,
        DeregistrationRequest,
        Unknown,
    };

    enum class N1ProcedureState {
        Deregistered,
        AuthenticationPending,
        SecurityModePending,
        Registered,
    };

    struct N1SecurityContext {
        std::uint32_t k_amf {0};
        std::string rand;
        std::string autn;
        bool authenticated {false};
        bool security_mode_complete {false};
        std::uint32_t ul_count {0};
        std::uint32_t dl_count {0};
        std::chrono::steady_clock::time_point state_entered_at {};
    };

    struct ParsedNasMessage {
        N1NasMessageType type {N1NasMessageType::Unknown};
        std::string res_star;
        std::string raw;
    };

    enum class N2ProcedureType {
        InitialUEMessage,
        InitialContextSetupRequest,
        UEContextReleaseCommand,
        Paging,
        Unknown,
    };

    enum class N2UeContextState {
        Idle,
        InitialUeAssociated,
        ContextSetupComplete,
        Released,
    };

    struct N2UeContext {
        std::uint32_t amf_ue_ngap_id {0};
        std::string ran_ue_ngap_id;
        std::string last_tai;
        N2UeContextState state {N2UeContextState::Idle};
    };

    struct ParsedN2Message {
        N2ProcedureType procedure {N2ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> ies;
        std::string raw;
        bool legacy {false};
    };

    enum class N8ProcedureType {
        GetAmData,
        GetSmfSelectionData,
        GetUeContextInSmfData,
        Unknown,
    };

    struct N8SubscriptionContext {
        std::uint32_t request_seq {0};
        std::string last_dataset {"am-data"};
        bool available {false};
    };

    struct ParsedN8Message {
        N8ProcedureType procedure {N8ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    enum class N11ProcedureType {
        Create,
        Modify,
        Release,
        Unknown,
    };

    enum class N11SessionState {
        None,
        Active,
    };

    struct N11SessionContext {
        std::uint32_t pdu_session_id {0};
        std::string dnn {"internet"};
        std::string snssai {"1-010203"};
        std::uint32_t sequence {0};
        N11SessionState state {N11SessionState::None};
    };

    struct ParsedN11Message {
        N11ProcedureType procedure {N11ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    enum class N12ProcedureType {
        AuthRequest,
        AuthResponse,
        Unknown,
    };

    enum class N12AuthState {
        Idle,
        ChallengeSent,
        Authenticated,
    };

    struct N12AuthContext {
        std::uint32_t request_seq {0};
        std::string rand;
        std::string autn;
        std::string xres_star;
        std::string auth_method {"5g-aka"};
        N12AuthState state {N12AuthState::Idle};
    };

    struct ParsedN12Message {
        N12ProcedureType procedure {N12ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    enum class N15ProcedureType {
        GetAmPolicy,
        GetSmPolicy,
        UpdatePolicyAssociation,
        Unknown,
    };

    enum class N15PolicyState {
        None,
        Associated,
    };

    struct N15PolicyContext {
        std::string association_id;
        std::string last_policy_type {"am-policy"};
        std::string last_snssai {"1-010203"};
        std::uint32_t request_seq {0};
        N15PolicyState state {N15PolicyState::None};
    };

    struct ParsedN15Message {
        N15ProcedureType procedure {N15ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    enum class N22ProcedureType {
        SelectSlice,
        UpdateSelection,
        ReleaseSelection,
        Unknown,
    };

    enum class N22SelectionState {
        None,
        Selected,
    };

    struct N22SelectionContext {
        std::string selection_id;
        std::string selected_snssai;
        std::string allowed_snssai {"1-010203,1-112233"};
        std::uint32_t request_seq {0};
        N22SelectionState state {N22SelectionState::None};
    };

    struct ParsedN22Message {
        N22ProcedureType procedure {N22ProcedureType::Unknown};
        std::unordered_map<std::string, std::string> fields;
        bool legacy {false};
    };

    struct N22ResponseModel {
        std::string model {"N22SelectionResponse"};
        std::string version {"1"};
        std::string status;
        std::string code;
        std::string procedure;
        std::string selection_state;
        std::string selection_id;
        std::string selected_snssai;
        std::string cause;
        std::string correlation_id;
    };

    static bool is_all_digits(const std::string& value);
    static ParsedNasMessage parse_n1_message(const std::string& payload);
    static std::unordered_map<std::string, std::string> parse_nas_kv(const std::string& payload);
    static std::string to_hex8(std::uint32_t value);
    static std::uint32_t fnv1a32(const std::string& text);
    static std::string derive_rand(const std::string& imsi, std::uint32_t dl_count);
    static std::string derive_autn(const std::string& imsi, const std::string& rand, std::uint32_t k_amf);
    static std::string expected_res_star(const std::string& imsi, const std::string& rand);
    static std::string build_nas_downlink_pdu(
        const std::string& message,
        const std::unordered_map<std::string, std::string>& fields,
        std::uint32_t dl_count,
        std::uint32_t k_amf,
        bool integrity_protected);
    static ParsedN2Message parse_n2_message(const std::string& imsi, const std::string& payload);
    static bool validate_n2_message(const ParsedN2Message& message, const N2UeContext& ctx, std::string& error);
    static std::string build_ngap_pdu(const ParsedN2Message& message);
    static ParsedN8Message parse_n8_message(const std::string& request);
    static bool validate_n8_message(const ParsedN8Message& message, const N8SubscriptionContext& ctx, std::string& error);
    static std::string build_n8_sbi_request(const std::string& imsi, const ParsedN8Message& message, const N8SubscriptionContext& ctx);
    static ParsedN11Message parse_n11_message(const std::string& operation);
    static bool validate_n11_message(const ParsedN11Message& message, const N11SessionContext& ctx, std::string& error);
    static std::string build_n11_sbi_request(const std::string& imsi, const ParsedN11Message& message, const N11SessionContext& ctx);
    static ParsedN12Message parse_n12_message(const std::string& request);
    static bool validate_n12_message(const ParsedN12Message& message, const N12AuthContext& ctx, std::string& error);
    static std::string build_n12_sbi_request(const std::string& imsi, const ParsedN12Message& message, const N12AuthContext& ctx);
    static ParsedN15Message parse_n15_message(const std::string& request);
    static bool validate_n15_message(const ParsedN15Message& message, const N15PolicyContext& ctx, std::string& error);
    static std::string build_n15_sbi_request(const std::string& imsi, const ParsedN15Message& message, const N15PolicyContext& ctx);
    static ParsedN22Message parse_n22_message(const std::string& request);
    static bool validate_n22_message(const ParsedN22Message& message, const N22SelectionContext& ctx, std::string& error);
    static std::string n22_procedure_name(N22ProcedureType procedure);
    static N22ResponseModel build_n22_success_response(const ParsedN22Message& message, const N22SelectionContext& ctx, const std::string& selection_result);
    static N22ResponseModel build_n22_error_response(const std::string& imsi, const std::string& cause);
    static void append_n22_response_fields(std::ostringstream& req, const N22ResponseModel& response);
    static std::string build_n22_sbi_request(const std::string& imsi, const ParsedN22Message& message, const N22SelectionContext& ctx, const std::string& selection_result);
    static std::string next_ran_ue_ngap_id(const std::string& imsi);
    static std::string next_amf_ue_ngap_id(const std::string& imsi);
    static std::string normalize_ngap_procedure(const std::string& value);
    static std::string normalize_n8_procedure(const std::string& value);
    static std::string normalize_n11_procedure(const std::string& value);
    static std::string normalize_n12_procedure(const std::string& value);
    static std::string normalize_n15_procedure(const std::string& value);
    static std::string normalize_n22_procedure(const std::string& value);
    static bool try_parse_u32(const std::string& value, std::uint32_t& out);
    static bool is_valid_snssai(const std::string& value);
    static bool list_contains_csv_token(const std::string& csv, const std::string& token);
    static std::string compute_nas_mac(const std::string& pdu_without_mac, std::uint32_t k_amf, std::uint32_t count);
    static std::string trim_copy(const std::string& value);
    bool send_n1_service_reject(const std::string& imsi, N1SecurityContext& ctx, const std::string& cause);
    bool send_n2_error_indication(const std::string& imsi, const std::string& cause);
    bool send_n8_error_indication(const std::string& imsi, const std::string& cause);
    bool send_n11_error_indication(const std::string& imsi, const std::string& cause);
    bool send_n12_error_indication(const std::string& imsi, const std::string& cause);
    bool send_n15_error_indication(const std::string& imsi, const std::string& cause);
    bool send_n22_error_indication(const std::string& imsi, const std::string& cause);

    IN1Interface* n1_;
    IN2Interface* n2_;
    ISbiInterface* sbi_;
    IN8Interface* n8_;
    IN11Interface* n11_;
    IN12Interface* n12_;
    IN15Interface* n15_;
    IN22Interface* n22_;
    static constexpr std::chrono::seconds kN1ProcedureTimeout {1};
    std::unordered_map<std::string, N1ProcedureState> n1_procedure_state_;
    std::unordered_map<std::string, N1SecurityContext> n1_security_context_;
    std::unordered_map<std::string, N2UeContext> n2_ue_context_;
    std::unordered_map<std::string, N8SubscriptionContext> n8_subscription_context_;
    std::unordered_map<std::string, N11SessionContext> n11_session_context_;
    std::unordered_map<std::string, N12AuthContext> n12_auth_context_;
    std::unordered_map<std::string, N15PolicyContext> n15_policy_context_;
    std::unordered_map<std::string, N22SelectionContext> n22_selection_context_;
    std::string mcc_ {"250"};
    std::string mnc_ {"03"};
};

}  // namespace amf
