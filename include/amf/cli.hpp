#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>

#include "amf/interfaces.hpp"

namespace amf {

enum class CliMode {
    Exec,
    Config,
    ConfigAmf,
    ConfigAmfN2,
    ConfigAmfSbi,
};

enum class SessionRole {
    Operator,
    Admin,
};

enum class Permission {
    CandidateLock,
    CandidateRenew,
    CandidateUnlock,
    Rollback,
    Commit,
    CommitConfirmed,
    ConfirmCommit,
    ForceUnlock,
    PolicyReload,
    SessionRoleChange,
    SessionOwnerChange,
    ShowPolicy,
};

struct RolePolicy {
    std::map<Permission, bool> permissions;
};

struct N2Config {
    std::string local_address {"127.0.0.1"};
    std::uint16_t port {38412};
};

struct SbiConfig {
    std::string bind_address {"127.0.0.1"};
    std::uint16_t port {7777};
    std::string nf_instance {"amf-01"};
};

struct AmfCliConfig {
    std::string mcc {"250"};
    std::string mnc {"03"};
    N2Config n2 {};
    SbiConfig sbi {};
};

class CliShell {
public:
    CliShell(
        IAmfNode& node,
        std::istream& in,
        std::ostream& out,
        std::optional<AmfCliConfig> initial_cfg = std::nullopt,
        std::string policy_path = "rbac-policy.conf",
        std::string audit_log_path = "amf-audit.log",
        std::string runtime_config_path = {});

    void run();

private:
    bool execute_line(const std::string& line);
    bool execute_exec_tokens(const std::vector<std::string>& tokens);
    bool execute_config_tokens(const std::vector<std::string>& tokens);
    bool execute_config_amf_tokens(const std::vector<std::string>& tokens);
    bool execute_config_amf_n2_tokens(const std::vector<std::string>& tokens);
    bool execute_config_amf_sbi_tokens(const std::vector<std::string>& tokens);

    bool commit_candidate();
    bool commit_candidate_confirmed(std::uint64_t seconds);
    bool confirm_commit();
    bool rollback_candidate_to_running();
    void rollback_confirmed_commit();
    void reset_candidate_from_running();
    bool acquire_candidate_lock(std::uint64_t ttl_seconds);
    bool renew_candidate_lock(std::uint64_t ttl_seconds);
    bool release_candidate_lock();
    void release_candidate_lock_force();
    bool ensure_lock_for_modify();
    bool has_permission(Permission permission) const;
    bool load_policy_table(const std::string& policy_path);
    bool reload_runtime_config(const std::string& path);
    void init_default_policy_table();
    void audit_event(const std::string& action, bool success, const std::string& detail = {});
    static std::string now_utc();
    void print_policy_table() const;
    bool is_candidate_lock_active() const;
    std::uint64_t candidate_lock_ttl_left_seconds() const;
    void process_timers();

    std::string prompt() const;
    void print_running_config() const;
    void print_candidate_config() const;
    void print_config_diff() const;
    void print_lock_status() const;
    void print_help() const;

    static std::vector<std::string> tokenize(const std::string& line);
    static std::string join_tail(const std::vector<std::string>& tokens, std::size_t start_index);

    IAmfNode& node_;
    std::istream& in_;
    std::ostream& out_;
    CliMode mode_ {CliMode::Exec};

    AmfCliConfig running_cfg_ {};
    AmfCliConfig candidate_cfg_ {};
    std::string active_owner_id_ {"operator-local"};
    SessionRole active_role_ {SessionRole::Operator};
    std::map<SessionRole, RolePolicy> policy_table_ {};
    std::string policy_table_path_ {"rbac-policy.conf"};
    std::string audit_log_path_ {"amf-audit.log"};
    std::string runtime_config_path_ {};
    std::optional<std::string> candidate_lock_owner_id_;
    std::optional<std::chrono::steady_clock::time_point> candidate_lock_deadline_;

    std::optional<AmfCliConfig> rollback_snapshot_;
    std::optional<std::chrono::steady_clock::time_point> rollback_deadline_;

    bool running_ {true};
};

}  // namespace amf
