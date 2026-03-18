#include "amf/modules/mobility.hpp"

namespace amf {

bool MobilityModule::start() {
    if (state_ == AmfState::Running || state_ == AmfState::Degraded || state_ == AmfState::Initializing) {
        return false;
    }

    state_ = AmfState::Initializing;
    state_ = AmfState::Running;
    return true;
}

bool MobilityModule::stop() {
    if (state_ == AmfState::Stopped || state_ == AmfState::Idle) {
        return false;
    }

    state_ = AmfState::Stopped;
    return true;
}

bool MobilityModule::set_degraded() {
    if (state_ != AmfState::Running) {
        return false;
    }

    state_ = AmfState::Degraded;
    return true;
}

bool MobilityModule::recover() {
    if (state_ != AmfState::Degraded) {
        return false;
    }

    state_ = AmfState::Running;
    return true;
}

bool MobilityModule::tick() {
    return state_ == AmfState::Running || state_ == AmfState::Degraded;
}

bool MobilityModule::is_operational() const {
    return state_ == AmfState::Running || state_ == AmfState::Degraded;
}

AmfState MobilityModule::state() const {
    return state_;
}

}  // namespace amf
