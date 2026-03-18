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
endif()
