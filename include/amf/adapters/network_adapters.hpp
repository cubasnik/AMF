#pragma once

#include <chrono>
#include <ostream>
#include <string>

#include "amf/config/runtime_config.hpp"
#include "amf/interfaces.hpp"
#include "amf/logging/file_logger.hpp"

namespace amf {

class NetworkN2Adapter final : public IN2Interface {
public:
    NetworkN2Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void deliver_nas(const std::string& imsi, const std::string& payload) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkSbiAdapter final : public ISbiInterface {
public:
    NetworkSbiAdapter(
        const InterfaceEndpointConfig& endpoint,
        const SbiResilienceConfig& resilience,
        std::ostream& out,
        FileLogger* logger = nullptr);
    bool notify_service(const std::string& service_name, const std::string& payload) override;
    std::string last_failure_reason() const override;
    std::optional<SbiFailureCounters> failure_counters() const override;

private:
    InterfaceEndpointConfig endpoint_;
    SbiResilienceConfig resilience_;
    SbiFailureCounters counters_ {};
    std::string last_failure_reason_ {"service-reject"};
    std::size_t consecutive_failures_ {0};
    std::chrono::system_clock::time_point circuit_open_until_ {};
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN1Adapter final : public IN1Interface {
public:
    NetworkN1Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void send_nas_to_ue(const std::string& imsi, const std::string& payload) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN3Adapter final : public IN3Interface {
public:
    NetworkN3Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void forward_user_plane(const std::string& imsi, const std::string& payload) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN8Adapter final : public IN8Interface {
public:
    NetworkN8Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void query_subscription(const std::string& imsi, const std::string& request) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN11Adapter final : public IN11Interface {
public:
    NetworkN11Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void manage_pdu_session(const std::string& imsi, const std::string& operation) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN12Adapter final : public IN12Interface {
public:
    NetworkN12Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void authenticate_ue(const std::string& imsi, const std::string& request) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN14Adapter final : public IN14Interface {
public:
    NetworkN14Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void transfer_amf_context(const std::string& imsi, const std::string& request) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN15Adapter final : public IN15Interface {
public:
    NetworkN15Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void query_policy(const std::string& imsi, const std::string& request) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN22Adapter final : public IN22Interface {
public:
    NetworkN22Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void select_network_slice(const std::string& imsi, const std::string& request) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

class NetworkN26Adapter final : public IN26Interface {
public:
    NetworkN26Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger = nullptr);
    void interwork_with_mme(const std::string& imsi, const std::string& operation) override;

private:
    InterfaceEndpointConfig endpoint_;
    std::ostream& out_;
    FileLogger* logger_;
};

}  // namespace amf
