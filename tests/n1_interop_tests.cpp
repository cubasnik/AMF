#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestSbi final : public amf::ISbiInterface {
public:
    bool notify_service(const std::string&, const std::string&) override { return true; }
};

class TestN1 final : public amf::IN1Interface {
public:
    void send_nas_to_ue(const std::string& imsi, const std::string& payload) override {
        ++messages;
        last_imsi = imsi;
        last_payload = payload;
    }

    int messages {0};
    std::string last_imsi;
    std::string last_payload;
};

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::string extract_field(const std::string& pdu, const std::string& key) {
    const std::string needle = key + "=";
    const std::size_t pos = pdu.find(needle);
    if (pos == std::string::npos) {
        return {};
    }

    const std::size_t value_start = pos + needle.size();
    const std::size_t value_end = pdu.find('|', value_start);
    return pdu.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
}

std::uint32_t fnv1a32(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

std::string to_hex8(std::uint32_t value) {
    std::ostringstream oss;
    oss.setf(std::ios::hex, std::ios::basefield);
    oss.setf(std::ios::uppercase);
    oss.fill('0');
    oss.width(8);
    oss << value;
    return oss.str();
}

std::string expected_res_star(const std::string& imsi, const std::string& rand) {
    return to_hex8(fnv1a32(imsi + ":res*:" + rand));
}

bool bootstrap_authentication_pending(amf::AmfNode& node, TestN1& n1, const std::string& imsi) {
    if (!node.send_n1_nas(imsi, "NAS5G|dir=UL|message=RegistrationRequest")) {
        return false;
    }
    return n1.last_payload.find("message=AuthenticationRequest") != std::string::npos;
}

bool test_n1_replay_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000001001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(bootstrap_authentication_pending(node, n1, imsi), "UE should move to authentication pending");

    const std::string rand = extract_field(n1.last_payload, "rand");
    const std::string res_star = expected_res_star(imsi, rand);

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + res_star),
        "First AuthenticationResponse should be accepted");
    ok &= check(n1.last_payload.find("message=SecurityModeCommand") != std::string::npos,
        "AMF should return SecurityModeCommand after valid AuthenticationResponse");

    ok &= check(!node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + res_star),
        "Replay AuthenticationResponse should be rejected");
    ok &= check(n1.last_payload.find("message=ServiceReject") != std::string::npos,
        "Replay should trigger ServiceReject");
    ok &= check(n1.last_payload.find("cause=unexpected-auth-response") != std::string::npos,
        "Replay reject cause should be unexpected-auth-response");

    return ok;
}

bool test_n1_tamper_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000001002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(bootstrap_authentication_pending(node, n1, imsi), "UE should move to authentication pending");

    const std::string rand = extract_field(n1.last_payload, "rand");
    std::string tampered_res = expected_res_star(imsi, rand);
    tampered_res[0] = tampered_res[0] == 'A' ? 'B' : 'A';

    ok &= check(!node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + tampered_res),
        "Tampered AuthenticationResponse should be rejected");
    ok &= check(n1.last_payload.find("message=ServiceReject") != std::string::npos,
        "Tampered auth should trigger ServiceReject");
    ok &= check(n1.last_payload.find("cause=auth-failed") != std::string::npos,
        "Tampered auth reject cause should be auth-failed");

    return ok;
}

bool test_n1_timer_expiry_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000001003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(bootstrap_authentication_pending(node, n1, imsi), "UE should move to authentication pending");

    const std::string rand = extract_field(n1.last_payload, "rand");
    const std::string res_star = expected_res_star(imsi, rand);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    ok &= check(!node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + res_star),
        "AuthenticationResponse after timeout should be rejected");
    ok &= check(n1.last_payload.find("message=ServiceReject") != std::string::npos,
        "Timer expiry should trigger ServiceReject");
    ok &= check(n1.last_payload.find("cause=timer-expiry") != std::string::npos,
        "Timer expiry reject cause should be timer-expiry");

    return ok;
}

bool test_n1_legacy_alias_interop() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    amf::AmfNode node(n2, sbi, peers);

    const std::string imsi = "250030000001004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");

    ok &= check(node.send_n1_nas(imsi, "registration-request"), "Legacy registration-request alias should be accepted");
    const std::string rand = extract_field(n1.last_payload, "rand");
    const std::string res_star = expected_res_star(imsi, rand);

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + res_star),
        "NAS5G AuthenticationResponse should interoperate with legacy start");
    ok &= check(node.send_n1_nas(imsi, "security-mode-complete"), "Legacy security-mode-complete alias should be accepted");
    ok &= check(node.send_n1_nas(imsi, "deregistration-request"), "Legacy deregistration-request alias should be accepted");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n1_replay_rejected();
    ok &= test_n1_tamper_rejected();
    ok &= test_n1_timer_expiry_rejected();
    ok &= test_n1_legacy_alias_interop();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N1 interop/negative tests passed.\n";
    return 0;
}
