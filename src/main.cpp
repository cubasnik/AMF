#include <iostream>
#include <string>

#include "amf/amf.hpp"
#include "amf/adapters/console_adapters.hpp"
#include "amf/cli.hpp"
#include "amf/config/runtime_config.hpp"
#include "amf/logging/file_logger.hpp"

int main(int argc, char** argv) {
    amf::RuntimeConfig runtime_cfg {};
    std::string runtime_config_path;
    if (argc > 1) {
        runtime_config_path = argv[1];
        std::string error;
        if (!amf::load_runtime_config_file(runtime_config_path, runtime_cfg, error)) {
            std::cerr << "Config load error: " << error << "\n";
            return 1;
        }
    }

    amf::FileLogger logger(runtime_cfg.log_file);
    logger.log(amf::LogLevel::Info, "AMF process starting");

    amf::ConsoleN2Adapter n2(std::cout, &logger);
    amf::ConsoleSbiAdapter sbi(std::cout, &logger);
    amf::ConsoleN1Adapter n1(std::cout, &logger);
    amf::ConsoleN3Adapter n3(std::cout, &logger);
    amf::ConsoleN8Adapter n8(std::cout, &logger);
    amf::ConsoleN11Adapter n11(std::cout, &logger);
    amf::ConsoleN12Adapter n12(std::cout, &logger);
    amf::ConsoleN14Adapter n14(std::cout, &logger);
    amf::ConsoleN15Adapter n15(std::cout, &logger);
    amf::ConsoleN22Adapter n22(std::cout, &logger);
    amf::ConsoleN26Adapter n26(std::cout, &logger);

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

    if (!node.set_plmn(runtime_cfg.cli.mcc, runtime_cfg.cli.mnc)) {
        std::cerr << "Config apply error: invalid PLMN values in runtime config\n";
        logger.log(amf::LogLevel::Error, "Config apply failed: invalid PLMN");
        return 1;
    }

    if (!node.set_alarm_thresholds(runtime_cfg.alarm_thresholds)) {
        std::cerr << "Config apply error: invalid alarm thresholds in runtime config\n";
        logger.log(amf::LogLevel::Error, "Config apply failed: invalid alarm thresholds");
        return 1;
    }

    logger.log(amf::LogLevel::Info, "Runtime config applied mcc=" + runtime_cfg.cli.mcc + " mnc=" + runtime_cfg.cli.mnc);

    amf::CliShell cli(
        node,
        std::cin,
        std::cout,
        runtime_cfg.cli,
        runtime_cfg.rbac_policy_file,
        runtime_cfg.audit_log_file,
        runtime_config_path);

    cli.run();

    logger.log(amf::LogLevel::Info, "AMF process terminated");

    return 0;
}
