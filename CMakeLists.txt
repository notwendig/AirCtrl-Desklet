cmake_minimum_required(VERSION 3.16)
set(AIRCTRL_COMPONENT "" CACHE STRING "Component to build: server or client")
set_property(CACHE AIRCTRL_COMPONENT PROPERTY STRINGS server client)
string(TOLOWER "${AIRCTRL_COMPONENT}" AIRCTRL_COMPONENT)
if(NOT AIRCTRL_COMPONENT STREQUAL "server" AND NOT AIRCTRL_COMPONENT STREQUAL "client")
    message(FATAL_ERROR
        "Set AIRCTRL_COMPONENT to server or client. Prefer a CMake preset: "
        "debug-server, debug-client, release-server or release-client.")
endif()
if(AIRCTRL_COMPONENT STREQUAL "server")
    project(airctrl_desklet VERSION 1.06 LANGUAGES CXX)
else()
    project(airctrl_desklet VERSION 1.06 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CTest)

option(AIRCTRL_TEST_WITH_DBUS "Run tests with a private D-Bus notification service" OFF)

configure_file(src/common/version.hpp.in generated/airctrl_version.hpp @ONLY)

# The Philips protocol is production code only for the server. Client tests use
# it solely for the loopback UDP device simulator. This branch has no Qt lookup.
if(AIRCTRL_COMPONENT STREQUAL "server" OR BUILD_TESTING)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    find_package(Threads REQUIRED)
    find_package(nlohmann_json 3.9 REQUIRED)
    set(AIRCTRL_PROTOCOL_SOURCE "${PROJECT_SOURCE_DIR}/third_party/aioairctrl")
    add_library(aioairctrl STATIC
        ${AIRCTRL_PROTOCOL_SOURCE}/src/encryption.cpp
        ${AIRCTRL_PROTOCOL_SOURCE}/src/coap.cpp
        ${AIRCTRL_PROTOCOL_SOURCE}/src/client.cpp)
    set_target_properties(aioairctrl PROPERTIES AUTOMOC OFF)
    target_include_directories(aioairctrl PUBLIC ${AIRCTRL_PROTOCOL_SOURCE}/include)
    target_link_libraries(aioairctrl PUBLIC nlohmann_json::nlohmann_json
        PRIVATE OpenSSL::Crypto Threads::Threads)
endif()

if(AIRCTRL_COMPONENT STREQUAL "server")
    add_subdirectory(src/server)
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Gui Widgets DBus)
    configure_file(src/client/automation_example.hpp.in
        generated/airctrl_automation_example.hpp @ONLY)
    add_subdirectory(src/client)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target airctrl_client_common aioairctrl desklet_ui airctrl-server airctrl-client airctrl-desklet)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

if(BUILD_TESTING AND AIRCTRL_COMPONENT STREQUAL "client")
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(fake-backend tests/fake_backend.cpp)
    target_link_libraries(fake-backend PRIVATE Qt6::Core Qt6::Network)

    add_executable(diagnostics-preview tests/render_diagnostics.cpp assets/resources.qrc)
    target_link_libraries(diagnostics-preview PRIVATE desklet_ui)

    add_executable(desklet-tests tests/test_desklet.cpp assets/resources.qrc)
    add_executable(automation-tests tests/test_automation.cpp)
    target_link_libraries(desklet-tests PRIVATE desklet_ui aioairctrl Qt6::Test)
    target_link_libraries(automation-tests PRIVATE desklet_ui Qt6::Test)
    target_compile_definitions(automation-tests PRIVATE
        AUTOMATION_EXAMPLE_FILE="${PROJECT_SOURCE_DIR}/examples/automation.lua")
    target_include_directories(desklet-tests PRIVATE ${AIRCTRL_PROTOCOL_SOURCE}/src)

    get_filename_component(AIRCTRL_CONFIGURATION_DIR "${CMAKE_BINARY_DIR}" DIRECTORY)
    set(AIRCTRL_TEST_SERVER_EXECUTABLE
        "${AIRCTRL_CONFIGURATION_DIR}/server/airctrl-server" CACHE FILEPATH
        "airctrl-server executable used by the client integration tests")
    target_compile_definitions(desklet-tests PRIVATE
        FAKE_BACKEND="$<TARGET_FILE:fake-backend>"
        REAL_BACKEND="${AIRCTRL_TEST_SERVER_EXECUTABLE}")
    add_dependencies(desklet-tests fake-backend)

    if(AIRCTRL_TEST_WITH_DBUS)
        find_program(DBUS_RUN_SESSION dbus-run-session)
        if(NOT DBUS_RUN_SESSION)
            message(FATAL_ERROR "AIRCTRL_TEST_WITH_DBUS needs dbus-run-session")
        endif()
        add_test(NAME desklet COMMAND ${DBUS_RUN_SESSION} -- ${CMAKE_COMMAND} -E env
            AIRCTRL_TEST_PRIVATE_DBUS=1 $<TARGET_FILE:desklet-tests> -platform offscreen)
    else()
        add_test(NAME desklet COMMAND desklet-tests -platform offscreen)
    endif()
    add_test(NAME automation COMMAND automation-tests)
    set_tests_properties(desklet PROPERTIES TIMEOUT 300)
    set_tests_properties(automation PROPERTIES TIMEOUT 30)
endif()

if(AIRCTRL_COMPONENT STREQUAL "server")
    install(TARGETS airctrl-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES config/airctrld.cfg DESTINATION ${CMAKE_INSTALL_DATADIR}/airctrl-desklet)
else()
    install(TARGETS airctrl-desklet airctrl-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES assets/airctrl-desklet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/airctrl-desklet.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES third_party/lua/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/airctrl-desklet RENAME LUA-LICENSE)
    install(FILES examples/automation.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/airctrl-desklet/examples)
endif()
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/airctrl-desklet)
