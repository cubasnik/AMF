#include <iostream>
#include <string>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestN12 final : public amf::IN12Interface {
public:
    void authenticate_ue(const std::string& imsi, const std::string& request) override {
        ++requests;
        last_imsi = imsi;
        last_request = request;
    }

    int requests {0};
    std::string last_imsi;
    std::string last_request;
};

class TestN15 final : public amf::IN15Interface {
public:
    void query_policy(const std::string& imsi, const std::string& request) override {
        ++queries;
        last_imsi = imsi;
        last_request = request;
    }

    int queries {0};
    std::string last_imsi;
    std::string last_request;
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

std::string find_field_value(const std::string& text, const std::string& key) {
    const std::size_t pos = text.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const std::size_t value_pos = pos + key.size();
    const std::size_t line_end = text.find('\n', value_pos);
    return text.substr(value_pos, line_end == std::string::npos ? std::string::npos : line_end - value_pos);
}

amf::AmfNode make_node(TestN2& n2, TestSbi& sbi, TestN12& n12, TestN15& n15) {
    amf::AmfPeerInterfaces peers {};
    peers.n12 = &n12;
    peers.n15 = &n15;
    return amf::AmfNode(n2, sbi, peers);
}

bool test_n12_auth_request_and_response() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.authenticate_n12(imsi), "Legacy auth-request should be accepted");
    ok &= check(contains(n12.last_request, "N12SBI/1\n"), "N12 should emit structured payload");
    ok &= check(contains(n12.last_request, "procedure=AuthRequest"), "N12 should encode AuthRequest");

    const auto res_pos = n12.last_request.find("ie.rand=");
    ok &= check(res_pos != std::string::npos, "AuthRequest should include RAND");

    ok &= check(node.authenticate_n12(imsi, "auth-response"), "Legacy auth-response should be accepted after challenge");
    ok &= check(contains(n12.last_request, "procedure=AuthResponse"), "N12 should encode AuthResponse");
    ok &= check(contains(n12.last_request, "ie.result=authenticated"), "AuthResponse should confirm authenticated result");

    return ok;
}

bool test_n12_missing_and_failed_auth_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthResponse|res-star=ABCDEF01"), "AuthResponse without context should be rejected");
    ok &= check(contains(n12.last_request, "ie.cause=no-auth-context"), "No auth context should emit no-auth-context");

    ok &= check(node.authenticate_n12(imsi), "AuthRequest should be accepted");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthResponse|res-star=BAD00000"), "Wrong RES* should be rejected");
    ok &= check(contains(n12.last_request, "ie.cause=auth-failed"), "Wrong RES* should emit auth-failed");

    return ok;
}

bool test_n12_pending_auth_rejected_but_context_preserved() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.authenticate_n12(imsi), "Initial auth-request should be accepted");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthRequest|serving-network-name=5G:mnc03.mcc250.3gppnetwork.org|auth-method=eap"), "Overlapping auth-request should be rejected while challenge is pending");
    ok &= check(contains(n12.last_request, "ie.cause=auth-already-pending"), "Pending challenge should emit auth-already-pending");
    ok &= check(node.authenticate_n12(imsi, "auth-response"), "Correct auth-response should still succeed after rejected duplicate request");
    ok &= check(contains(n12.last_request, "ie.result=authenticated"), "Preserved auth context should allow successful auth-response");

    return ok;
}

bool test_n12_missing_mandatory_ie_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.authenticate_n12(imsi), "Initial auth-request should be accepted");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthResponse"), "Structured AuthResponse without res-star should be rejected");
    ok &= check(contains(n12.last_request, "ie.cause=missing-mandatory-ie"), "Missing res-star should emit missing-mandatory-ie");

    return ok;
}

bool test_n12_unsupported_auth_method_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthRequest|serving-network-name=5G:mnc03.mcc250.3gppnetwork.org|auth-method=eap"),
        "Unsupported auth method should be rejected");
    ok &= check(contains(n12.last_request, "ie.cause=unsupported-auth-method"), "Unsupported auth method should emit unsupported-auth-method");

    return ok;
}

bool test_n12_unsupported_procedure_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000012006";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.authenticate_n12(imsi, "N12SBI|procedure=AuthCancel|res-star=ABCD1234"), "Unsupported N12 procedure should be rejected");
    ok &= check(contains(n12.last_request, "ie.cause=unsupported-procedure"), "Unsupported N12 procedure should emit unsupported-procedure");

    return ok;
}

bool test_n15_policy_query_and_update() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.query_n15_policy(imsi), "Legacy get-am-policy should be accepted");
    ok &= check(contains(n15.last_request, "N15SBI/1\n"), "N15 should emit structured payload");
    ok &= check(contains(n15.last_request, "procedure=GetAmPolicy"), "N15 should encode GetAmPolicy");

    std::string assoc_id;
    const std::string key = "ie.association-id=";
    const std::size_t assoc_pos = n15.last_request.find(key);
    ok &= check(assoc_pos != std::string::npos, "Policy query should include association id");
    if (assoc_pos != std::string::npos) {
        const std::size_t value_pos = assoc_pos + key.size();
        const std::size_t line_end = n15.last_request.find('\n', value_pos);
        assoc_id = n15.last_request.substr(value_pos, line_end == std::string::npos ? std::string::npos : line_end - value_pos);
    }

    ok &= check(node.query_n15_policy(imsi, "N15SBI|procedure=UpdatePolicyAssociation|association-id=" + assoc_id + "|policy-rule=allow-video"), "Policy association update should be accepted");
    ok &= check(contains(n15.last_request, "procedure=UpdatePolicyAssociation"), "N15 should encode UpdatePolicyAssociation");
    ok &= check(contains(n15.last_request, "ie.policy-rule=allow-video"), "N15 update should include policy rule");

    return ok;
}

bool test_n15_no_context_and_assoc_mismatch_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.query_n15_policy(imsi, "update-policy-association"), "Update without context should be rejected");
    ok &= check(contains(n15.last_request, "ie.cause=no-policy-context"), "No policy context should emit no-policy-context");

    ok &= check(node.query_n15_policy(imsi), "Get policy should be accepted");
    ok &= check(!node.query_n15_policy(imsi, "N15SBI|procedure=UpdatePolicyAssociation|association-id=pcf-bad|policy-rule=deny-all"), "Association mismatch should be rejected");
    ok &= check(contains(n15.last_request, "ie.cause=association-id-mismatch"), "Association mismatch should emit association-id-mismatch");

    return ok;
}

bool test_n15_schema_rejects_but_context_preserved() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.query_n15_policy(imsi), "Initial policy query should succeed");

    const std::string assoc_id = find_field_value(n15.last_request, "ie.association-id=");
    ok &= check(!assoc_id.empty(), "Initial policy query should expose association id");
    ok &= check(!node.query_n15_policy(imsi, "N15SBI|procedure=UpdatePolicyAssociation|association-id=" + assoc_id + "|policy-type=sm-policy|policy-rule=deny-video"), "Policy-type mismatch should be rejected");
    ok &= check(contains(n15.last_request, "ie.cause=policy-type-mismatch"), "Mismatch should emit policy-type-mismatch");
    ok &= check(node.query_n15_policy(imsi, "N15SBI|procedure=UpdatePolicyAssociation|association-id=" + assoc_id + "|policy-rule=allow-video|snssai=1-010203"), "Valid update should still succeed after rejected mutation");
    ok &= check(contains(n15.last_request, "ie.policy-rule=allow-video"), "Valid update should reach adapter after rejected mutation");

    return ok;
}

bool test_n15_missing_mandatory_ie_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.query_n15_policy(imsi, "N15SBI|procedure=GetSmPolicy|snssai=1-010203"),
        "Structured GetSmPolicy without policy-type should be rejected");
    ok &= check(contains(n15.last_request, "procedure=ErrorIndication"), "Missing IE should emit ErrorIndication");
    ok &= check(contains(n15.last_request, "ie.cause=missing-mandatory-ie"), "Missing IE should emit missing-mandatory-ie");

    return ok;
}

bool test_n15_invalid_snssai_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.query_n15_policy(imsi, "N15SBI|procedure=GetAmPolicy|policy-type=am-policy|snssai=bad-snssai"),
        "Invalid SNSSAI should be rejected");
    ok &= check(contains(n15.last_request, "ie.cause=invalid-snssai"), "Invalid SNSSAI should emit invalid-snssai");

    return ok;
}

bool test_n15_unsupported_procedure_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN12 n12;
    TestN15 n15;
    auto node = make_node(n2, sbi, n12, n15);

    const std::string imsi = "250030000015006";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.query_n15_policy(imsi, "N15SBI|procedure=DeletePolicyAssociation|association-id=pcf-1"),
        "Unsupported N15 procedure should be rejected");
    ok &= check(contains(n15.last_request, "ie.cause=unsupported-procedure"), "Unsupported N15 procedure should emit unsupported-procedure");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n12_auth_request_and_response();
    ok &= test_n12_missing_and_failed_auth_rejected();
    ok &= test_n12_pending_auth_rejected_but_context_preserved();
    ok &= test_n12_missing_mandatory_ie_rejected();
    ok &= test_n12_unsupported_auth_method_rejected();
    ok &= test_n12_unsupported_procedure_rejected();
    ok &= test_n15_policy_query_and_update();
    ok &= test_n15_no_context_and_assoc_mismatch_rejected();
    ok &= test_n15_schema_rejects_but_context_preserved();
    ok &= test_n15_missing_mandatory_ie_rejected();
    ok &= test_n15_invalid_snssai_rejected();
    ok &= test_n15_unsupported_procedure_rejected();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N12/N15 interop/negative tests passed.\n";
    return 0;
}
