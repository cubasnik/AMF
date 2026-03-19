#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "amf/amf.hpp"
#include "amf/cli.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestSbi final : public amf::ISbiInterface {
public:
    bool notify_service(const std::string&, const std::string&) override { return true; }
};

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string run_script(const std::string& script) {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    std::istringstream input(script);
    std::ostringstream output;
    amf::CliShell cli(node, input, output);
    cli.run();

    return output.str();
}

bool test_lock_owner_enforcement() {
    const std::string output = run_script(
        "configure terminal\n"
        "session owner opA\n"
        "candidate lock 30\n"
        "amf\n"
        "mnc 04\n"
        "exit\n"
        "session owner opB\n"
        "amf\n"
        "mnc 05\n"
        "end\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "Candidate MNC updated."), "Owner opA should be able to modify candidate");
    ok &= check(contains(output, "Candidate lock owned by opA"), "Owner opB should be blocked by lock ownership");
    return ok;
}

bool test_rbac_force_unlock_denied_for_operator() {
    const std::string output = run_script(
        "configure terminal\n"
        "session role operator\n"
        "candidate force-unlock\n"
        "end\n"
        "quit\n");

    return check(
        contains(output, "Candidate force-unlock rejected by policy for role operator."),
        "Operator force-unlock should be rejected by RBAC policy");
}

bool test_lock_ttl_reporting_and_lock_required() {
    const std::string output = run_script(
        "configure terminal\n"
        "session owner opA\n"
        "candidate lock 5\n"
        "show configuration lock\n"
        "candidate unlock\n"
        "amf\n"
        "mnc 07\n"
        "end\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "Candidate lock  : LOCKED"), "Lock status should report LOCKED while active");
    ok &= check(contains(output, "Lock ttl-left   : "), "Lock status should include TTL left field");
    ok &= check(contains(output, "Candidate lock required. Use: candidate lock <ttl-seconds>"), "Modify should require lock after unlock");
    return ok;
}

bool test_runtime_config_reload_with_path() {
    {
        std::ofstream policy("cli-reload-rbac.conf");
        policy << "role operator candidate_lock allow\n";
        policy << "role operator candidate_renew allow\n";
        policy << "role operator candidate_unlock allow\n";
        policy << "role operator rollback allow\n";
        policy << "role operator commit allow\n";
        policy << "role operator commit_confirmed allow\n";
        policy << "role operator confirm_commit allow\n";
        policy << "role operator force_unlock deny\n";
        policy << "role operator policy_reload deny\n";
        policy << "role operator session_role_change allow\n";
        policy << "role operator session_owner_change allow\n";
        policy << "role operator show_policy allow\n";
        policy << "role admin candidate_lock allow\n";
        policy << "role admin candidate_renew allow\n";
        policy << "role admin candidate_unlock allow\n";
        policy << "role admin rollback allow\n";
        policy << "role admin commit allow\n";
        policy << "role admin commit_confirmed allow\n";
        policy << "role admin confirm_commit allow\n";
        policy << "role admin force_unlock allow\n";
        policy << "role admin policy_reload allow\n";
        policy << "role admin session_role_change allow\n";
        policy << "role admin session_owner_change allow\n";
        policy << "role admin show_policy allow\n";
    }

    {
        std::ofstream out("cli-reload.json");
        out << "{\n";
        out << "  \"mcc\": \"250\",\n";
        out << "  \"mnc\": \"04\",\n";
        out << "  \"log_file\": \"cli-reload.log\",\n";
        out << "  \"audit_log_file\": \"cli-reload-audit.log\",\n";
        out << "  \"rbac_policy_file\": \"cli-reload-rbac.conf\",\n";
        out << "  \"n2\": {\n";
        out << "    \"local_address\": \"127.0.0.1\",\n";
        out << "    \"port\": 38412\n";
        out << "  },\n";
        out << "  \"sbi\": {\n";
        out << "    \"bind_address\": \"127.0.0.1\",\n";
        out << "    \"port\": 7777,\n";
        out << "    \"nf_instance\": \"amf-reload\"\n";
        out << "  },\n";
        out << "  \"alarm_thresholds\": {\n";
        out << "    \"warning_error_rate_percent\": 10.0,\n";
        out << "    \"critical_error_rate_percent\": 50.0,\n";
        out << "    \"critical_error_count\": 3,\n";
        out << "    \"admin_down_warning\": false\n";
        out << "  }\n";
        out << "}\n";
    }

    const std::string output = run_script(
        "session role admin\n"
        "runtime_config reload cli-reload.json\n"
        "show amf status\n"
        "show amf interfaces detail\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "Runtime config reloaded."), "Runtime config reload should succeed");
    ok &= check(contains(output, "PLMN           : 250-04"), "Reloaded PLMN should be visible in status");
    ok &= check(contains(output, "N2 [control-plane] avail=DOWN configured=yes success=0 errors=0 error-rate=0.0% reason=admin-down alarm=none"),
        "Reloaded alarm thresholds should set admin-down alarm to none when configured");
    return ok;
}

bool test_show_amf_interfaces_inventory() {
    const std::string output = run_script(
        "show amf interfaces\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "AMF interface inventory:"), "Interface inventory header should be printed");
    ok &= check(contains(output, "N2 [control-plane] : configured"), "N2 should be shown as configured");
    ok &= check(contains(output, "N1 [control-plane] : detached"), "N1 should be shown as detached when no peer is wired");
    ok &= check(contains(output, "SBI [control-plane] : configured"), "SBI should be shown as configured");
    return ok;
}

bool test_show_amf_interfaces_detail_diagnostics() {
    const std::string output = run_script(
        "show amf interfaces detail\n"
        "amf start\n"
        "simulate n2 250030000000111 fail-case\n"
        "show amf interfaces detail\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "AMF interface diagnostics:"), "Detailed diagnostics header should be printed");
    ok &= check(contains(output, "N2 [control-plane] avail=DOWN configured=yes success=0 errors=0 error-rate=0.0% reason=admin-down alarm=warning"),
        "Configured interfaces should show admin-down before AMF start");
    ok &= check(contains(output, "N2 [control-plane] avail=UP"), "N2 should be UP when AMF is running");
    ok &= check(contains(output, "N2 [control-plane] avail=UP configured=yes success=0 errors=1 error-rate=100.0% reason=service-reject alarm=critical"),
        "N2 should expose service-reject reason with 100% error rate after rejected send");
    ok &= check(contains(output, "last-activity="), "Detailed diagnostics should include last activity");
    ok &= check(contains(output, "N1 [control-plane] avail=DOWN configured=no success=0 errors=0 error-rate=0.0% reason=detached alarm=none"),
        "Detached interfaces should be reported with detached reason and zero counters");
    return ok;
}

bool test_show_amf_interfaces_errors_last() {
    const std::string output = run_script(
        "amf start\n"
        "simulate n2 250030000000111 fail-service\n"
        "amf stop\n"
        "simulate n2 250030000000111 fail-down\n"
        "show amf interfaces errors last 5 reason admin-down\n"
        "show amf interfaces errors last 5 iface N2 reason service-reject\n"
        "show amf interfaces errors last 1\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "AMF interface errors (last 1):"), "Filtered output should support reason filter");
    ok &= check(contains(output, "iface=N2 reason=admin-down"), "Reason filter should return admin-down entries");
    ok &= check(contains(output, "iface=N2 reason=service-reject"), "Combined iface+reason filter should return service-reject entries");
    ok &= check(contains(output, "AMF interface errors (last 1):"), "Interface error history header should be shown");
    ok &= check(contains(output, "T") && contains(output, "Z"), "Error history should include UTC timestamp");
    return ok;
}

bool test_show_amf_telemetry() {
    const std::string output = run_script(
        "amf start\n"
        "simulate n2 250030000000111 fail-case\n"
        "show amf telemetry 60\n"
        "quit\n");

    bool ok = true;
    ok &= check(contains(output, "AMF transport telemetry (window=60s):"), "Telemetry header should be printed");
    ok &= check(contains(output, "N2 [control-plane]"), "Telemetry should include N2 row");
    ok &= check(contains(output, "success-rate="), "Telemetry should include success-rate field");
    ok &= check(contains(output, "p50=") && contains(output, "p95="), "Telemetry should include p50/p95 fields");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_lock_owner_enforcement();
    ok &= test_rbac_force_unlock_denied_for_operator();
    ok &= test_lock_ttl_reporting_and_lock_required();
    ok &= test_runtime_config_reload_with_path();
    ok &= test_show_amf_interfaces_inventory();
    ok &= test_show_amf_interfaces_detail_diagnostics();
    ok &= test_show_amf_interfaces_errors_last();
    ok &= test_show_amf_telemetry();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] All CLI scenario tests passed.\n";
    return 0;
}
