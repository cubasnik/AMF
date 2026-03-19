#include <iostream>
#include <string>

#include "amf/amf.hpp"
#include "amf/adapters/console_adapters.hpp"
#include "amf/adapters/network_adapters.hpp"
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

    const bool use_network_adapters = runtime_cfg.network_adapters.mode == "network";

    amf::ConsoleN2Adapter console_n2(std::cout, &logger);
    amf::ConsoleSbiAdapter console_sbi(std::cout, &logger);
    amf::ConsoleN1Adapter console_n1(std::cout, &logger);
    amf::ConsoleN3Adapter console_n3(std::cout, &logger);
    amf::ConsoleN8Adapter console_n8(std::cout, &logger);
    amf::ConsoleN11Adapter console_n11(std::cout, &logger);
    amf::ConsoleN12Adapter console_n12(std::cout, &logger);
    amf::ConsoleN14Adapter console_n14(std::cout, &logger);
    amf::ConsoleN15Adapter console_n15(std::cout, &logger);
    amf::ConsoleN22Adapter console_n22(std::cout, &logger);
    amf::ConsoleN26Adapter console_n26(std::cout, &logger);

    amf::NetworkN2Adapter network_n2(runtime_cfg.network_adapters.n2, std::cout, &logger);
    amf::NetworkSbiAdapter network_sbi(runtime_cfg.network_adapters.sbi, runtime_cfg.network_adapters.sbi_resilience, std::cout, &logger);
    amf::NetworkN1Adapter network_n1(runtime_cfg.network_adapters.n1, std::cout, &logger);
    amf::NetworkN3Adapter network_n3(runtime_cfg.network_adapters.n3, std::cout, &logger);
    amf::NetworkN8Adapter network_n8(runtime_cfg.network_adapters.n8, std::cout, &logger);
    amf::NetworkN11Adapter network_n11(runtime_cfg.network_adapters.n11, std::cout, &logger);
    amf::NetworkN12Adapter network_n12(runtime_cfg.network_adapters.n12, std::cout, &logger);
    amf::NetworkN14Adapter network_n14(runtime_cfg.network_adapters.n14, std::cout, &logger);
    amf::NetworkN15Adapter network_n15(runtime_cfg.network_adapters.n15, std::cout, &logger);
    amf::NetworkN22Adapter network_n22(runtime_cfg.network_adapters.n22, std::cout, &logger);
    amf::NetworkN26Adapter network_n26(runtime_cfg.network_adapters.n26, std::cout, &logger);

    amf::IN2Interface& n2 = use_network_adapters ? static_cast<amf::IN2Interface&>(network_n2) : static_cast<amf::IN2Interface&>(console_n2);
    amf::ISbiInterface& sbi = use_network_adapters ? static_cast<amf::ISbiInterface&>(network_sbi) : static_cast<amf::ISbiInterface&>(console_sbi);
    amf::IN1Interface& n1 = use_network_adapters ? static_cast<amf::IN1Interface&>(network_n1) : static_cast<amf::IN1Interface&>(console_n1);
    amf::IN3Interface& n3 = use_network_adapters ? static_cast<amf::IN3Interface&>(network_n3) : static_cast<amf::IN3Interface&>(console_n3);
    amf::IN8Interface& n8 = use_network_adapters ? static_cast<amf::IN8Interface&>(network_n8) : static_cast<amf::IN8Interface&>(console_n8);
    amf::IN11Interface& n11 = use_network_adapters ? static_cast<amf::IN11Interface&>(network_n11) : static_cast<amf::IN11Interface&>(console_n11);
    amf::IN12Interface& n12 = use_network_adapters ? static_cast<amf::IN12Interface&>(network_n12) : static_cast<amf::IN12Interface&>(console_n12);
    amf::IN14Interface& n14 = use_network_adapters ? static_cast<amf::IN14Interface&>(network_n14) : static_cast<amf::IN14Interface&>(console_n14);
    amf::IN15Interface& n15 = use_network_adapters ? static_cast<amf::IN15Interface&>(network_n15) : static_cast<amf::IN15Interface&>(console_n15);
    amf::IN22Interface& n22 = use_network_adapters ? static_cast<amf::IN22Interface&>(network_n22) : static_cast<amf::IN22Interface&>(console_n22);
    amf::IN26Interface& n26 = use_network_adapters ? static_cast<amf::IN26Interface&>(network_n26) : static_cast<amf::IN26Interface&>(console_n26);

    logger.log(amf::LogLevel::Info, std::string("Adapter mode: ") + runtime_cfg.network_adapters.mode);

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
