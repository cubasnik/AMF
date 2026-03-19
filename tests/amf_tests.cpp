#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "amf/amf.hpp"
#include "amf/adapters/network_adapters.hpp"
#include "amf/modules/mobility.hpp"
#include "amf/modules/registration.hpp"
#include "amf/modules/session_management.hpp"

namespace {

#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket kInvalidSocket = INVALID_SOCKET;

class TestSocketRuntime {
public:
    TestSocketRuntime() {
        WSADATA data {};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~TestSocketRuntime() {
        if (ok_) {
            WSACleanup();
        }
    }

    bool ok() const { return ok_; }

private:
    bool ok_ {false};
};
#else
using TestSocket = int;
constexpr TestSocket kInvalidSocket = -1;
#endif

void close_test_socket(TestSocket sock) {
    if (sock == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
}

struct OneShotHttpServer {
    TestSocket listen_socket {kInvalidSocket};
    std::uint16_t port {0};
    bool received_request {false};
    std::thread worker;

    ~OneShotHttpServer() {
        if (worker.joinable()) {
            worker.join();
        }
        close_test_socket(listen_socket);
    }
};

bool start_one_shot_http_server_raw(const std::string& raw_response, OneShotHttpServer& server) {
#if defined(_WIN32)
    static TestSocketRuntime runtime;
    if (!runtime.ok()) {
        return false;
    }
#endif

    server.listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listen_socket == kInvalidSocket) {
        return false;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (bind(server.listen_socket, reinterpret_cast<const sockaddr*>(&addr), static_cast<int>(sizeof(addr))) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }

    if (listen(server.listen_socket, 1) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }

    sockaddr_in bound {};
#if defined(_WIN32)
    int bound_len = sizeof(bound);
#else
    socklen_t bound_len = sizeof(bound);
#endif
    if (getsockname(server.listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }
    server.port = ntohs(bound.sin_port);

    server.worker = std::thread([&server, raw_response]() {
#if defined(_WIN32)
        int client_len = sizeof(sockaddr_in);
#else
        socklen_t client_len = sizeof(sockaddr_in);
#endif
        sockaddr_in client_addr {};
        const TestSocket client = accept(server.listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == kInvalidSocket) {
            return;
        }

        char buffer[4096] {};
        const int rc = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (rc > 0) {
            server.received_request = true;
        }

        send(client, raw_response.c_str(), static_cast<int>(raw_response.size()), 0);
        close_test_socket(client);
    });

    return true;
}

bool start_one_shot_http_server(const std::string& status_line, OneShotHttpServer& server) {
    const std::string response =
        "HTTP/1.1 " + status_line + "\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    return start_one_shot_http_server_raw(response, server);
}

bool start_one_shot_http_server_no_response(std::size_t hold_open_ms, OneShotHttpServer& server) {
#if defined(_WIN32)
    static TestSocketRuntime runtime;
    if (!runtime.ok()) {
        return false;
    }
#endif

    server.listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listen_socket == kInvalidSocket) {
        return false;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (bind(server.listen_socket, reinterpret_cast<const sockaddr*>(&addr), static_cast<int>(sizeof(addr))) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }

    if (listen(server.listen_socket, 1) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }

    sockaddr_in bound {};
#if defined(_WIN32)
    int bound_len = sizeof(bound);
#else
    socklen_t bound_len = sizeof(bound);
#endif
    if (getsockname(server.listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        close_test_socket(server.listen_socket);
        server.listen_socket = kInvalidSocket;
        return false;
    }
    server.port = ntohs(bound.sin_port);

    server.worker = std::thread([&server, hold_open_ms]() {
#if defined(_WIN32)
        int client_len = sizeof(sockaddr_in);
#else
        socklen_t client_len = sizeof(sockaddr_in);
#endif
        sockaddr_in client_addr {};
        const TestSocket client = accept(server.listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == kInvalidSocket) {
            return;
        }

        char buffer[4096] {};
        const int rc = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (rc > 0) {
            server.received_request = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(hold_open_ms));
        close_test_socket(client);
    });

    return true;
}

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
    bool notify_service(const std::string& service_name, const std::string& payload) override {
        ++notifications;
        last_service = service_name;
        last_payload = payload;
        return true;
    }

    int notifications {0};
    std::string last_service;
    std::string last_payload;
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

class TestN14 final : public amf::IN14Interface {
public:
    void transfer_amf_context(const std::string& imsi, const std::string& request) override {
        ++transfers;
        last_imsi = imsi;
        last_request = request;
    }
    int transfers {0};
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

class TestN22 final : public amf::IN22Interface {
public:
    void select_network_slice(const std::string& imsi, const std::string& request) override {
        ++selections;
        last_imsi = imsi;
        last_request = request;
    }
    int selections {0};
    std::string last_imsi;
    std::string last_request;
};

class TestN26 final : public amf::IN26Interface {
public:
    void interwork_with_mme(const std::string& imsi, const std::string& operation) override {
        ++operations;
        last_imsi = imsi;
        last_payload = operation;
    }
    int operations {0};
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
    ok &= check(node.send_n1_nas("250030000000111", "registration-request"), "AMF N1 send should succeed");
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
    ok &= check(n2.last_payload.find("NGAP/1\n") != std::string::npos, "N2 adapter should receive structured NGAP payload");
    ok &= check(n2.last_payload.find("procedure=InitialUEMessage") != std::string::npos, "N2 adapter should receive InitialUEMessage procedure");
    ok &= check(n1.messages == 1, "N1 adapter should receive one message");
    ok &= check(n3.packets == 1, "N3 adapter should receive one packet");
    ok &= check(n3.last_payload.find("GTPU/1\n") != std::string::npos, "N3 adapter should receive structured GTPU payload");
    ok &= check(n3.last_payload.find("message=ErrorIndication") == std::string::npos, "N3 adapter should not receive ErrorIndication in happy-path flow");
    ok &= check(n8.queries == 1, "N8 adapter should receive one query");
    ok &= check(n11.operations == 1, "N11 adapter should receive one operation");
    ok &= check(n8.last_request.find("N8SBI/1\n") != std::string::npos, "N8 adapter should receive structured N8SBI payload");
    ok &= check(n11.last_operation.find("N11SBI/1\n") != std::string::npos, "N11 adapter should receive structured N11SBI payload");
    ok &= check(n12.requests == 1, "N12 adapter should receive one request");
    ok &= check(n12.last_request.find("N12SBI/1\n") != std::string::npos, "N12 adapter should receive structured N12SBI payload");
    ok &= check(n14.transfers == 1, "N14 adapter should receive one transfer");
    ok &= check(n14.last_request.find("N14SBI/1\n") != std::string::npos, "N14 adapter should receive structured N14SBI payload");
    ok &= check(n14.last_request.find("procedure=ContextTransfer") != std::string::npos, "N14 adapter should receive ContextTransfer procedure");
    ok &= check(n15.queries == 1, "N15 adapter should receive one query");
    ok &= check(n15.last_request.find("N15SBI/1\n") != std::string::npos, "N15 adapter should receive structured N15SBI payload");
    ok &= check(n22.selections == 1, "N22 adapter should receive one selection");
    ok &= check(n22.last_request.find("N22SBI/1\n") != std::string::npos, "N22 adapter should receive structured N22SBI payload");
    ok &= check(n22.last_request.find("procedure=SelectSlice") != std::string::npos, "N22 adapter should receive SelectSlice procedure");
    ok &= check(n26.operations == 1, "N26 adapter should receive one operation");
    ok &= check(n26.last_payload.find("GTPV2C/1\n") != std::string::npos, "N26 adapter should receive structured GTPV2C payload");
    ok &= check(n26.last_payload.find("procedure=HandoverRequest") != std::string::npos, "N26 adapter should receive HandoverRequest procedure");

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

    const auto telemetry = node.list_interface_telemetry(60);
    const auto find_tel = [&](const std::string& name) {
        return std::find_if(
            telemetry.begin(), telemetry.end(),
            [&](const amf::InterfaceTelemetry& t) { return t.name == name; });
    };
    const auto n2_tel = find_tel("N2");
    ok &= check(n2_tel != telemetry.end(), "N2 telemetry should be present");
    ok &= check(n2_tel != telemetry.end() && n2_tel->attempts_in_window >= 1, "N2 telemetry should include attempts");
    ok &= check(n2_tel != telemetry.end() && n2_tel->success_rate_percent > 0.0, "N2 telemetry should include non-zero success rate");
    return ok;
}

bool test_network_sbi_response_handling() {
    bool ok = true;

    {
        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server("200 OK", server), "HTTP test server should start for 200 response test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for SBI 200 response test");
        ok &= check(node.notify_sbi("namf-comm", "event-200"), "SBI notify should succeed on HTTP 200 response");

        if (server.worker.joinable()) {
            server.worker.join();
        }
        ok &= check(server.received_request, "HTTP 200 server should receive request payload");

        const auto diagnostics = node.list_interface_diagnostics();
        const auto sbi_diag = std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [](const amf::InterfaceDiagnostics& diag) { return diag.name == "SBI"; });
        ok &= check(sbi_diag != diagnostics.end(), "SBI diagnostics should be present after 200 response");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->success_count == 1, "SBI success counter should increment on HTTP 200");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->error_count == 0, "SBI error counter should remain zero on HTTP 200");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_non_2xx_failures == 0, "SBI non-2xx counter should stay zero on HTTP 200");
    }

    {
        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server("500 Internal Server Error", server), "HTTP test server should start for 500 response test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for SBI 500 response test");
        ok &= check(!node.notify_sbi("namf-comm", "event-500"), "SBI notify should fail on HTTP 500 response");

        if (server.worker.joinable()) {
            server.worker.join();
        }
        ok &= check(server.received_request, "HTTP 500 server should receive request payload");

        const auto diagnostics = node.list_interface_diagnostics();
        const auto sbi_diag = std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [](const amf::InterfaceDiagnostics& diag) { return diag.name == "SBI"; });
        ok &= check(sbi_diag != diagnostics.end(), "SBI diagnostics should be present after 500 response");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->success_count == 0, "SBI success counter should stay zero on HTTP 500");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->error_count == 1, "SBI error counter should increment on HTTP 500");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->status_reason == "service-reject", "SBI status reason should be service-reject on HTTP 500");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_non_2xx_failures == 1, "SBI non-2xx counter should increment on HTTP 500");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_connect_failures == 0, "SBI connect-fail counter should stay zero on HTTP 500");
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_timeout_failures == 0, "SBI timeout counter should stay zero on HTTP 500");
    }

    {
        const std::string long_header_value(8192, 'x');
        const std::string chunked_response =
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "X-Long-Header: " + long_header_value + "\r\n"
            "Connection: close\r\n\r\n"
            "4\r\nWiki\r\n"
            "5\r\npedia\r\n"
            "0\r\n\r\n";

        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server_raw(chunked_response, server), "HTTP test server should start for chunked response test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for SBI chunked response test");
        ok &= check(node.notify_sbi("namf-comm", "event-chunked"), "SBI notify should succeed on HTTP chunked response");

        if (server.worker.joinable()) {
            server.worker.join();
        }
        ok &= check(server.received_request, "HTTP chunked server should receive request payload");
    }

    {
        const std::string invalid_status_response =
            "HTTP/11 200 OK\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";

        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server_raw(invalid_status_response, server), "HTTP test server should start for invalid status-line test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for SBI invalid status-line test");
        ok &= check(!node.notify_sbi("namf-comm", "event-invalid-status"), "SBI notify should fail on invalid HTTP status-line");

        if (server.worker.joinable()) {
            server.worker.join();
        }

        const auto diagnostics = node.list_interface_diagnostics();
        const auto sbi_diag = std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [](const amf::InterfaceDiagnostics& diag) { return diag.name == "SBI"; });
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_non_2xx_failures == 1, "SBI non-2xx counter should increment on invalid status-line");
    }

    {
        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server_no_response(300, server), "HTTP test server should start for no-response timeout test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.timeout_ms = 100;
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for SBI timeout test");
        ok &= check(!node.notify_sbi("namf-comm", "event-timeout"), "SBI notify should fail when server sends no response");

        if (server.worker.joinable()) {
            server.worker.join();
        }
        ok &= check(server.received_request, "No-response server should receive request payload");

        const auto diagnostics = node.list_interface_diagnostics();
        const auto sbi_diag = std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [](const amf::InterfaceDiagnostics& diag) { return diag.name == "SBI"; });
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_timeout_failures == 1, "SBI timeout counter should increment on no-response timeout");
    }

    {
        const std::string malformed_http_response =
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n"
            "Z\r\nnot-hex\r\n";

        OneShotHttpServer server;
        ok &= check(start_one_shot_http_server_raw(malformed_http_response, server), "HTTP test server should start for malformed response test");

        amf::InterfaceEndpointConfig endpoint {"127.0.0.1", server.port, "tcp"};
        TestN2 n2;
        std::ostringstream sink;
        amf::SbiResilienceConfig resilience {};
        resilience.retry_count = 0;
        resilience.circuit_breaker_failure_threshold = 3;
        resilience.circuit_breaker_reset_seconds = 5;
        amf::NetworkSbiAdapter sbi(endpoint, resilience, sink, nullptr);
        amf::AmfNode node(n2, sbi);

        ok &= check(node.start(), "AMF should start for malformed HTTP response test");
        ok &= check(!node.notify_sbi("namf-comm", "event-malformed"), "SBI notify should fail on malformed HTTP response");

        if (server.worker.joinable()) {
            server.worker.join();
        }

        const auto diagnostics = node.list_interface_diagnostics();
        const auto sbi_diag = std::find_if(
            diagnostics.begin(), diagnostics.end(),
            [](const amf::InterfaceDiagnostics& diag) { return diag.name == "SBI"; });
        ok &= check(sbi_diag != diagnostics.end() && sbi_diag->sbi_non_2xx_failures == 1, "SBI non-2xx counter should increment on malformed HTTP response");
    }

    return ok;
}

bool test_n1_nas_security_fsm() {
    TestN2 n2;
    TestSbi sbi;
    TestN1 n1;

    amf::AmfPeerInterfaces peers {};
    peers.n1 = &n1;
    amf::AmfNode node(n2, sbi, peers);

    bool ok = true;
    const std::string imsi = "250030000000222";

    ok &= check(node.start(), "AMF should start for N1 NAS FSM test");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered for N1 NAS FSM test");

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=RegistrationRequest"), "Registration request should be accepted");
    ok &= check(n1.last_payload.find("message=AuthenticationRequest") != std::string::npos, "AMF should answer RegistrationRequest with AuthenticationRequest");

    const std::size_t rand_pos = n1.last_payload.find("rand=");
    ok &= check(rand_pos != std::string::npos, "AuthenticationRequest should contain RAND");

    std::string rand;
    if (rand_pos != std::string::npos) {
        const std::size_t value_start = rand_pos + 5;
        const std::size_t value_end = n1.last_payload.find('|', value_start);
        rand = n1.last_payload.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
    }

    auto fnv1a32 = [](const std::string& text) {
        std::uint32_t hash = 2166136261u;
        for (const unsigned char ch : text) {
            hash ^= static_cast<std::uint32_t>(ch);
            hash *= 16777619u;
        }
        return hash;
    };
    auto to_hex8 = [](std::uint32_t value) {
        std::ostringstream oss;
        oss.setf(std::ios::hex, std::ios::basefield);
        oss.setf(std::ios::uppercase);
        oss.fill('0');
        oss.width(8);
        oss << value;
        return oss.str();
    };

    const std::string res_star = to_hex8(fnv1a32(imsi + ":res*:" + rand));
    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=" + res_star), "Authentication response should be accepted");
    ok &= check(n1.last_payload.find("message=SecurityModeCommand") != std::string::npos, "AMF should answer valid AuthenticationResponse with SecurityModeCommand");
    ok &= check(n1.last_payload.find("mac=") != std::string::npos, "SecurityModeCommand should include integrity MAC");

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=SecurityModeComplete"), "Security mode complete should be accepted");
    ok &= check(n1.last_payload.find("message=RegistrationAccept") != std::string::npos, "AMF should answer SecurityModeComplete with RegistrationAccept");

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=DeregistrationRequest"), "Deregistration request should be accepted");
    ok &= check(n1.last_payload.find("message=DeregistrationAccept") != std::string::npos, "AMF should answer DeregistrationRequest with DeregistrationAccept");

    ok &= check(node.send_n1_nas(imsi, "NAS5G|dir=UL|message=AuthenticationResponse|res*=BAD0BAD0") == false,
        "Out-of-sequence AuthenticationResponse should be rejected");
    ok &= check(n1.last_payload.find("message=ServiceReject") != std::string::npos, "Out-of-sequence NAS should trigger ServiceReject");

    const auto st = node.status();
    ok &= check(st.stats.n1_messages == 4, "Only successful N1 NAS procedures should increment N1 stats");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_mobility_module();
    ok &= test_registration_module();
    ok &= test_session_management_module();
    ok &= test_amf_node_integration();
    ok &= test_network_sbi_response_handling();
    ok &= test_n1_nas_security_fsm();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] All AMF tests passed.\n";
    return 0;
}
