#include <iostream>
#include <string>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestN3 final : public amf::IN3Interface {
public:
    void forward_user_plane(const std::string& imsi, const std::string& payload) override {
        ++packets;
        last_imsi = imsi;
        last_payload = payload;
    }

    int packets {0};
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

bool test_n3_legacy_payload_interop() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.forward_n3_user_plane(imsi, "tunnel-establish"), "Legacy tunnel-establish should be accepted");
    ok &= check(contains(n3.last_payload, "GTPU/1\n"), "N3 should emit structured GTPU payload");
    ok &= check(contains(n3.last_payload, "message=TunnelEstablishAck"), "Tunnel establish should emit TunnelEstablishAck");

    ok &= check(node.forward_n3_user_plane(imsi, "hello-upf"), "Legacy plain payload should map to UL TPDU");
    ok &= check(contains(n3.last_payload, "message=TPDU"), "Legacy payload should emit TPDU");
    ok &= check(contains(n3.last_payload, "dir=UL"), "Legacy payload should emit UL direction");
    ok &= check(contains(n3.last_payload, "payload=hello-upf"), "Legacy payload should be preserved");

    return ok;
}

bool test_n3_structured_lifecycle() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=1001|qfi=7"), "Structured TunnelEstablish should be accepted");
    ok &= check(contains(n3.last_payload, "message=TunnelEstablishAck"), "Structured establish should emit ack");
    ok &= check(contains(n3.last_payload, "teid=1001"), "Ack should include configured TEID");

    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=UplinkTpdu|teid=1001|payload=alpha"), "UplinkTpdu should be accepted");
    ok &= check(contains(n3.last_payload, "seq=1"), "First UL TPDU should set sequence to 1");

    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=DownlinkTpdu|teid=1001|payload=beta"), "DownlinkTpdu should be accepted");
    ok &= check(contains(n3.last_payload, "dir=DL"), "Downlink TPDU should set DL direction");
    ok &= check(contains(n3.last_payload, "seq=1"), "First DL TPDU should set sequence to 1");

    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=TunnelRelease|teid=1001"), "TunnelRelease should be accepted");
    ok &= check(contains(n3.last_payload, "message=TunnelReleaseAck"), "Release should emit release ack");

    return ok;
}

bool test_n3_duplicate_establish_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=2001|qfi=9"), "First establish should be accepted");

    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=2001|qfi=9"), "Duplicate establish should be rejected");
    ok &= check(contains(n3.last_payload, "message=ErrorIndication"), "Duplicate establish should emit ErrorIndication");
    ok &= check(contains(n3.last_payload, "cause=duplicate-tunnel-establish"), "Duplicate establish should emit specific cause");

    return ok;
}

bool test_n3_no_context_tpdu_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=UplinkTpdu|teid=3001|payload=data"), "TPDU without tunnel should be rejected");
    ok &= check(contains(n3.last_payload, "cause=no-tunnel-context"), "No context should emit no-tunnel-context cause");

    return ok;
}

bool test_n3_teid_mismatch_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=4001|qfi=9"), "Tunnel establish should be accepted");

    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=UplinkTpdu|teid=4999|payload=bad"), "TEID mismatch should be rejected");
    ok &= check(contains(n3.last_payload, "cause=teid-mismatch"), "TEID mismatch should emit teid-mismatch cause");

    return ok;
}

bool test_n3_missing_ie_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003006";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=5001"),
        "Structured TunnelEstablish without qfi should be rejected");
    ok &= check(contains(n3.last_payload, "message=ErrorIndication"), "Missing IE should emit ErrorIndication");
    ok &= check(contains(n3.last_payload, "cause=missing-mandatory-ie"), "Missing IE should emit missing-mandatory-ie cause");

    return ok;
}

bool test_n3_invalid_teid_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003007";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=TunnelEstablish|teid=0|qfi=9"),
        "TunnelEstablish with invalid TEID should be rejected");
    ok &= check(contains(n3.last_payload, "cause=invalid-teid"), "Invalid TEID should emit invalid-teid cause");

    return ok;
}

bool test_n3_unsupported_message_rejected() {
    TestN2 n2;
    TestN3 n3;
    TestSbi sbi;
    amf::AmfPeerInterfaces peers {};
    peers.n3 = &n3;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000003008";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.forward_n3_user_plane(imsi, "GTPU|message=EchoRequest|teid=6001"),
        "Unsupported structured GTPU message should be rejected");
    ok &= check(contains(n3.last_payload, "message=ErrorIndication"), "Unsupported message should emit ErrorIndication");
    ok &= check(contains(n3.last_payload, "cause=unsupported-message"), "Unsupported message should emit unsupported-message cause");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n3_legacy_payload_interop();
    ok &= test_n3_structured_lifecycle();
    ok &= test_n3_duplicate_establish_rejected();
    ok &= test_n3_no_context_tpdu_rejected();
    ok &= test_n3_teid_mismatch_rejected();
    ok &= test_n3_missing_ie_rejected();
    ok &= test_n3_invalid_teid_rejected();
    ok &= test_n3_unsupported_message_rejected();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N3 interop/negative tests passed.\n";
    return 0;
}
