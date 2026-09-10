cmake_minimum_required(VERSION 3.16)
project(airctrl_desklet VERSION 1.06 LANGUAGES C CXX)
configure_file(src/version.hpp.in generated/airctrl_version.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/examples/automation.lua" AIRCTRL_AUTOMATION_EXAMPLE)
configure_file(src/automation_example.hpp.in generated/airctrl_automation_example.hpp @ONLY)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(GNUInstallDirs)
include(CTest)
option(AIRCTRL_TEST_WITH_DBUS "Run tests with a private D-Bus notification service" OFF)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets DBus Network)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9 REQUIRED)

# Lua 5.4.9 is embedded from the verified official source release. Only the
# explicitly opened sandbox libraries are exposed to user scripts.
set(AIRCTRL_LUA_SOURCE third_party/lua/src)
set(AIRCTRL_LUA_FILES
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c
    lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lmathlib.c lstrlib.c
    ltablib.c lutf8lib.c)
list(TRANSFORM AIRCTRL_LUA_FILES PREPEND "${AIRCTRL_LUA_SOURCE}/")
add_library(airctrl_lua STATIC ${AIRCTRL_LUA_FILES})
set_target_properties(airctrl_lua PROPERTIES AUTOMOC OFF)
target_include_directories(airctrl_lua PUBLIC ${AIRCTRL_LUA_SOURCE})
target_link_libraries(airctrl_lua PUBLIC ${CMAKE_DL_LIBS} m)

set(AIRCTRL_SOURCE third_party/aioairctrl)
add_library(aioairctrl STATIC ${AIRCTRL_SOURCE}/src/encryption.cpp
    ${AIRCTRL_SOURCE}/src/coap.cpp ${AIRCTRL_SOURCE}/src/client.cpp)
set_target_properties(aioairctrl PROPERTIES AUTOMOC OFF)
target_include_directories(aioairctrl PUBLIC ${AIRCTRL_SOURCE}/include)
target_link_libraries(aioairctrl PUBLIC nlohmann_json::nlohmann_json
    PRIVATE OpenSSL::Crypto Threads::Threads)
# Only airctrl-server links the device protocol in the installed system.
# Every UI/automation/CLI consumer uses the server's TCP endpoint instead.
add_executable(airctrl-server src/server.cpp src/ipc.cpp src/ipc.hpp
    src/controlvalues.cpp src/controlvalues.hpp)
target_include_directories(airctrl-server PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(airctrl-server PRIVATE Qt6::Core Qt6::Network aioairctrl)
add_executable(airctrl-client src/client_main.cpp src/ipc.cpp src/ipc.hpp
    src/controlvalues.cpp src/controlvalues.hpp)
target_include_directories(airctrl-client PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(airctrl-client PRIVATE Qt6::Core Qt6::Network)

add_library(desklet_ui STATIC src/controller.cpp src/controller.hpp src/ipc.cpp src/ipc.hpp
    src/controlvalues.cpp src/controlvalues.hpp
    src/automation.cpp src/automation.hpp
    src/alerts.cpp src/alerts.hpp
    src/emblems.cpp src/emblems.hpp
    src/diagnostics.cpp src/diagnostics.hpp
    src/panel.cpp src/panel.hpp src/desklet.cpp src/desklet.hpp src/preferences.cpp src/preferences.hpp)
target_link_libraries(desklet_ui PUBLIC Qt6::Widgets Qt6::DBus Qt6::Network airctrl_lua)
target_include_directories(desklet_ui PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target desklet_ui airctrl-server airctrl-client)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
add_executable(airctrl-desklet src/main.cpp assets/resources.qrc)
target_link_libraries(airctrl-desklet PRIVATE desklet_ui)

if(BUILD_TESTING)
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
        AUTOMATION_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/examples/automation.lua")
    target_include_directories(desklet-tests PRIVATE ${AIRCTRL_SOURCE}/src)
    target_compile_definitions(desklet-tests PRIVATE
        FAKE_BACKEND="$<TARGET_FILE:fake-backend>"
        REAL_BACKEND="$<TARGET_FILE:airctrl-server>")
    add_dependencies(desklet-tests fake-backend airctrl-server)
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
    # The suite deliberately contains a real 19-second quiet-period test and
    # starts many isolated local server processes. Slower desktop systems need
    # more than CTest's former 90-second limit for the complete suite.
    set_tests_properties(desklet PROPERTIES TIMEOUT 300)
    set_tests_properties(automation PROPERTIES TIMEOUT 30)
endif()

install(TARGETS airctrl-desklet airctrl-server airctrl-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/airctrl-desklet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/airctrl-desklet.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/airctrl-desklet)
install(FILES third_party/lua/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/airctrl-desklet RENAME LUA-LICENSE)
install(FILES examples/automation.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/airctrl-desklet/examples)
install(FILES config/airctrld.cfg DESTINATION ${CMAKE_INSTALL_DATADIR}/airctrl-desklet)
