#include <iostream>
#include <string>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestN26 final : public amf::IN26Interface {
public:
    void interwork_with_mme(const std::string& imsi, const std::string& operation) override {
        ++messages;
        last_imsi = imsi;
        last_payload = operation;
    }

    int messages {0};
    std::string last_imsi;
    std::string last_payload;
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

amf::AmfNode make_node(TestN2& n2, TestSbi& sbi, TestN26& n26) {
    amf::AmfPeerInterfaces peers {};
    peers.n26 = &n26;
    return amf::AmfNode(n2, sbi, peers);
}

bool test_n26_handover_and_context_transfer() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.interwork_n26(imsi, "handover"), "Legacy handover should be accepted");
    ok &= check(contains(n26.last_payload, "GTPV2C/1\n"), "N26 should emit structured GTPV2C payload");
    ok &= check(contains(n26.last_payload, "procedure=HandoverRequest"), "Handover should map to HandoverRequest");
    ok &= check(contains(n26.last_payload, "header.version=2"), "N26 payload should include version header");

    ok &= check(node.interwork_n26(imsi, "GTPV2C|procedure=ContextTransfer|target-mme=mme-b"), "ContextTransfer should be accepted after handover");
    ok &= check(contains(n26.last_payload, "procedure=ContextTransfer"), "ContextTransfer procedure should be encoded");
    ok &= check(contains(n26.last_payload, "ie.target-mme=mme-b"), "ContextTransfer should include target-mme IE");

    return ok;
}

bool test_n26_isr_activate_deactivate_flow() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.interwork_n26(imsi, "handover"), "Handover should be accepted");
    ok &= check(node.interwork_n26(imsi, "context-transfer"), "Context transfer alias should be accepted");

    ok &= check(node.interwork_n26(imsi, "isr-activate"), "ISR activation should be accepted");
    ok &= check(contains(n26.last_payload, "procedure=IsrActivate"), "ISR activate should be encoded");

    ok &= check(node.interwork_n26(imsi, "isr-deactivate"), "ISR deactivation should be accepted");
    ok &= check(contains(n26.last_payload, "procedure=IsrDeactivate"), "ISR deactivate should be encoded");

    ok &= check(node.interwork_n26(imsi, "release"), "Release should be accepted");
    ok &= check(contains(n26.last_payload, "procedure=ReleaseContext"), "Release should map to ReleaseContext");

    return ok;
}

bool test_n26_missing_mandatory_ie_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.interwork_n26(imsi, "handover"), "Handover should be accepted");

    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=ContextTransfer"), "Missing target-mme should be rejected");
    ok &= check(contains(n26.last_payload, "procedure=ErrorIndication"), "Rejected N26 message should emit ErrorIndication");
    ok &= check(contains(n26.last_payload, "ie.cause=missing-mandatory-ie"), "ErrorIndication should include missing-mandatory-ie cause");

    return ok;
}

bool test_n26_duplicate_handover_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.interwork_n26(imsi, "handover"), "First handover should be accepted");

    ok &= check(!node.interwork_n26(imsi, "handover"), "Duplicate handover should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=duplicate-handover-request"), "Duplicate handover should emit specific cause");

    return ok;
}

bool test_n26_no_context_release_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(!node.interwork_n26(imsi, "release"), "Release without context should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=no-context"), "Release without context should emit no-context cause");

    return ok;
}

bool test_n26_handover_strict_ie_validation_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026006";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=HandoverRequest|mme-teid=1001|tai=250-03"),
        "Structured handover without enb-teid should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=missing-mandatory-ie"), "Missing handover IE should emit missing-mandatory-ie");

    return ok;
}

bool test_n26_invalid_teid_and_unsupported_procedure_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026007";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=HandoverRequest|mme-teid=0|enb-teid=2001|tai=250-03"),
        "Invalid MME TEID should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=invalid-mme-teid"), "Invalid MME TEID should emit invalid-mme-teid");
    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=HandoverRequest|mme-teid=1001|enb-teid=0|tai=250-03"),
        "Invalid ENB TEID should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=invalid-enb-teid"), "Invalid ENB TEID should emit invalid-enb-teid");
    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=IsrStatus|mme-teid=1001"),
        "Unsupported N26 procedure should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=unsupported-procedure"), "Unsupported N26 procedure should emit unsupported-procedure");

    return ok;
}

bool test_n26_context_and_isr_state_errors_rejected() {
    TestN2 n2;
    TestN26 n26;
    TestSbi sbi;
    auto node = make_node(n2, sbi, n26);

    const std::string imsi = "250030000026008";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.interwork_n26(imsi, "GTPV2C|procedure=ContextTransfer|target-mme=mme-b"),
        "Context transfer without handover context should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=no-handover-context"), "Missing handover context should emit no-handover-context");
    ok &= check(!node.interwork_n26(imsi, "isr-activate"), "ISR activate without context transfer should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=no-context-transfer"), "ISR activate without context transfer should emit no-context-transfer");

    ok &= check(node.interwork_n26(imsi, "handover"), "Handover should be accepted");
    ok &= check(node.interwork_n26(imsi, "context-transfer"), "Context transfer should be accepted after handover");
    ok &= check(!node.interwork_n26(imsi, "isr-deactivate"), "ISR deactivate without active ISR should be rejected");
    ok &= check(contains(n26.last_payload, "ie.cause=isr-not-active"), "ISR deactivate without active ISR should emit isr-not-active");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n26_handover_and_context_transfer();
    ok &= test_n26_isr_activate_deactivate_flow();
    ok &= test_n26_missing_mandatory_ie_rejected();
    ok &= test_n26_duplicate_handover_rejected();
    ok &= test_n26_no_context_release_rejected();
    ok &= test_n26_handover_strict_ie_validation_rejected();
    ok &= test_n26_invalid_teid_and_unsupported_procedure_rejected();
    ok &= test_n26_context_and_isr_state_errors_rejected();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N26 interop/negative tests passed.\n";
    return 0;
}
