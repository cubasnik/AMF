#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <mutex>

#include "amf/interfaces.hpp"
#include "amf/modules/control_plane.hpp"
#include "amf/modules/interworking.hpp"
#include "amf/modules/mobility.hpp"
#include "amf/modules/registration.hpp"
#include "amf/modules/user_plane.hpp"

namespace amf {

class AmfNode final : public IAmfNode {
public:
    AmfNode(IN2Interface& n2, ISbiInterface& sbi, AmfPeerInterfaces peers = {});

    bool start() override;
    bool stop() override;
    bool set_degraded() override;
    bool recover() override;
    void tick() override;

    bool register_ue(const std::string& imsi, const std::string& tai) override;
    bool deregister_ue(const std::string& imsi) override;
    std::optional<UeContext> find_ue(const std::string& imsi) const override;
    std::vector<UeContext> list_ue() const override;

    bool send_n2_nas(const std::string& imsi, const std::string& payload) override;
    bool notify_sbi(const std::string& service_name, const std::string& payload) override;
    bool send_n1_nas(const std::string& imsi, const std::string& payload) override;
    bool forward_n3_user_plane(const std::string& imsi, const std::string& payload) override;
    bool query_n8_subscription(const std::string& imsi, const std::string& request = "get-am-data") override;
    bool manage_n11_pdu_session(const std::string& imsi, const std::string& operation) override;
    bool authenticate_n12(const std::string& imsi, const std::string& request = "auth-request") override;
    bool transfer_n14_context(const std::string& imsi, const std::string& request = "context-transfer") override;
    bool query_n15_policy(const std::string& imsi, const std::string& request = "get-am-policy") override;
    bool select_n22_slice(const std::string& imsi, const std::string& request = "select-slice") override;
    bool interwork_n26(const std::string& imsi, const std::string& operation) override;
    bool set_plmn(const std::string& mcc, const std::string& mnc) override;
    bool set_alarm_thresholds(const AlarmThresholds& thresholds) override;
    std::vector<InterfaceInfo> list_interfaces() const override;
    std::vector<InterfaceDiagnostics> list_interface_diagnostics() const override;
    std::vector<InterfaceErrorEvent> list_interface_errors_last(std::size_t limit) const override;
    std::vector<InterfaceTelemetry> list_interface_telemetry(std::size_t window_seconds = 60) const override;

    AmfStatusSnapshot status() const override;
    void clear_stats() override;

private:
    struct TelemetrySample {
        std::chrono::steady_clock::time_point timestamp;
        double latency_ms {0.0};
        bool success {false};
    };

    bool is_operational() const;
    void init_interface_diagnostics();
    void record_interface_activity(const std::string& iface_name, bool success, const std::string& failure_reason = {}, double latency_ms = 0.0);
    std::string derive_status_reason(const InterfaceDiagnostics& diag, bool operational) const;
    std::string derive_alarm_level(const InterfaceDiagnostics& diag, bool operational) const;

    IN2Interface& n2_;
    ISbiInterface& sbi_;
    AmfPeerInterfaces peers_ {};

    mutable std::mutex mutex_;
    MobilityModule mobility_ {};
    RegistrationModule registration_ {};
    ControlPlaneModule control_plane_;
    UserPlaneModule user_plane_;
    InterworkingModule interworking_;
    AmfStats stats_ {};
    std::map<std::string, InterfaceDiagnostics> interface_diagnostics_ {};
    std::deque<InterfaceErrorEvent> interface_error_history_ {};
    std::map<std::string, std::deque<TelemetrySample>> interface_telemetry_ {};
    static constexpr std::size_t kMaxInterfaceErrorHistory = 256;
    static constexpr std::size_t kMaxTelemetrySamples = 1024;
    AlarmThresholds alarm_thresholds_ {};
};

}  // namespace amf
