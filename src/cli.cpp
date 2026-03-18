#include "amf/cli.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "amf/amf.hpp"
#include "amf/config/runtime_config.hpp"

namespace amf {

namespace {

bool is_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool try_parse_u64(const std::string& value, std::uint64_t& output) {
    if (value.empty() || !is_digits(value)) {
        return false;
    }

    try {
        output = std::stoull(value);
    } catch (...) {
        return false;
    }

    return output > 0;
}

bool is_valid_owner_id(const std::string& value) {
    if (value.empty() || value.size() > 32) {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
    });
}

const char* to_string(SessionRole role) {
    return role == SessionRole::Admin ? "admin" : "operator";
}

const char* to_string(Permission permission) {
    switch (permission) {
        case Permission::CandidateLock:
            return "candidate-lock";
        case Permission::CandidateRenew:
            return "candidate-renew";
        case Permission::CandidateUnlock:
            return "candidate-unlock";
        case Permission::Rollback:
            return "rollback";
        case Permission::Commit:
            return "commit";
        case Permission::CommitConfirmed:
            return "commit-confirmed";
        case Permission::ConfirmCommit:
            return "confirm-commit";
        case Permission::ForceUnlock:
            return "force-unlock";
        case Permission::PolicyReload:
            return "policy-reload";
        case Permission::SessionRoleChange:
            return "session-role-change";
        case Permission::SessionOwnerChange:
            return "session-owner-change";
        case Permission::ShowPolicy:
            return "show-policy";
    }

    return "unknown";
}

bool try_parse_role(const std::string& value, SessionRole& role) {
    if (value == "admin") {
        role = SessionRole::Admin;
        return true;
    }
    if (value == "operator") {
        role = SessionRole::Operator;
        return true;
    }

    return false;
}

bool try_parse_permission(const std::string& value, Permission& permission) {
    if (value == "candidate_lock" || value == "candidate-lock") {
        permission = Permission::CandidateLock;
        return true;
    }
    if (value == "candidate_renew" || value == "candidate-renew") {
        permission = Permission::CandidateRenew;
        return true;
    }
    if (value == "candidate_unlock" || value == "candidate-unlock") {
        permission = Permission::CandidateUnlock;
        return true;
    }
    if (value == "rollback" || value == "discard") {
        permission = Permission::Rollback;
        return true;
    }
    if (value == "commit") {
        permission = Permission::Commit;
        return true;
    }
    if (value == "commit_confirmed" || value == "commit-confirmed") {
        permission = Permission::CommitConfirmed;
        return true;
    }
    if (value == "confirm_commit" || value == "confirm-commit") {
        permission = Permission::ConfirmCommit;
        return true;
    }
    if (value == "force_unlock" || value == "force-unlock") {
        permission = Permission::ForceUnlock;
        return true;
    }
    if (value == "policy_reload" || value == "policy-reload") {
        permission = Permission::PolicyReload;
        return true;
    }
    if (value == "session_role_change" || value == "session-role-change") {
        permission = Permission::SessionRoleChange;
        return true;
    }
    if (value == "session_owner_change" || value == "session-owner-change") {
        permission = Permission::SessionOwnerChange;
        return true;
    }
    if (value == "show_policy" || value == "show-policy") {
        permission = Permission::ShowPolicy;
        return true;
    }

    return false;
}

const std::vector<Permission>& all_permissions() {
    static const std::vector<Permission> permissions = {
        Permission::CandidateLock,
        Permission::CandidateRenew,
        Permission::CandidateUnlock,
        Permission::Rollback,
        Permission::Commit,
        Permission::CommitConfirmed,
        Permission::ConfirmCommit,
        Permission::ForceUnlock,
        Permission::PolicyReload,
        Permission::SessionRoleChange,
        Permission::SessionOwnerChange,
        Permission::ShowPolicy,
    };

    return permissions;
}

bool try_parse_allow_deny(const std::string& value, bool& allow) {
    if (value == "allow") {
        allow = true;
        return true;
    }
    if (value == "deny") {
        allow = false;
        return true;
    }

    return false;
}

bool is_valid_ipv4(const std::string& ip) {
    std::istringstream parts(ip);
    std::string part;
    int count = 0;

    while (std::getline(parts, part, '.')) {
        if (part.empty() || part.size() > 3 || !is_digits(part)) {
            return false;
        }

        int value = std::stoi(part);
        if (value < 0 || value > 255) {
            return false;
        }

        ++count;
    }

    return count == 4;
}

bool try_parse_port(const std::string& value, std::uint16_t& port) {
    if (value.empty() || !is_digits(value)) {
        return false;
    }

    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value);
    } catch (...) {
        return false;
    }

    if (parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool is_valid_nf_instance(const std::string& value) {
    if (value.empty() || value.size() > 32) {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
    });
}

bool is_valid_mcc(const std::string& value) {
    return value.size() == 3 && is_digits(value);
}

bool is_valid_mnc(const std::string& value) {
    return value.size() == 2 && is_digits(value);
}

bool same_config(const AmfCliConfig& lhs, const AmfCliConfig& rhs) {
    return lhs.mcc == rhs.mcc && lhs.mnc == rhs.mnc && lhs.n2.local_address == rhs.n2.local_address
        && lhs.n2.port == rhs.n2.port && lhs.sbi.bind_address == rhs.sbi.bind_address
        && lhs.sbi.port == rhs.sbi.port && lhs.sbi.nf_instance == rhs.sbi.nf_instance;
}

std::string parent_directory(const std::string& path) {
    const auto slash_pos = path.find_last_of("/\\");
    if (slash_pos == std::string::npos) {
        return {};
    }

    return path.substr(0, slash_pos);
}

bool is_absolute_path(const std::string& path) {
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return true;
    }

    return !path.empty() && (path[0] == '/' || path[0] == '\\');
}

std::string resolve_path(const std::string& path, const std::string& base_dir) {
    if (path.empty() || is_absolute_path(path) || base_dir.empty()) {
        return path;
    }

    return base_dir + "/" + path;
}

}  // namespace

CliShell::CliShell(
    IAmfNode& node,
    std::istream& in,
    std::ostream& out,
    std::optional<AmfCliConfig> initial_cfg,
    std::string policy_path,
        std::string audit_log_path,
        std::string runtime_config_path)
        : node_(node), in_(in), out_(out), policy_table_path_(std::move(policy_path)), audit_log_path_(std::move(audit_log_path)),
            runtime_config_path_(std::move(runtime_config_path)) {
    init_default_policy_table();
    load_policy_table(policy_table_path_);

    if (initial_cfg.has_value()) {
        running_cfg_ = initial_cfg.value();
    } else {
        const auto st = node_.status();
        running_cfg_.mcc = st.mcc;
        running_cfg_.mnc = st.mnc;
    }

    reset_candidate_from_running();
}

void CliShell::run() {
    out_ << "EricssonSoftware-like AMF CLI (exec/config tree). Type 'help' for commands.\n";

    std::string line;
    while (running_) {
        process_timers();
        out_ << prompt();
        if (!std::getline(in_, line)) {
            running_ = false;
            break;
        }

        if (line.empty()) {
            continue;
        }

        execute_line(line);
    }
}

bool CliShell::execute_line(const std::string& line) {
    const auto tokens = tokenize(line);
    if (tokens.empty()) {
        return false;
    }

    if (tokens[0] == "help") {
        print_help();
        return true;
    }

    if (mode_ == CliMode::Exec && (tokens[0] == "exit" || tokens[0] == "quit")) {
        running_ = false;
        out_ << "CLI session ended.\n";
        return true;
    }

    if (tokens[0] == "end") {
        mode_ = CliMode::Exec;
        return true;
    }

    if (tokens[0] == "exit") {
        if (mode_ == CliMode::Config) {
            mode_ = CliMode::Exec;
            return true;
        }
        if (mode_ == CliMode::ConfigAmf) {
            mode_ = CliMode::Config;
            return true;
        }
        if (mode_ == CliMode::ConfigAmfN2 || mode_ == CliMode::ConfigAmfSbi) {
            mode_ = CliMode::ConfigAmf;
            return true;
        }
    }

    if (tokens.size() == 2 && tokens[0] == "configure" && tokens[1] == "terminal") {
        reset_candidate_from_running();
        mode_ = CliMode::Config;
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "conf" && tokens[1] == "t") {
        reset_candidate_from_running();
        mode_ = CliMode::Config;
        return true;
    }

    if (mode_ == CliMode::Exec) {
        return execute_exec_tokens(tokens);
    }

    if (mode_ == CliMode::Config) {
        return execute_config_tokens(tokens);
    }

    if (mode_ == CliMode::ConfigAmf) {
        return execute_config_amf_tokens(tokens);
    }

    if (mode_ == CliMode::ConfigAmfN2) {
        return execute_config_amf_n2_tokens(tokens);
    }

    return execute_config_amf_sbi_tokens(tokens);
}

bool CliShell::execute_exec_tokens(const std::vector<std::string>& tokens) {
    const bool is_runtime_config_cmd = tokens.size() >= 2 && tokens.size() <= 3
        && (tokens[0] == "runtime-config" || tokens[0] == "runtime_config" || tokens[0] == "runtimeconfig")
        && tokens[1] == "reload";

    if (is_runtime_config_cmd) {
            if (!has_permission(Permission::PolicyReload)) {
                out_ << "Runtime config reload rejected by policy for role " << to_string(active_role_) << ".\n";
                audit_event("runtime-config-reload", false, "permission-denied");
                return false;
            }

            std::string path = runtime_config_path_;
            if (tokens.size() == 3) {
                path = tokens[2];
            }

            if (path.empty()) {
                out_ << "Runtime config path not set. Use: runtime-config reload <path>\n";
                audit_event("runtime-config-reload", false, "empty-path");
                return false;
            }

            const bool reloaded = reload_runtime_config(path);
            out_ << (reloaded ? "Runtime config reloaded.\n" : "Runtime config reload failed.\n");
            return reloaded;
        }

    if (tokens.size() == 2 && tokens[0] == "show" && tokens[1] == "session") {
        out_ << "Active owner    : " << active_owner_id_ << "\n";
        out_ << "Active role     : " << to_string(active_role_) << "\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "show" && tokens[1] == "policy") {
        if (!has_permission(Permission::ShowPolicy)) {
            out_ << "Show policy rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        print_policy_table();
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "policy" && tokens[1] == "reload") {
        if (!has_permission(Permission::PolicyReload)) {
            out_ << "Policy reload rejected by policy for role " << to_string(active_role_) << ".\n";
            audit_event("policy-reload", false, "permission-denied");
            return false;
        }

        const bool reloaded = load_policy_table(policy_table_path_);
        out_ << (reloaded ? "Policy table reloaded.\n" : "Policy reload failed.\n");
        audit_event("policy-reload", reloaded, policy_table_path_);
        return reloaded;
    }

    if (tokens.size() == 3 && tokens[0] == "session" && tokens[1] == "owner") {
        if (!has_permission(Permission::SessionOwnerChange)) {
            out_ << "Session owner change rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        if (!is_valid_owner_id(tokens[2])) {
            out_ << "Owner rejected. Use [A-Za-z0-9_-], max length 32.\n";
            return false;
        }

        active_owner_id_ = tokens[2];
        out_ << "Active owner switched to " << active_owner_id_ << ".\n";
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "session" && tokens[1] == "role") {
        if (!has_permission(Permission::SessionRoleChange)) {
            out_ << "Session role change rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        SessionRole role = SessionRole::Operator;
        if (!try_parse_role(tokens[2], role)) {
            out_ << "Role rejected. Use: session role operator|admin\n";
            return false;
        }

        active_role_ = role;
        out_ << "Active role switched to " << to_string(active_role_) << ".\n";
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "running" && tokens[2] == "config") {
        print_running_config();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "candidate") {
        print_candidate_config();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "diff") {
        print_config_diff();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "lock") {
        print_lock_status();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "amf" && tokens[2] == "status") {
        const auto st = node_.status();
        out_ << "AMF state      : " << to_string(st.state) << "\n";
        out_ << "PLMN           : " << st.mcc << '-' << st.mnc << "\n";
        out_ << "N2 bind        : " << running_cfg_.n2.local_address << ':' << running_cfg_.n2.port << "\n";
        out_ << "SBI bind       : " << running_cfg_.sbi.bind_address << ':' << running_cfg_.sbi.port << "\n";
        out_ << "SBI nf-id      : " << running_cfg_.sbi.nf_instance << "\n";
        out_ << "Active UE      : " << st.active_ue << "\n";
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "amf" && tokens[2] == "stats") {
        const auto st = node_.status();
        out_ << "starts         : " << st.stats.starts << "\n";
        out_ << "stops          : " << st.stats.stops << "\n";
        out_ << "heartbeat      : " << st.stats.heartbeat_ticks << "\n";
        out_ << "ue registers   : " << st.stats.ue_registrations << "\n";
        out_ << "ue deregisters : " << st.stats.ue_deregistrations << "\n";
        out_ << "n2 messages    : " << st.stats.n2_signaling_messages << "\n";
        out_ << "sbi notify     : " << st.stats.sbi_notifications << "\n";
        out_ << "n1 messages    : " << st.stats.n1_messages << "\n";
        out_ << "n3 packets     : " << st.stats.n3_user_plane_packets << "\n";
        out_ << "n8 queries     : " << st.stats.n8_subscription_queries << "\n";
        out_ << "n11 pdu ops    : " << st.stats.n11_pdu_session_ops << "\n";
        out_ << "n12 auth req   : " << st.stats.n12_auth_requests << "\n";
        out_ << "n14 xfers      : " << st.stats.n14_context_transfers << "\n";
        out_ << "n15 policies   : " << st.stats.n15_policy_queries << "\n";
        out_ << "n22 slices     : " << st.stats.n22_slice_selections << "\n";
        out_ << "n26 iwk ops    : " << st.stats.n26_interworking_ops << "\n";
        return true;
    }

    if (tokens.size() >= 3 && tokens[0] == "show" && tokens[1] == "amf" && tokens[2] == "ue") {
        if (tokens.size() == 3) {
            const auto ue_list = node_.list_ue();
            if (ue_list.empty()) {
                out_ << "No UE context entries.\n";
                return true;
            }

            for (const auto& ue : ue_list) {
                out_ << ue.imsi << " tai=" << ue.tai << " state=" << to_string(ue.state)
                     << " last_seen=" << ue.last_seen_utc << "\n";
            }
            return true;
        }

        if (tokens.size() == 4) {
            const auto ue = node_.find_ue(tokens[3]);
            if (!ue.has_value()) {
                out_ << "UE not found.\n";
                return false;
            }

            out_ << ue->imsi << " tai=" << ue->tai << " state=" << to_string(ue->state)
                 << " last_seen=" << ue->last_seen_utc << "\n";
            return true;
        }
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "amf" && tokens[2] == "interfaces") {
        const auto interfaces = node_.list_interfaces();
        out_ << "AMF interface inventory:\n";
        for (const auto& iface : interfaces) {
            out_ << "  " << iface.name << " [" << iface.plane << "] : "
                 << (iface.configured ? "configured" : "detached") << "\n";
        }
        return true;
    }

    if (tokens.size() == 4 && tokens[0] == "show" && tokens[1] == "amf" && tokens[2] == "interfaces" && tokens[3] == "detail") {
        const auto diagnostics = node_.list_interface_diagnostics();
        out_ << "AMF interface diagnostics:\n";
        for (const auto& diag : diagnostics) {
            const std::size_t attempts = diag.success_count + diag.error_count;
            const double error_rate = attempts == 0 ? 0.0 : (static_cast<double>(diag.error_count) * 100.0 / static_cast<double>(attempts));
              std::ostringstream rate_ss;
              rate_ss << std::fixed << std::setprecision(1) << error_rate;
            out_ << "  " << diag.name << " [" << diag.plane << "]"
                 << " avail=" << (diag.available ? "UP" : "DOWN")
                 << " configured=" << (diag.configured ? "yes" : "no")
                 << " success=" << diag.success_count
                 << " errors=" << diag.error_count
                  << " error-rate=" << rate_ss.str() << "%"
                 << " reason=" << diag.status_reason
                  << " alarm=" << diag.alarm_level
                 << " last-activity=" << diag.last_activity_utc << "\n";
        }
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "amf") {
        if (tokens[1] == "start") {
            out_ << (node_.start() ? "AMF started.\n" : "AMF start rejected.\n");
            return true;
        }
        if (tokens[1] == "stop") {
            out_ << (node_.stop() ? "AMF stopped.\n" : "AMF stop rejected.\n");
            return true;
        }
        if (tokens[1] == "degrade") {
            out_ << (node_.set_degraded() ? "AMF moved to DEGRADED.\n" : "AMF degrade rejected.\n");
            return true;
        }
        if (tokens[1] == "recover") {
            out_ << (node_.recover() ? "AMF moved to RUNNING.\n" : "AMF recover rejected.\n");
            return true;
        }
        if (tokens[1] == "tick") {
            node_.tick();
            out_ << "Heartbeat tick accepted.\n";
            return true;
        }
    }

    if (tokens.size() == 4 && tokens[0] == "ue" && tokens[1] == "register") {
        out_ << (node_.register_ue(tokens[2], tokens[3]) ? "UE registered.\n" : "UE register rejected.\n");
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "ue" && tokens[1] == "deregister") {
        out_ << (node_.deregister_ue(tokens[2]) ? "UE deregistered.\n" : "UE deregister rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n2") {
        const auto payload = join_tail(tokens, 3);
        out_ << (node_.send_n2_nas(tokens[2], payload) ? "N2 message sent.\n" : "N2 send rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "sbi") {
        const auto payload = join_tail(tokens, 3);
        out_ << (node_.notify_sbi(tokens[2], payload) ? "SBI notification sent.\n" : "SBI notify rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n1") {
        const auto payload = join_tail(tokens, 3);
        out_ << (node_.send_n1_nas(tokens[2], payload) ? "N1 NAS sent.\n" : "N1 NAS rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n3") {
        const auto payload = join_tail(tokens, 3);
        out_ << (node_.forward_n3_user_plane(tokens[2], payload) ? "N3 user-plane forwarded.\n" : "N3 forwarding rejected.\n");
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "simulate" && tokens[1] == "n8") {
        out_ << (node_.query_n8_subscription(tokens[2]) ? "N8 subscription query sent.\n" : "N8 query rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n11") {
        const auto operation = join_tail(tokens, 3);
        out_ << (node_.manage_n11_pdu_session(tokens[2], operation) ? "N11 PDU session operation sent.\n" : "N11 operation rejected.\n");
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "simulate" && tokens[1] == "n12") {
        out_ << (node_.authenticate_n12(tokens[2]) ? "N12 authentication sent.\n" : "N12 authentication rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n14") {
        const auto target = join_tail(tokens, 3);
        out_ << (node_.transfer_n14_context(tokens[2], target) ? "N14 context transfer sent.\n" : "N14 transfer rejected.\n");
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "simulate" && tokens[1] == "n15") {
        out_ << (node_.query_n15_policy(tokens[2]) ? "N15 policy query sent.\n" : "N15 query rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n22") {
        const auto snssai = join_tail(tokens, 3);
        out_ << (node_.select_n22_slice(tokens[2], snssai) ? "N22 slice selection sent.\n" : "N22 selection rejected.\n");
        return true;
    }

    if (tokens.size() >= 4 && tokens[0] == "simulate" && tokens[1] == "n26") {
        const auto op = join_tail(tokens, 3);
        out_ << (node_.interwork_n26(tokens[2], op) ? "N26 interworking sent.\n" : "N26 interworking rejected.\n");
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "clear" && tokens[1] == "stats") {
        node_.clear_stats();
        out_ << "AMF stats reset.\n";
        return true;
    }

    out_ << "Unknown command. Type 'help'.\n";
    return false;
}

bool CliShell::execute_config_tokens(const std::vector<std::string>& tokens) {
    const bool is_runtime_config_cmd = tokens.size() >= 2 && tokens.size() <= 3
        && (tokens[0] == "runtime-config" || tokens[0] == "runtime_config" || tokens[0] == "runtimeconfig")
        && tokens[1] == "reload";

    if (is_runtime_config_cmd) {
            if (!has_permission(Permission::PolicyReload)) {
                out_ << "Runtime config reload rejected by policy for role " << to_string(active_role_) << ".\n";
                audit_event("runtime-config-reload", false, "permission-denied");
                return false;
            }

            std::string path = runtime_config_path_;
            if (tokens.size() == 3) {
                path = tokens[2];
            }

            if (path.empty()) {
                out_ << "Runtime config path not set. Use: runtime-config reload <path>\n";
                audit_event("runtime-config-reload", false, "empty-path");
                return false;
            }

            const bool reloaded = reload_runtime_config(path);
            out_ << (reloaded ? "Runtime config reloaded.\n" : "Runtime config reload failed.\n");
            return reloaded;
        }

    if (tokens.size() == 2 && tokens[0] == "show" && tokens[1] == "session") {
        out_ << "Active owner    : " << active_owner_id_ << "\n";
        out_ << "Active role     : " << to_string(active_role_) << "\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "show" && tokens[1] == "policy") {
        if (!has_permission(Permission::ShowPolicy)) {
            out_ << "Show policy rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        print_policy_table();
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "policy" && tokens[1] == "reload") {
        if (!has_permission(Permission::PolicyReload)) {
            out_ << "Policy reload rejected by policy for role " << to_string(active_role_) << ".\n";
            audit_event("policy-reload", false, "permission-denied");
            return false;
        }

        const bool reloaded = load_policy_table(policy_table_path_);
        out_ << (reloaded ? "Policy table reloaded.\n" : "Policy reload failed.\n");
        audit_event("policy-reload", reloaded, policy_table_path_);
        return reloaded;
    }

    if (tokens.size() == 3 && tokens[0] == "session" && tokens[1] == "owner") {
        if (!has_permission(Permission::SessionOwnerChange)) {
            out_ << "Session owner change rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        if (!is_valid_owner_id(tokens[2])) {
            out_ << "Owner rejected. Use [A-Za-z0-9_-], max length 32.\n";
            return false;
        }

        active_owner_id_ = tokens[2];
        out_ << "Active owner switched to " << active_owner_id_ << ".\n";
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "session" && tokens[1] == "role") {
        if (!has_permission(Permission::SessionRoleChange)) {
            out_ << "Session role change rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        SessionRole role = SessionRole::Operator;
        if (!try_parse_role(tokens[2], role)) {
            out_ << "Role rejected. Use: session role operator|admin\n";
            return false;
        }

        active_role_ = role;
        out_ << "Active role switched to " << to_string(active_role_) << ".\n";
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "running" && tokens[2] == "config") {
        print_running_config();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "candidate") {
        print_candidate_config();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "diff") {
        print_config_diff();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "show" && tokens[1] == "configuration" && tokens[2] == "lock") {
        print_lock_status();
        return true;
    }

    if (tokens.size() == 3 && tokens[0] == "candidate" && tokens[1] == "lock") {
        if (!has_permission(Permission::CandidateLock)) {
            out_ << "Candidate lock rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        std::uint64_t ttl_seconds = 0;
        if (!try_parse_u64(tokens[2], ttl_seconds)) {
            out_ << "Candidate lock rejected. Expected: candidate lock <ttl-seconds>.\n";
            return false;
        }

        const bool locked = acquire_candidate_lock(ttl_seconds);
        out_ << (locked ? "Candidate lock acquired.\n" : "Candidate lock held by different owner.\n");
        return locked;
    }

    if (tokens.size() == 2 && tokens[0] == "candidate" && tokens[1] == "unlock") {
        if (!has_permission(Permission::CandidateUnlock)) {
            out_ << "Candidate unlock rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool unlocked = release_candidate_lock();
        out_ << (unlocked ? "Candidate lock released.\n" : "Candidate unlock rejected. Not lock owner.\n");
        return unlocked;
    }

    if (tokens.size() == 3 && tokens[0] == "candidate" && tokens[1] == "renew") {
        if (!has_permission(Permission::CandidateRenew)) {
            out_ << "Candidate renew rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        std::uint64_t ttl_seconds = 0;
        if (!try_parse_u64(tokens[2], ttl_seconds)) {
            out_ << "Candidate renew rejected. Expected: candidate renew <ttl-seconds>.\n";
            return false;
        }

        const bool renewed = renew_candidate_lock(ttl_seconds);
        out_ << (renewed ? "Candidate lock renewed.\n" : "Candidate renew rejected. Not lock owner.\n");
        return renewed;
    }

    if (tokens.size() == 2 && tokens[0] == "candidate" && tokens[1] == "force-unlock") {
        if (!has_permission(Permission::ForceUnlock)) {
            out_ << "Candidate force-unlock rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        release_candidate_lock_force();
        out_ << "Candidate lock force-released.\n";
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "amf") {
        mode_ = CliMode::ConfigAmf;
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "commit") {
        const bool committed = commit_candidate();
        out_ << (committed ? "Commit complete.\n" : "Commit failed.\n");
        return committed;
    }

    if (tokens.size() == 3 && tokens[0] == "commit" && tokens[1] == "confirmed") {
        if (!has_permission(Permission::CommitConfirmed)) {
            out_ << "Commit confirmed rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        std::uint64_t seconds = 0;
        if (!try_parse_u64(tokens[2], seconds)) {
            out_ << "Commit confirmed rejected. Expected: commit confirmed <seconds>.\n";
            return false;
        }

        const bool committed = commit_candidate_confirmed(seconds);
        out_ << (committed ? "Commit confirmed accepted. Use 'confirm' to finalize.\n" : "Commit confirmed failed.\n");
        return committed;
    }

    if (tokens.size() == 1 && tokens[0] == "confirm") {
        if (!has_permission(Permission::ConfirmCommit)) {
            out_ << "Confirm commit rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool confirmed = confirm_commit();
        out_ << (confirmed ? "Commit confirmed finalized.\n" : "No pending confirmed commit.\n");
        return confirmed;
    }

    if (tokens.size() == 1 && (tokens[0] == "rollback" || tokens[0] == "discard")) {
        if (!has_permission(Permission::Rollback)) {
            out_ << "Rollback rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool rolled_back = rollback_candidate_to_running();
        out_ << (rolled_back ? "Candidate configuration rolled back.\n" : "Candidate rollback rejected. Not lock owner.\n");
        return rolled_back;
    }

    if (tokens.size() == 3 && tokens[0] == "plmn") {
        std::vector<std::string> forwarded = {"plmn", tokens[1], tokens[2]};
        return execute_config_amf_tokens(forwarded);
    }

    if (tokens.size() == 2 && (tokens[0] == "mcc" || tokens[0] == "mnc")) {
        std::vector<std::string> forwarded = {tokens[0], tokens[1]};
        return execute_config_amf_tokens(forwarded);
    }

    if (!tokens.empty() && tokens[0] == "do") {
        std::vector<std::string> exec_tokens(tokens.begin() + 1, tokens.end());
        if (exec_tokens.empty()) {
            out_ << "Unknown command. Type 'help'.\n";
            return false;
        }
        return execute_exec_tokens(exec_tokens);
    }

    out_ << "Unknown command. Type 'help'.\n";
    return false;
}

bool CliShell::execute_config_amf_tokens(const std::vector<std::string>& tokens) {
    if (tokens.size() == 1 && tokens[0] == "n2") {
        mode_ = CliMode::ConfigAmfN2;
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "sbi") {
        mode_ = CliMode::ConfigAmfSbi;
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "commit") {
        const bool committed = commit_candidate();
        out_ << (committed ? "Commit complete.\n" : "Commit failed.\n");
        return committed;
    }

    if (tokens.size() == 3 && tokens[0] == "commit" && tokens[1] == "confirmed") {
        if (!has_permission(Permission::CommitConfirmed)) {
            out_ << "Commit confirmed rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        std::uint64_t seconds = 0;
        if (!try_parse_u64(tokens[2], seconds)) {
            out_ << "Commit confirmed rejected. Expected: commit confirmed <seconds>.\n";
            return false;
        }

        const bool committed = commit_candidate_confirmed(seconds);
        out_ << (committed ? "Commit confirmed accepted. Use 'confirm' to finalize.\n" : "Commit confirmed failed.\n");
        return committed;
    }

    if (tokens.size() == 1 && tokens[0] == "confirm") {
        if (!has_permission(Permission::ConfirmCommit)) {
            out_ << "Confirm commit rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool confirmed = confirm_commit();
        out_ << (confirmed ? "Commit confirmed finalized.\n" : "No pending confirmed commit.\n");
        return confirmed;
    }

    if (tokens.size() == 1 && (tokens[0] == "rollback" || tokens[0] == "discard")) {
        if (!has_permission(Permission::Rollback)) {
            out_ << "Rollback rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool rolled_back = rollback_candidate_to_running();
        out_ << (rolled_back ? "Candidate configuration rolled back.\n" : "Candidate rollback rejected. Not lock owner.\n");
        return rolled_back;
    }

    if (tokens.size() == 3 && tokens[0] == "plmn") {
        if (!ensure_lock_for_modify()) {
            return false;
        }

        if (!is_valid_mcc(tokens[1]) || !is_valid_mnc(tokens[2])) {
            out_ << "PLMN rejected. Expected: plmn <mcc(3)> <mnc(2)>.\n";
            return false;
        }

        candidate_cfg_.mcc = tokens[1];
        candidate_cfg_.mnc = tokens[2];
        out_ << "Candidate PLMN updated.\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "mcc") {
        if (!ensure_lock_for_modify()) {
            return false;
        }

        if (!is_valid_mcc(tokens[1])) {
            out_ << "MCC rejected. Expected 3 digits.\n";
            return false;
        }

        candidate_cfg_.mcc = tokens[1];
        out_ << "Candidate MCC updated.\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "mnc") {
        if (!ensure_lock_for_modify()) {
            return false;
        }

        if (!is_valid_mnc(tokens[1])) {
            out_ << "MNC rejected. Expected 2 digits.\n";
            return false;
        }

        candidate_cfg_.mnc = tokens[1];
        out_ << "Candidate MNC updated.\n";
        return true;
    }

    if (!tokens.empty() && tokens[0] == "do") {
        std::vector<std::string> exec_tokens(tokens.begin() + 1, tokens.end());
        if (exec_tokens.empty()) {
            out_ << "Unknown command. Type 'help'.\n";
            return false;
        }
        return execute_exec_tokens(exec_tokens);
    }

    out_ << "Unknown command. Type 'help'.\n";
    return false;
}

bool CliShell::execute_config_amf_n2_tokens(const std::vector<std::string>& tokens) {
    if (tokens.size() == 2 && tokens[0] == "local-address") {
        if (!ensure_lock_for_modify()) {
            return false;
        }
        if (!is_valid_ipv4(tokens[1])) {
            out_ << "N2 local-address rejected. Expected IPv4.\n";
            return false;
        }

        candidate_cfg_.n2.local_address = tokens[1];
        out_ << "Candidate N2 local-address updated.\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "port") {
        if (!ensure_lock_for_modify()) {
            return false;
        }
        std::uint16_t port = 0;
        if (!try_parse_port(tokens[1], port)) {
            out_ << "N2 port rejected. Expected 1..65535.\n";
            return false;
        }

        candidate_cfg_.n2.port = port;
        out_ << "Candidate N2 port updated.\n";
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "commit") {
        const bool committed = commit_candidate();
        out_ << (committed ? "Commit complete.\n" : "Commit failed.\n");
        return committed;
    }

    if (tokens.size() == 1 && (tokens[0] == "rollback" || tokens[0] == "discard")) {
        if (!has_permission(Permission::Rollback)) {
            out_ << "Rollback rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool rolled_back = rollback_candidate_to_running();
        out_ << (rolled_back ? "Candidate configuration rolled back.\n" : "Candidate rollback rejected. Not lock owner.\n");
        return rolled_back;
    }

    if (!tokens.empty() && tokens[0] == "do") {
        std::vector<std::string> exec_tokens(tokens.begin() + 1, tokens.end());
        if (exec_tokens.empty()) {
            out_ << "Unknown command. Type 'help'.\n";
            return false;
        }
        return execute_exec_tokens(exec_tokens);
    }

    out_ << "Unknown command. Type 'help'.\n";
    return false;
}

bool CliShell::execute_config_amf_sbi_tokens(const std::vector<std::string>& tokens) {
    if (tokens.size() == 2 && tokens[0] == "bind-address") {
        if (!ensure_lock_for_modify()) {
            return false;
        }
        if (!is_valid_ipv4(tokens[1])) {
            out_ << "SBI bind-address rejected. Expected IPv4.\n";
            return false;
        }

        candidate_cfg_.sbi.bind_address = tokens[1];
        out_ << "Candidate SBI bind-address updated.\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "port") {
        if (!ensure_lock_for_modify()) {
            return false;
        }
        std::uint16_t port = 0;
        if (!try_parse_port(tokens[1], port)) {
            out_ << "SBI port rejected. Expected 1..65535.\n";
            return false;
        }

        candidate_cfg_.sbi.port = port;
        out_ << "Candidate SBI port updated.\n";
        return true;
    }

    if (tokens.size() == 2 && tokens[0] == "nf-instance") {
        if (!ensure_lock_for_modify()) {
            return false;
        }
        if (!is_valid_nf_instance(tokens[1])) {
            out_ << "SBI nf-instance rejected. Use [A-Za-z0-9_-], max length 32.\n";
            return false;
        }

        candidate_cfg_.sbi.nf_instance = tokens[1];
        out_ << "Candidate SBI nf-instance updated.\n";
        return true;
    }

    if (tokens.size() == 1 && tokens[0] == "commit") {
        const bool committed = commit_candidate();
        out_ << (committed ? "Commit complete.\n" : "Commit failed.\n");
        return committed;
    }

    if (tokens.size() == 1 && (tokens[0] == "rollback" || tokens[0] == "discard")) {
        if (!has_permission(Permission::Rollback)) {
            out_ << "Rollback rejected by policy for role " << to_string(active_role_) << ".\n";
            return false;
        }

        const bool rolled_back = rollback_candidate_to_running();
        out_ << (rolled_back ? "Candidate configuration rolled back.\n" : "Candidate rollback rejected. Not lock owner.\n");
        return rolled_back;
    }

    if (!tokens.empty() && tokens[0] == "do") {
        std::vector<std::string> exec_tokens(tokens.begin() + 1, tokens.end());
        if (exec_tokens.empty()) {
            out_ << "Unknown command. Type 'help'.\n";
            return false;
        }
        return execute_exec_tokens(exec_tokens);
    }

    out_ << "Unknown command. Type 'help'.\n";
    return false;
}

bool CliShell::commit_candidate() {
    if (!has_permission(Permission::Commit)) {
        out_ << "Commit rejected by policy for role " << to_string(active_role_) << ".\n";
        audit_event("commit", false, "permission-denied");
        return false;
    }

    if (!ensure_lock_for_modify()) {
        audit_event("commit", false, "lock-required-or-owner-mismatch");
        return false;
    }

    if (same_config(candidate_cfg_, running_cfg_)) {
        audit_event("commit", true, "no-op");
        return true;
    }

    const bool updated = node_.set_plmn(candidate_cfg_.mcc, candidate_cfg_.mnc);
    if (!updated) {
        audit_event("commit", false, "node-set-plmn-rejected");
        return false;
    }

    running_cfg_ = candidate_cfg_;
    rollback_snapshot_.reset();
    rollback_deadline_.reset();
    audit_event("commit", true, "plmn=" + running_cfg_.mcc + "-" + running_cfg_.mnc);
    return true;
}

bool CliShell::commit_candidate_confirmed(std::uint64_t seconds) {
    if (!has_permission(Permission::Commit)) {
        out_ << "Commit rejected by policy for role " << to_string(active_role_) << ".\n";
        audit_event("commit-confirmed", false, "permission-denied");
        return false;
    }

    if (!ensure_lock_for_modify()) {
        audit_event("commit-confirmed", false, "lock-required-or-owner-mismatch");
        return false;
    }

    if (seconds == 0) {
        audit_event("commit-confirmed", false, "invalid-timeout");
        return false;
    }

    const auto previous = running_cfg_;
    if (!commit_candidate()) {
        audit_event("commit-confirmed", false, "commit-stage-failed");
        return false;
    }

    rollback_snapshot_ = previous;
    rollback_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    audit_event("commit-confirmed", true, "timeout-seconds=" + std::to_string(seconds));
    return true;
}

bool CliShell::confirm_commit() {
    if (!rollback_deadline_.has_value()) {
        audit_event("confirm-commit", false, "no-pending-confirmed-commit");
        return false;
    }

    rollback_snapshot_.reset();
    rollback_deadline_.reset();
    audit_event("confirm-commit", true);
    return true;
}

bool CliShell::rollback_candidate_to_running() {
    if (is_candidate_lock_active() && (!candidate_lock_owner_id_.has_value() || candidate_lock_owner_id_.value() != active_owner_id_)) {
        audit_event("rollback", false, "lock-owner-mismatch");
        return false;
    }

    reset_candidate_from_running();
    audit_event("rollback", true);
    return true;
}

void CliShell::rollback_confirmed_commit() {
    if (!rollback_snapshot_.has_value()) {
        return;
    }

    const auto previous = rollback_snapshot_.value();
    if (node_.set_plmn(previous.mcc, previous.mnc)) {
        running_cfg_ = previous;
        candidate_cfg_ = running_cfg_;
    }

    rollback_snapshot_.reset();
    rollback_deadline_.reset();
    audit_event("auto-rollback", true);
}

void CliShell::reset_candidate_from_running() {
    candidate_cfg_ = running_cfg_;
}

bool CliShell::acquire_candidate_lock(std::uint64_t ttl_seconds) {
    if (ttl_seconds == 0) {
        audit_event("candidate-lock", false, "invalid-ttl");
        return false;
    }

    if (is_candidate_lock_active()) {
        if (!candidate_lock_owner_id_.has_value() || candidate_lock_owner_id_.value() != active_owner_id_) {
            audit_event("candidate-lock", false, "already-owned-by-another");
            return false;
        }

        candidate_lock_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
        audit_event("candidate-lock", true, "renewed ttl=" + std::to_string(ttl_seconds));
        return true;
    }

    candidate_lock_owner_id_ = active_owner_id_;
    candidate_lock_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
    audit_event("candidate-lock", true, "ttl=" + std::to_string(ttl_seconds));
    return true;
}

bool CliShell::renew_candidate_lock(std::uint64_t ttl_seconds) {
    if (ttl_seconds == 0 || !is_candidate_lock_active()) {
        audit_event("candidate-renew", false, "invalid-ttl-or-no-active-lock");
        return false;
    }

    if (!candidate_lock_owner_id_.has_value() || candidate_lock_owner_id_.value() != active_owner_id_) {
        audit_event("candidate-renew", false, "lock-owner-mismatch");
        return false;
    }

    candidate_lock_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
    audit_event("candidate-renew", true, "ttl=" + std::to_string(ttl_seconds));
    return true;
}

bool CliShell::release_candidate_lock() {
    if (!is_candidate_lock_active()) {
        candidate_lock_owner_id_.reset();
        candidate_lock_deadline_.reset();
        audit_event("candidate-unlock", true, "already-unlocked");
        return true;
    }

    if (!candidate_lock_owner_id_.has_value() || candidate_lock_owner_id_.value() != active_owner_id_) {
        audit_event("candidate-unlock", false, "lock-owner-mismatch");
        return false;
    }

    candidate_lock_owner_id_.reset();
    candidate_lock_deadline_.reset();
    audit_event("candidate-unlock", true);
    return true;
}

void CliShell::release_candidate_lock_force() {
    candidate_lock_owner_id_.reset();
    candidate_lock_deadline_.reset();
    audit_event("candidate-force-unlock", true);
}

bool CliShell::ensure_lock_for_modify() {
    if (!is_candidate_lock_active()) {
        out_ << "Candidate lock required. Use: candidate lock <ttl-seconds>\n";
        return false;
    }

    if (!candidate_lock_owner_id_.has_value() || candidate_lock_owner_id_.value() != active_owner_id_) {
        out_ << "Candidate lock owned by "
             << (candidate_lock_owner_id_.has_value() ? candidate_lock_owner_id_.value() : std::string("unknown"))
             << ". Switch session owner or wait for TTL expiry.\n";
        return false;
    }

    return true;
}

bool CliShell::has_permission(Permission permission) const {
    const auto it = policy_table_.find(active_role_);
    if (it == policy_table_.end()) {
        return false;
    }

    const auto p_it = it->second.permissions.find(permission);
    if (p_it == it->second.permissions.end()) {
        return false;
    }

    return p_it->second;
}

bool CliShell::load_policy_table(const std::string& policy_path) {
    std::ifstream file(policy_path);
    if (!file.is_open()) {
        return false;
    }

    auto loaded = policy_table_;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }

        if (line[0] == '#') {
            continue;
        }

        std::istringstream ss(line);
        std::string kw;
        std::string role_name;
        std::string permission_name;
        std::string action;

        ss >> kw >> role_name >> permission_name >> action;
        if (kw != "role" || role_name.empty() || permission_name.empty() || action.empty()) {
            return false;
        }

        SessionRole role = SessionRole::Operator;
        Permission permission = Permission::Commit;
        bool allow = false;

        if (!try_parse_role(role_name, role) || !try_parse_permission(permission_name, permission)
            || !try_parse_allow_deny(action, allow)) {
            return false;
        }

        auto& entry = loaded[role];
        entry.permissions[permission] = allow;
    }

    policy_table_ = loaded;
    return true;
}

bool CliShell::reload_runtime_config(const std::string& path) {
    RuntimeConfig cfg {};
    std::string error;
    if (!load_runtime_config_file(path, cfg, error)) {
        audit_event("runtime-config-reload", false, error);
        return false;
    }

    const std::string cfg_dir = parent_directory(path);
    const std::string resolved_policy_path = resolve_path(cfg.rbac_policy_file, cfg_dir);
    const std::string resolved_audit_path = resolve_path(cfg.audit_log_file, cfg_dir);

    const std::string old_policy_path = policy_table_path_;
    policy_table_path_ = resolved_policy_path;
    if (!load_policy_table(policy_table_path_)) {
        policy_table_path_ = old_policy_path;
        load_policy_table(policy_table_path_);
        audit_event("runtime-config-reload", false, "policy-load-failed: " + resolved_policy_path);
        return false;
    }

    if (!node_.set_plmn(cfg.cli.mcc, cfg.cli.mnc)) {
        policy_table_path_ = old_policy_path;
        load_policy_table(policy_table_path_);
        audit_event("runtime-config-reload", false, "node-set-plmn-rejected");
        return false;
    }

    if (!node_.set_alarm_thresholds(cfg.alarm_thresholds)) {
        policy_table_path_ = old_policy_path;
        load_policy_table(policy_table_path_);
        audit_event("runtime-config-reload", false, "node-set-alarm-thresholds-rejected");
        return false;
    }

    running_cfg_ = cfg.cli;
    candidate_cfg_ = cfg.cli;
    audit_log_path_ = resolved_audit_path;
    runtime_config_path_ = path;

    audit_event("runtime-config-reload", true, path);
    return true;
}

void CliShell::init_default_policy_table() {
    policy_table_.clear();

    RolePolicy operator_policy {};
    operator_policy.permissions[Permission::CandidateLock] = true;
    operator_policy.permissions[Permission::CandidateRenew] = true;
    operator_policy.permissions[Permission::CandidateUnlock] = true;
    operator_policy.permissions[Permission::Rollback] = true;
    operator_policy.permissions[Permission::Commit] = true;
    operator_policy.permissions[Permission::CommitConfirmed] = true;
    operator_policy.permissions[Permission::ConfirmCommit] = true;
    operator_policy.permissions[Permission::ForceUnlock] = false;
    operator_policy.permissions[Permission::PolicyReload] = false;
    operator_policy.permissions[Permission::SessionRoleChange] = true;
    operator_policy.permissions[Permission::SessionOwnerChange] = true;
    operator_policy.permissions[Permission::ShowPolicy] = true;
    policy_table_[SessionRole::Operator] = operator_policy;

    RolePolicy admin_policy {};
    admin_policy.permissions[Permission::CandidateLock] = true;
    admin_policy.permissions[Permission::CandidateRenew] = true;
    admin_policy.permissions[Permission::CandidateUnlock] = true;
    admin_policy.permissions[Permission::Rollback] = true;
    admin_policy.permissions[Permission::Commit] = true;
    admin_policy.permissions[Permission::CommitConfirmed] = true;
    admin_policy.permissions[Permission::ConfirmCommit] = true;
    admin_policy.permissions[Permission::ForceUnlock] = true;
    admin_policy.permissions[Permission::PolicyReload] = true;
    admin_policy.permissions[Permission::SessionRoleChange] = true;
    admin_policy.permissions[Permission::SessionOwnerChange] = true;
    admin_policy.permissions[Permission::ShowPolicy] = true;
    policy_table_[SessionRole::Admin] = admin_policy;
}

void CliShell::audit_event(const std::string& action, bool success, const std::string& detail) {
    std::ofstream log_file(audit_log_path_, std::ios::app);
    if (!log_file.is_open()) {
        return;
    }

    log_file << now_utc()
             << " owner=" << active_owner_id_
             << " role=" << to_string(active_role_)
             << " action=" << action
             << " result=" << (success ? "success" : "deny");

    if (!detail.empty()) {
        log_file << " detail=\"" << detail << "\"";
    }

    log_file << "\n";
}

std::string CliShell::now_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc {};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_time);
#else
    gmtime_r(&now_time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

void CliShell::print_policy_table() const {
    for (const auto& [role, policy] : policy_table_) {
        for (const auto permission : all_permissions()) {
            const auto it = policy.permissions.find(permission);
            const bool allow = it != policy.permissions.end() && it->second;
            out_ << "role " << to_string(role) << ' ' << to_string(permission) << ' ' << (allow ? "allow" : "deny") << "\n";
        }
    }
}

bool CliShell::is_candidate_lock_active() const {
    if (!candidate_lock_owner_id_.has_value() || !candidate_lock_deadline_.has_value()) {
        return false;
    }

    return std::chrono::steady_clock::now() < candidate_lock_deadline_.value();
}

std::uint64_t CliShell::candidate_lock_ttl_left_seconds() const {
    if (!is_candidate_lock_active()) {
        return 0;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::seconds>(candidate_lock_deadline_.value() - now).count();
    return diff > 0 ? static_cast<std::uint64_t>(diff) : 0;
}

void CliShell::process_timers() {
    if (candidate_lock_owner_id_.has_value() && candidate_lock_deadline_.has_value()
        && std::chrono::steady_clock::now() >= candidate_lock_deadline_.value()) {
        const auto owner = candidate_lock_owner_id_.value();
        candidate_lock_owner_id_.reset();
        candidate_lock_deadline_.reset();
        reset_candidate_from_running();
        out_ << "Candidate lock expired for owner " << owner << ".\n";
    }

    if (!rollback_deadline_.has_value()) {
        return;
    }

    if (std::chrono::steady_clock::now() < rollback_deadline_.value()) {
        return;
    }

    rollback_confirmed_commit();
    out_ << "Commit-confirm timeout reached. Auto-rollback applied.\n";
}

std::string CliShell::prompt() const {
    if (mode_ == CliMode::ConfigAmfN2) {
        return "AMF(config-amf-n2)# ";
    }

    if (mode_ == CliMode::ConfigAmfSbi) {
        return "AMF(config-amf-sbi)# ";
    }

    if (mode_ == CliMode::ConfigAmf) {
        return "AMF(config-amf)# ";
    }

    if (mode_ == CliMode::Config) {
        return "AMF(config)# ";
    }

    return "AMF# ";
}

void CliShell::print_running_config() const {
    out_ << "! running-config\n";
    out_ << "amf\n";
    out_ << " plmn " << running_cfg_.mcc << ' ' << running_cfg_.mnc << "\n";
    out_ << " n2\n";
    out_ << "  local-address " << running_cfg_.n2.local_address << "\n";
    out_ << "  port " << running_cfg_.n2.port << "\n";
    out_ << " sbi\n";
    out_ << "  bind-address " << running_cfg_.sbi.bind_address << "\n";
    out_ << "  port " << running_cfg_.sbi.port << "\n";
    out_ << "  nf-instance " << running_cfg_.sbi.nf_instance << "\n";
    out_ << "!\n";
}

void CliShell::print_candidate_config() const {
    out_ << "! candidate-config\n";
    out_ << "amf\n";
    out_ << " plmn " << candidate_cfg_.mcc << ' ' << candidate_cfg_.mnc << "\n";
    out_ << " n2\n";
    out_ << "  local-address " << candidate_cfg_.n2.local_address << "\n";
    out_ << "  port " << candidate_cfg_.n2.port << "\n";
    out_ << " sbi\n";
    out_ << "  bind-address " << candidate_cfg_.sbi.bind_address << "\n";
    out_ << "  port " << candidate_cfg_.sbi.port << "\n";
    out_ << "  nf-instance " << candidate_cfg_.sbi.nf_instance << "\n";
    out_ << "!\n";
}

void CliShell::print_config_diff() const {
    if (same_config(candidate_cfg_, running_cfg_)) {
        out_ << "No pending candidate changes.\n";
        return;
    }

    if (running_cfg_.mcc != candidate_cfg_.mcc || running_cfg_.mnc != candidate_cfg_.mnc) {
        out_ << "- plmn " << running_cfg_.mcc << ' ' << running_cfg_.mnc << "\n";
        out_ << "+ plmn " << candidate_cfg_.mcc << ' ' << candidate_cfg_.mnc << "\n";
    }
    if (running_cfg_.n2.local_address != candidate_cfg_.n2.local_address) {
        out_ << "- n2 local-address " << running_cfg_.n2.local_address << "\n";
        out_ << "+ n2 local-address " << candidate_cfg_.n2.local_address << "\n";
    }
    if (running_cfg_.n2.port != candidate_cfg_.n2.port) {
        out_ << "- n2 port " << running_cfg_.n2.port << "\n";
        out_ << "+ n2 port " << candidate_cfg_.n2.port << "\n";
    }
    if (running_cfg_.sbi.bind_address != candidate_cfg_.sbi.bind_address) {
        out_ << "- sbi bind-address " << running_cfg_.sbi.bind_address << "\n";
        out_ << "+ sbi bind-address " << candidate_cfg_.sbi.bind_address << "\n";
    }
    if (running_cfg_.sbi.port != candidate_cfg_.sbi.port) {
        out_ << "- sbi port " << running_cfg_.sbi.port << "\n";
        out_ << "+ sbi port " << candidate_cfg_.sbi.port << "\n";
    }
    if (running_cfg_.sbi.nf_instance != candidate_cfg_.sbi.nf_instance) {
        out_ << "- sbi nf-instance " << running_cfg_.sbi.nf_instance << "\n";
        out_ << "+ sbi nf-instance " << candidate_cfg_.sbi.nf_instance << "\n";
    }
}

void CliShell::print_lock_status() const {
    out_ << "Active owner    : " << active_owner_id_ << "\n";
    out_ << "Active role     : " << to_string(active_role_) << "\n";
    out_ << "Candidate lock  : " << (is_candidate_lock_active() ? "LOCKED" : "UNLOCKED") << "\n";
    out_ << "Lock owner      : "
         << (candidate_lock_owner_id_.has_value() && is_candidate_lock_active() ? candidate_lock_owner_id_.value() : std::string("NONE"))
         << "\n";
    out_ << "Lock ttl-left   : " << candidate_lock_ttl_left_seconds() << "s\n";
    out_ << "Commit-confirm : " << (rollback_deadline_.has_value() ? "PENDING" : "NONE") << "\n";
}

void CliShell::print_help() const {
    out_ << "Available commands (mode-dependent):\n";
    out_ << "  [exec] show amf status|stats|interfaces [detail]|ue [imsi]\n";
    out_ << "  [exec] show session\n";
    out_ << "  [exec] show policy\n";
    out_ << "  [exec] session owner <owner-id>\n";
    out_ << "  [exec] session role operator|admin\n";
    out_ << "  [exec] policy reload\n";
    out_ << "  [exec] runtime-config reload [path]\n";
    out_ << "  [exec] show running config\n";
    out_ << "  [exec] show configuration candidate\n";
    out_ << "  [exec] show configuration diff\n";
    out_ << "  [exec] show configuration lock\n";
    out_ << "  [exec] amf start|stop|degrade|recover|tick\n";
    out_ << "  [exec] ue register <imsi> <tai>\n";
    out_ << "  [exec] ue deregister <imsi>\n";
    out_ << "  [exec] simulate n2 <imsi> <payload>\n";
    out_ << "  [exec] simulate sbi <service> <payload>\n";
    out_ << "  [exec] simulate n1 <imsi> <payload>\n";
    out_ << "  [exec] simulate n3 <imsi> <payload>\n";
    out_ << "  [exec] simulate n8 <imsi>\n";
    out_ << "  [exec] simulate n11 <imsi> <operation>\n";
    out_ << "  [exec] simulate n12 <imsi>\n";
    out_ << "  [exec] simulate n14 <imsi> <target-amf>\n";
    out_ << "  [exec] simulate n15 <imsi>\n";
    out_ << "  [exec] simulate n22 <imsi> <snssai>\n";
    out_ << "  [exec] simulate n26 <imsi> <operation>\n";
    out_ << "  [exec] clear stats\n";
    out_ << "  [exec] configure terminal | conf t\n";
    out_ << "  [config] amf\n";
    out_ << "  [config] show session\n";
    out_ << "  [config] show policy\n";
    out_ << "  [config] session owner <owner-id>\n";
    out_ << "  [config] session role operator|admin\n";
    out_ << "  [config] policy reload\n";
    out_ << "  [config] runtime-config reload [path]\n";
    out_ << "  [config] candidate lock <ttl-seconds>|renew <ttl-seconds>|unlock|force-unlock\n";
    out_ << "  [config] commit|commit confirmed <seconds>|confirm|rollback\n";
    out_ << "  [config-amf] plmn <mcc> <mnc>\n";
    out_ << "  [config-amf] mcc <value>\n";
    out_ << "  [config-amf] mnc <value>\n";
    out_ << "  [config-amf] n2|sbi\n";
    out_ << "  [config-amf] commit|commit confirmed <seconds>|confirm|rollback\n";
    out_ << "  [config-amf-n2] local-address <ipv4>\n";
    out_ << "  [config-amf-n2] port <1..65535>\n";
    out_ << "  [config-amf-sbi] bind-address <ipv4>\n";
    out_ << "  [config-amf-sbi] port <1..65535>\n";
    out_ << "  [config-amf-sbi] nf-instance <id>\n";
    out_ << "  [config] show running config|configuration candidate|configuration diff\n";
    out_ << "  [config] do <exec-command>\n";
    out_ << "  [config-amf] do <exec-command>\n";
    out_ << "  [config-amf-n2] do <exec-command>\n";
    out_ << "  [config-amf-sbi] do <exec-command>\n";
    out_ << "  [config] end|exit\n";
    out_ << "  [config-amf] end|exit\n";
    out_ << "  [config-amf-n2] end|exit\n";
    out_ << "  [config-amf-sbi] end|exit\n";
    out_ << "  [exec] exit|quit\n";
}

std::vector<std::string> CliShell::tokenize(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> tokens;
    std::string token;

    while (input >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

std::string CliShell::join_tail(const std::vector<std::string>& tokens, std::size_t start_index) {
    if (start_index >= tokens.size()) {
        return {};
    }

    std::ostringstream payload;
    for (std::size_t i = start_index; i < tokens.size(); ++i) {
        if (i > start_index) {
            payload << ' ';
        }
        payload << tokens[i];
    }

    return payload.str();
}

}  // namespace amf
