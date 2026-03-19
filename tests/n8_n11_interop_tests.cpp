#include <iostream>
#include <string>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestN8 final : public amf::IN8Interface {
public:
    void query_subscription(const std::string& imsi, const std::string& request) override {
        ++queries;
        last_imsi = imsi;
        last_request = request;
    }

    int queries {0};
    std::string last_imsi;
    std::string last_request;
};

class TestN11 final : public amf::IN11Interface {
public:
    void manage_pdu_session(const std::string& imsi, const std::string& operation) override {
        ++operations;
        last_imsi = imsi;
        last_operation = operation;
    }

    int operations {0};
    std::string last_imsi;
    std::string last_operation;
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

amf::AmfNode make_node(TestN2& n2, TestSbi& sbi, TestN8& n8, TestN11& n11) {
    amf::AmfPeerInterfaces peers {};
    peers.n8 = &n8;
    peers.n11 = &n11;
    return amf::AmfNode(n2, sbi, peers);
}

bool test_n8_legacy_and_structured_requests() {
    TestN2 n2;
    TestSbi sbi;
    TestN8 n8;
    TestN11 n11;
    auto node = make_node(n2, sbi, n8, n11);

    const std::string imsi = "250030000008001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.query_n8_subscription(imsi, "get-am-data"), "Legacy N8 get-am-data should be accepted");
    ok &= check(contains(n8.last_request, "N8SBI/1\n"), "N8 should emit structured payload");
    ok &= check(contains(n8.last_request, "procedure=GetAmData"), "N8 should encode GetAmData procedure");

    ok &= check(node.query_n8_subscription(imsi, "N8SBI|procedure=GetSmfSelectionData|dataset=smf-selection-data"), "Structured N8 request should be accepted");
    ok &= check(contains(n8.last_request, "procedure=GetSmfSelectionData"), "N8 should encode GetSmfSelectionData");
    ok &= check(contains(n8.last_request, "ie.dataset=smf-selection-data"), "N8 should include dataset IE");

    return ok;
}

bool test_n8_missing_dataset_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN8 n8;
    TestN11 n11;
    auto node = make_node(n2, sbi, n8, n11);

    const std::string imsi = "250030000008002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(!node.query_n8_subscription(imsi, "N8SBI|procedure=GetAmData"), "Structured N8 missing dataset should be rejected");
    ok &= check(contains(n8.last_request, "procedure=ErrorIndication"), "N8 rejection should emit ErrorIndication");
    ok &= check(contains(n8.last_request, "ie.cause=missing-mandatory-ie"), "N8 rejection should include missing-mandatory-ie");

    return ok;
}

bool test_n11_create_modify_release_flow() {
    TestN2 n2;
    TestSbi sbi;
    TestN8 n8;
    TestN11 n11;
    auto node = make_node(n2, sbi, n8, n11);

    const std::string imsi = "250030000011001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.manage_n11_pdu_session(imsi, "create"), "Legacy N11 create should be accepted");
    ok &= check(contains(n11.last_operation, "N11SBI/1\n"), "N11 should emit structured payload");
    ok &= check(contains(n11.last_operation, "procedure=Create"), "N11 should encode Create procedure");

    ok &= check(node.manage_n11_pdu_session(imsi, "N11SBI|procedure=Modify|pdu-session-id=10|dnn=ims"), "Structured N11 modify should be accepted");
    ok &= check(contains(n11.last_operation, "procedure=Modify"), "N11 should encode Modify procedure");
    ok &= check(contains(n11.last_operation, "ie.dnn=ims"), "N11 modify should update DNN");

    ok &= check(node.manage_n11_pdu_session(imsi, "release"), "Legacy N11 release should be accepted");
    ok &= check(contains(n11.last_operation, "procedure=Release"), "N11 should encode Release procedure");

    return ok;
}

bool test_n11_no_context_and_mismatch_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN8 n8;
    TestN11 n11;
    auto node = make_node(n2, sbi, n8, n11);

    const std::string imsi = "250030000011002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(!node.manage_n11_pdu_session(imsi, "modify"), "Modify without context should be rejected");
    ok &= check(contains(n11.last_operation, "ie.cause=no-session-context"), "No context should emit no-session-context");

    ok &= check(node.manage_n11_pdu_session(imsi, "create"), "Create should be accepted");
    ok &= check(!node.manage_n11_pdu_session(imsi, "N11SBI|procedure=Modify|pdu-session-id=99"), "Session id mismatch should be rejected");
    ok &= check(contains(n11.last_operation, "ie.cause=session-id-mismatch"), "Mismatch should emit session-id-mismatch");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n8_legacy_and_structured_requests();
    ok &= test_n8_missing_dataset_rejected();
    ok &= test_n11_create_modify_release_flow();
    ok &= test_n11_no_context_and_mismatch_rejected();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N8/N11 interop/negative tests passed.\n";
    return 0;
}
