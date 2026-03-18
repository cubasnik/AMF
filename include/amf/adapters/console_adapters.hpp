#pragma once

#include <ostream>
#include <string>

#include "amf/interfaces.hpp"
#include "amf/logging/file_logger.hpp"

namespace amf {

class ConsoleN2Adapter final : public IN2Interface {
public:
    ConsoleN2Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void deliver_nas(const std::string& imsi, const std::string& payload) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleSbiAdapter final : public ISbiInterface {
public:
    ConsoleSbiAdapter(std::ostream& out, FileLogger* logger = nullptr);

    void notify_service(const std::string& service_name, const std::string& payload) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN1Adapter final : public IN1Interface {
public:
    ConsoleN1Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void send_nas_to_ue(const std::string& imsi, const std::string& payload) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN3Adapter final : public IN3Interface {
public:
    ConsoleN3Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void forward_user_plane(const std::string& imsi, const std::string& payload) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN8Adapter final : public IN8Interface {
public:
    ConsoleN8Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void query_subscription(const std::string& imsi) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN11Adapter final : public IN11Interface {
public:
    ConsoleN11Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void manage_pdu_session(const std::string& imsi, const std::string& operation) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN12Adapter final : public IN12Interface {
public:
    ConsoleN12Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void authenticate_ue(const std::string& imsi) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN14Adapter final : public IN14Interface {
public:
    ConsoleN14Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void transfer_amf_context(const std::string& imsi, const std::string& target_amf) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN15Adapter final : public IN15Interface {
public:
    ConsoleN15Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void query_policy(const std::string& imsi) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN22Adapter final : public IN22Interface {
public:
    ConsoleN22Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void select_network_slice(const std::string& imsi, const std::string& snssai) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

class ConsoleN26Adapter final : public IN26Interface {
public:
    ConsoleN26Adapter(std::ostream& out, FileLogger* logger = nullptr);

    void interwork_with_mme(const std::string& imsi, const std::string& operation) override;

private:
    std::ostream& out_;
    FileLogger* logger_;
};

}  // namespace amf
