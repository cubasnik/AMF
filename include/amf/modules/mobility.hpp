#pragma once

#include "amf/interfaces.hpp"

namespace amf {

class MobilityModule {
public:
    bool start();
    bool stop();
    bool set_degraded();
    bool recover();
    bool tick();

    bool is_operational() const;
    AmfState state() const;

private:
    AmfState state_ {AmfState::Idle};
};

}  // namespace amf
