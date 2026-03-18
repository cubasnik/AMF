#include "amf/adapters/console_adapters.hpp"

namespace amf {

ConsoleN2Adapter::ConsoleN2Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN2Adapter::deliver_nas(const std::string& imsi, const std::string& payload) {
    out_ << "[N2] imsi=" << imsi << " payload=\"" << payload << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N2 deliver_nas imsi=" + imsi + " payload=\"" + payload + "\"");
    }
}

ConsoleSbiAdapter::ConsoleSbiAdapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleSbiAdapter::notify_service(const std::string& service_name, const std::string& payload) {
    out_ << "[SBI] service=" << service_name << " payload=\"" << payload << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "SBI notify service=" + service_name + " payload=\"" + payload + "\"");
    }
}

ConsoleN1Adapter::ConsoleN1Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN1Adapter::send_nas_to_ue(const std::string& imsi, const std::string& payload) {
    out_ << "[N1/NAS] ue=" << imsi << " payload=\"" << payload << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N1 send_nas_to_ue imsi=" + imsi + " payload=\"" + payload + "\"");
    }
}

ConsoleN3Adapter::ConsoleN3Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN3Adapter::forward_user_plane(const std::string& imsi, const std::string& payload) {
    out_ << "[N3/GTP-U] ue=" << imsi << " user-plane=\"" << payload << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N3 forward_user_plane imsi=" + imsi + " payload=\"" + payload + "\"");
    }
}

ConsoleN8Adapter::ConsoleN8Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN8Adapter::query_subscription(const std::string& imsi) {
    out_ << "[N8/UDM] query subscription for ue=" << imsi << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N8 query_subscription imsi=" + imsi);
    }
}

ConsoleN11Adapter::ConsoleN11Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN11Adapter::manage_pdu_session(const std::string& imsi, const std::string& operation) {
    out_ << "[N11/SMF] ue=" << imsi << " operation=" << operation << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N11 manage_pdu_session imsi=" + imsi + " operation=" + operation);
    }
}

ConsoleN12Adapter::ConsoleN12Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN12Adapter::authenticate_ue(const std::string& imsi) {
    out_ << "[N12/AUSF] authenticate ue=" << imsi << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N12 authenticate_ue imsi=" + imsi);
    }
}

ConsoleN14Adapter::ConsoleN14Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN14Adapter::transfer_amf_context(const std::string& imsi, const std::string& target_amf) {
    out_ << "[N14/AMF] transfer context ue=" << imsi << " target=" << target_amf << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N14 transfer_amf_context imsi=" + imsi + " target=" + target_amf);
    }
}

ConsoleN15Adapter::ConsoleN15Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN15Adapter::query_policy(const std::string& imsi) {
    out_ << "[N15/PCF] query policy for ue=" << imsi << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N15 query_policy imsi=" + imsi);
    }
}

ConsoleN22Adapter::ConsoleN22Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN22Adapter::select_network_slice(const std::string& imsi, const std::string& snssai) {
    out_ << "[N22/NSSF] select slice ue=" << imsi << " snssai=" << snssai << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N22 select_network_slice imsi=" + imsi + " snssai=" + snssai);
    }
}

ConsoleN26Adapter::ConsoleN26Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN26Adapter::interwork_with_mme(const std::string& imsi, const std::string& operation) {
    out_ << "[N26/MME] interworking ue=" << imsi << " operation=" << operation << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N26 interwork_with_mme imsi=" + imsi + " operation=" + operation);
    }
}

}  // namespace amf
