cmake_minimum_required(VERSION 3.16)

project(amf_node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMF_BUILD_TESTS "Build AMF unit tests" ON)

add_library(amf_core
    src/amf.cpp
    src/cli.cpp
    src/app/amf_services.cpp
    src/adapters/console_adapters.cpp
    src/adapters/network_adapters.cpp
    src/config/runtime_config.cpp
    src/logging/file_logger.cpp
    src/modules/registration.cpp
    src/modules/mobility.cpp
    src/modules/session_management.cpp
    src/modules/control_plane.cpp
    src/modules/user_plane.cpp
    src/modules/interworking.cpp
)

target_include_directories(amf_core PUBLIC include)

if(WIN32)
    target_link_libraries(amf_core PUBLIC ws2_32)
endif()

add_executable(amf
    src/main.cpp
)

target_link_libraries(amf PRIVATE amf_core)

if(AMF_BUILD_TESTS)
    enable_testing()

    add_executable(amf_tests
        tests/amf_tests.cpp
    )

    target_link_libraries(amf_tests PRIVATE amf_core)

    add_test(NAME amf_tests COMMAND amf_tests)

    add_executable(amf_cli_tests
        tests/cli_tests.cpp
    )

    target_link_libraries(amf_cli_tests PRIVATE amf_core)

    add_test(NAME amf_cli_tests COMMAND amf_cli_tests)

    add_executable(amf_config_logging_tests
        tests/config_logging_tests.cpp
    )

    target_link_libraries(amf_config_logging_tests PRIVATE amf_core)

    add_test(NAME amf_config_logging_tests COMMAND amf_config_logging_tests)

    add_executable(n1_interop_tests
        tests/n1_interop_tests.cpp
    )

    target_link_libraries(n1_interop_tests PRIVATE amf_core)

    add_test(NAME n1_interop_tests COMMAND n1_interop_tests)

    add_executable(n2_interop_tests
        tests/n2_interop_tests.cpp
    )

    target_link_libraries(n2_interop_tests PRIVATE amf_core)

    add_test(NAME n2_interop_tests COMMAND n2_interop_tests)

    add_executable(n3_interop_tests
        tests/n3_interop_tests.cpp
    )

    target_link_libraries(n3_interop_tests PRIVATE amf_core)

    add_test(NAME n3_interop_tests COMMAND n3_interop_tests)

    add_executable(n26_interop_tests
        tests/n26_interop_tests.cpp
    )

    target_link_libraries(n26_interop_tests PRIVATE amf_core)

    add_test(NAME n26_interop_tests COMMAND n26_interop_tests)

    add_executable(n8_n11_interop_tests
        tests/n8_n11_interop_tests.cpp
    )

    target_link_libraries(n8_n11_interop_tests PRIVATE amf_core)

    add_test(NAME n8_n11_interop_tests COMMAND n8_n11_interop_tests)

    add_executable(n12_n15_interop_tests
        tests/n12_n15_interop_tests.cpp
    )

    target_link_libraries(n12_n15_interop_tests PRIVATE amf_core)

    add_test(NAME n12_n15_interop_tests COMMAND n12_n15_interop_tests)

    add_executable(n14_n22_interop_tests
        tests/n14_n22_interop_tests.cpp
    )

    target_link_libraries(n14_n22_interop_tests PRIVATE amf_core)

    add_test(NAME n14_n22_interop_tests COMMAND n14_n22_interop_tests)
endif()
