#include <iostream>
#include <algorithm>
#include <string>

#include "amf/amf.hpp"
#include "amf/modules/mobility.hpp"
#include "amf/modules/registration.hpp"
#include "amf/modules/session_management.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string& imsi, const std::string& payload) override {
        ++messages;
        last_imsi = imsi;
        last_payload = payload;
    }

    int messages {0};
    std::string last_imsi;
    std::string last_payload;
};

class TestSbi final : public amf::ISbiInterface {
public:
    void notify_service(const std::string& service_name, const std::string& payload) override {
        ++notifications;
        last_service = service_name;
        last_payload = payload;
    }

    int notifications {0};
    std::string last_service;
    std::string last_payload;
};

class TestN1 final : public amf::IN1Interface {
public:
    void send_nas_to_ue(const std::string&, const std::string&) override { ++messages; }
    int messages {0};
};

class TestN3 final : public amf::IN3Interface {
public:
    void forward_user_plane(const std::string&, const std::string&) override { ++packets; }
    int packets {0};
};

class TestN8 final : public amf::IN8Interface {
public:
    void query_subscription(const std::string&) override { ++queries; }
    int queries {0};
};

class TestN11 final : public amf::IN11Interface {
public:
    void manage_pdu_session(const std::string&, const std::string&) override { ++operations; }
    int operations {0};
};

class TestN12 final : public amf::IN12Interface {
public:
    void authenticate_ue(const std::string&) override { ++requests; }
    int requests {0};
};

class TestN14 final : public amf::IN14Interface {
public:
    void transfer_amf_context(const std::string&, const std::string&) override { ++transfers; }
    int transfers {0};
};

class TestN15 final : public amf::IN15Interface {
public:
    void query_policy(const std::string&) override { ++queries; }
    int queries {0};
};

class TestN22 final : public amf::IN22Interface {
public:
    void select_network_slice(const std::string&, const std::string&) override { ++selections; }
    int selections {0};
};

class TestN26 final : public amf::IN26Interface {
public:
    void interwork_with_mme(const std::string&, const std::string&) override { ++operations; }
    int operations {0};
};

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool test_mobility_module() {
    amf::MobilityModule mobility;

    bool ok = true;
    ok &= check(mobility.state() == amf::AmfState::Idle, "Mobility initial state should be IDLE");
    ok &= check(mobility.start(), "Mobility start should succeed from IDLE");
    ok &= check(mobility.state() == amf::AmfState::Running, "Mobility state should be RUNNING");
    ok &= check(!mobility.start(), "Mobility second start should be rejected");
    ok &= check(mobility.set_degraded(), "Mobility degrade should succeed from RUNNING");
    ok &= check(mobility.recover(), "Mobility recover should succeed from DEGRADED");
    ok &= check(mobility.stop(), "Mobility stop should succeed from RUNNING");
    ok &= check(!mobility.stop(), "Mobility second stop should be rejected");
    return ok;
}

bool test_registration_module() {
    amf::RegistrationModule registration;

    bool ok = true;
    ok &= check(registration.register_ue("250030000000111", "250-03"), "Register UE should succeed");
    ok &= check(registration.active_ue_count() == 1, "Active UE count should be 1");

    const auto ue = registration.find_ue("250030000000111");
    ok &= check(ue.has_value(), "UE should be found after registration");
    ok &= check(ue->tai == "250-03", "UE TAI should match");

    ok &= check(registration.deregister_ue("250030000000111"), "Deregister should succeed");
    ok &= check(registration.active_ue_count() == 0, "Active UE count should be 0 after deregister");
    ok &= check(!registration.deregister_ue("999"), "Deregister unknown UE should fail");
    return ok;
}

bool test_session_management_module() {
    TestN2 n2;
    TestSbi sbi;
    amf::SessionManagementModule sessions(n2, sbi);

    bool ok = true;
    ok &= check(sessions.set_plmn("250", "03"), "Valid PLMN should be accepted");
    ok &= check(!sessions.set_plmn("25", "03"), "Invalid MCC should be rejected");
    ok &= check(!sessions.set_plmn("250", "3"), "Invalid MNC should be rejected");

    ok &= check(!sessions.send_n2_nas(false, true, "250030000000111", "rrc"), "N2 send should fail when not operational");
    ok &= check(!sessions.send_n2_nas(true, false, "250030000000111", "rrc"), "N2 send should fail for unknown UE");
    ok &= check(sessions.send_n2_nas(true, true, "250030000000111", "rrc"), "N2 send should pass with valid preconditions");
    ok &= check(n2.messages == 1, "N2 message counter should be incremented");

    ok &= check(sessions.notify_sbi(true, "namf-comm", "event"), "SBI notify should succeed when operational");
    ok &= check(!sessions.notify_sbi(false, "namf-comm", "event"), "SBI notify should fail when not operational");
    ok &= check(sbi.notifications == 1, "SBI notification counter should be incremented once");
    return ok;
}

bool test_amf_node_integration() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;
    TestN3 n3;
    TestN8 n8;
    TestN11 n11;
    TestN12 n12;
    TestN14 n14;
    TestN15 n15;
    TestN22 n22;
    TestN26 n26;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    peers.n3 = &n3;
    peers.n8 = &n8;
    peers.n11 = &n11;
    peers.n12 = &n12;
    peers.n14 = &n14;
    peers.n15 = &n15;
    peers.n22 = &n22;
    peers.n26 = &n26;

    amf::AmfNode node(n2, sbi, peers);

    bool ok = true;
    ok &= check(node.start(), "AMF start should succeed");
    ok &= check(node.register_ue("250030000000111", "250-03"), "AMF register UE should succeed");
    ok &= check(node.send_n2_nas("250030000000111", "registration-request"), "AMF N2 send should succeed for registered UE");
    ok &= check(node.send_n1_nas("250030000000111", "security-mode-command"), "AMF N1 send should succeed");
    ok &= check(node.forward_n3_user_plane("250030000000111", "ul-pdu"), "AMF N3 forwarding should succeed");
    ok &= check(node.query_n8_subscription("250030000000111"), "AMF N8 query should succeed");
    ok &= check(node.manage_n11_pdu_session("250030000000111", "create"), "AMF N11 operation should succeed");
    ok &= check(node.authenticate_n12("250030000000111"), "AMF N12 auth should succeed");
    ok &= check(node.transfer_n14_context("250030000000111", "amf-b"), "AMF N14 transfer should succeed");
    ok &= check(node.query_n15_policy("250030000000111"), "AMF N15 policy query should succeed");
    ok &= check(node.select_n22_slice("250030000000111", "1-010203"), "AMF N22 slice selection should succeed");
    ok &= check(node.interwork_n26("250030000000111", "handover"), "AMF N26 interworking should succeed");

    const auto st = node.status();
    ok &= check(st.state == amf::AmfState::Running, "AMF state should be RUNNING after start");
    ok &= check(st.active_ue == 1, "AMF active UE should be 1");
    ok &= check(st.stats.n2_signaling_messages == 1, "AMF N2 stats should be 1");
    ok &= check(st.stats.ue_registrations == 1, "AMF registration stats should be 1");
    ok &= check(st.stats.n1_messages == 1, "AMF N1 stats should be 1");
    ok &= check(st.stats.n3_user_plane_packets == 1, "AMF N3 stats should be 1");
    ok &= check(st.stats.n8_subscription_queries == 1, "AMF N8 stats should be 1");
    ok &= check(st.stats.n11_pdu_session_ops == 1, "AMF N11 stats should be 1");
    ok &= check(st.stats.n12_auth_requests == 1, "AMF N12 stats should be 1");
    ok &= check(st.stats.n14_context_transfers == 1, "AMF N14 stats should be 1");
    ok &= check(st.stats.n15_policy_queries == 1, "AMF N15 stats should be 1");
    ok &= check(st.stats.n22_slice_selections == 1, "AMF N22 stats should be 1");
    ok &= check(st.stats.n26_interworking_ops == 1, "AMF N26 stats should be 1");
    ok &= check(n2.messages == 1, "N2 adapter should receive one message");
    ok &= check(n1.messages == 1, "N1 adapter should receive one message");
    ok &= check(n3.packets == 1, "N3 adapter should receive one packet");
    ok &= check(n8.queries == 1, "N8 adapter should receive one query");
    ok &= check(n11.operations == 1, "N11 adapter should receive one operation");
    ok &= check(n12.requests == 1, "N12 adapter should receive one request");
    ok &= check(n14.transfers == 1, "N14 adapter should receive one transfer");
    ok &= check(n15.queries == 1, "N15 adapter should receive one query");
    ok &= check(n22.selections == 1, "N22 adapter should receive one selection");
    ok &= check(n26.operations == 1, "N26 adapter should receive one operation");

    const auto interfaces = node.list_interfaces();
    const auto find_interface = [&](const std::string& name) {
        return std::find_if(
            interfaces.begin(), interfaces.end(),
            [&](const amf::InterfaceInfo& iface) { return iface.name == name; });
    };

    const auto n2_it = find_interface("N2");
    const auto n14_it = find_interface("N14");
    const auto sbi_it = find_interface("SBI");
    ok &= check(n2_it != interfaces.end() && n2_it->configured, "N2 interface should be configured");
    ok &= check(n14_it != interfaces.end() && n14_it->configured, "N14 interface should be configured when adapter is present");
    ok &= check(sbi_it != interfaces.end() && sbi_it->configured, "SBI interface should be configured");

    const auto diagnostics = node.list_interface_diagnostics();
    const auto find_diag = [&](const std::string& name) {
        return std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [&](const amf::InterfaceDiagnostics& diag) { return diag.name == name; });
    };

    const auto n2_diag = find_diag("N2");
    const auto n3_diag = find_diag("N3");
    ok &= check(n2_diag != diagnostics.end(), "N2 diagnostics entry should be present");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->available, "N2 diagnostics should report availability while AMF is running");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->success_count == 1, "N2 diagnostics should report one successful operation");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->error_count == 0, "N2 diagnostics should not report errors on successful flow");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->status_reason == "ok", "N2 diagnostics should report ok reason after successful operation");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->alarm_level == "none", "N2 diagnostics should not raise alarm on healthy flow");
    ok &= check(n2_diag != diagnostics.end() && n2_diag->last_activity_utc != "never", "N2 diagnostics should report last activity");
    ok &= check(n3_diag != diagnostics.end() && n3_diag->available, "N3 diagnostics should report availability while AMF is running");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_mobility_module();
    ok &= test_registration_module();
    ok &= test_session_management_module();
    ok &= test_amf_node_integration();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] All AMF tests passed.\n";
    return 0;
}
