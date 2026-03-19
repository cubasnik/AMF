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

bool ConsoleSbiAdapter::notify_service(const std::string& service_name, const std::string& payload) {
    out_ << "[SBI] service=" << service_name << " payload=\"" << payload << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "SBI notify service=" + service_name + " payload=\"" + payload + "\"");
    }

    return true;
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

void ConsoleN8Adapter::query_subscription(const std::string& imsi, const std::string& request) {
    out_ << "[N8/UDM] ue=" << imsi << " request=\"" << request << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N8 query_subscription imsi=" + imsi + " request=\"" + request + "\"");
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

void ConsoleN12Adapter::authenticate_ue(const std::string& imsi, const std::string& request) {
    out_ << "[N12/AUSF] ue=" << imsi << " request=\"" << request << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N12 authenticate_ue imsi=" + imsi + " request=\"" + request + "\"");
    }
}

ConsoleN14Adapter::ConsoleN14Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN14Adapter::transfer_amf_context(const std::string& imsi, const std::string& request) {
    out_ << "[N14/AMF] ue=" << imsi << " request=\"" << request << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N14 transfer_amf_context imsi=" + imsi + " request=\"" + request + "\"");
    }
}

ConsoleN15Adapter::ConsoleN15Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN15Adapter::query_policy(const std::string& imsi, const std::string& request) {
    out_ << "[N15/PCF] ue=" << imsi << " request=\"" << request << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N15 query_policy imsi=" + imsi + " request=\"" + request + "\"");
    }
}

ConsoleN22Adapter::ConsoleN22Adapter(std::ostream& out, FileLogger* logger)
    : out_(out), logger_(logger) {}

void ConsoleN22Adapter::select_network_slice(const std::string& imsi, const std::string& request) {
    out_ << "[N22/NSSF] ue=" << imsi << " request=\"" << request << "\"\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Info, "N22 select_network_slice imsi=" + imsi + " request=\"" + request + "\"");
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
