#include <iostream>
#include <string>

#include "amf/amf.hpp"

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

bool test_n2_initial_ue_and_context_setup() {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    const std::string imsi = "250030000002001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.send_n2_nas(imsi, "registration-request"), "Legacy registration-request should produce InitialUEMessage");
    ok &= check(contains(n2.last_payload, "NGAP/1\n"), "N2 payload should be structured NGAP");
    ok &= check(contains(n2.last_payload, "procedure=InitialUEMessage"), "N2 should encode InitialUEMessage");
    ok &= check(contains(n2.last_payload, "ie.nas-pdu=NAS5G|dir=UL|message=RegistrationRequest"), "InitialUEMessage should carry NAS PDU IE");
    ok &= check(contains(n2.last_payload, "ie.rrc-establishment-cause=mo-Signalling"), "InitialUEMessage should carry RRC cause IE");

    ok &= check(node.send_n2_nas(imsi, "initial-context-setup"), "InitialContextSetupRequest should be accepted after InitialUEMessage");
    ok &= check(contains(n2.last_payload, "procedure=InitialContextSetupRequest"), "N2 should encode InitialContextSetupRequest");
    ok &= check(contains(n2.last_payload, "ie.pdu-session-id=10"), "Context setup should include PDU session IE");
    ok &= check(contains(n2.last_payload, "ie.qos-flow-id=5"), "Context setup should include QoS flow IE");

    return ok;
}

bool test_n2_release_and_paging() {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    const std::string imsi = "250030000002002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.send_n2_nas(imsi, "registration-request"), "InitialUEMessage should be accepted");
    ok &= check(node.send_n2_nas(imsi, "initial-context-setup"), "Context setup should be accepted");
    ok &= check(node.send_n2_nas(imsi, "paging"), "Paging should be accepted");
    ok &= check(contains(n2.last_payload, "procedure=Paging"), "Paging should be encoded");
    ok &= check(contains(n2.last_payload, "ie.ue-paging-id=" + imsi), "Paging should include paging ID IE");

    ok &= check(node.send_n2_nas(imsi, "ue-context-release"), "UEContextReleaseCommand should be accepted");
    ok &= check(contains(n2.last_payload, "procedure=UEContextReleaseCommand"), "Release command should be encoded");
    ok &= check(contains(n2.last_payload, "ie.cause=user-inactivity"), "Release command should include cause IE");

    return ok;
}

bool test_n2_missing_ie_rejected() {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    const std::string imsi = "250030000002003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.send_n2_nas(imsi, "NGAP|procedure=InitialUEMessage|ran-ue-ngap-id=RAN-1"), "Missing mandatory IEs should be rejected");
    ok &= check(contains(n2.last_payload, "procedure=ErrorIndication"), "Rejected message should emit ErrorIndication");
    ok &= check(contains(n2.last_payload, "ie.cause=missing-mandatory-ie"), "ErrorIndication should contain missing-mandatory-ie cause");

    return ok;
}

bool test_n2_duplicate_initial_ue_rejected() {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    const std::string imsi = "250030000002004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.send_n2_nas(imsi, "registration-request"), "First InitialUEMessage should be accepted");
    ok &= check(!node.send_n2_nas(imsi, "registration-request"), "Duplicate InitialUEMessage should be rejected");
    ok &= check(contains(n2.last_payload, "ie.cause=duplicate-initial-ue-message"), "Duplicate initial UE should emit specific error indication");

    return ok;
}

bool test_n2_release_without_context_rejected() {
    TestN2 n2;
    TestSbi sbi;
    amf::AmfNode node(n2, sbi);

    const std::string imsi = "250030000002005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.send_n2_nas(imsi, "ue-context-release"), "Release without context should be rejected");
    ok &= check(contains(n2.last_payload, "ie.cause=no-ue-context"), "Release without context should emit no-ue-context error");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n2_initial_ue_and_context_setup();
    ok &= test_n2_release_and_paging();
    ok &= test_n2_missing_ie_rejected();
    ok &= test_n2_duplicate_initial_ue_rejected();
    ok &= test_n2_release_without_context_rejected();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N2 interop/negative tests passed.\n";
    return 0;
}
