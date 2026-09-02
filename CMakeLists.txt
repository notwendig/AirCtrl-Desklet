cmake_minimum_required(VERSION 3.16)
project(airctrl_desklet VERSION 0.3.6 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(GNUInstallDirs)
include(CTest)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9 REQUIRED)

set(AIRCTRL_SOURCE third_party/aioairctrl)
add_library(aioairctrl STATIC ${AIRCTRL_SOURCE}/src/encryption.cpp
    ${AIRCTRL_SOURCE}/src/coap.cpp ${AIRCTRL_SOURCE}/src/client.cpp)
set_target_properties(aioairctrl PROPERTIES AUTOMOC OFF)
target_include_directories(aioairctrl PUBLIC ${AIRCTRL_SOURCE}/include)
target_link_libraries(aioairctrl PUBLIC nlohmann_json::nlohmann_json
    PRIVATE OpenSSL::Crypto Threads::Threads)
add_executable(airctrl-backend ${AIRCTRL_SOURCE}/src/main.cpp)
set_target_properties(airctrl-backend PROPERTIES AUTOMOC OFF)
target_link_libraries(airctrl-backend PRIVATE aioairctrl)

add_library(desklet_ui STATIC src/controller.cpp src/controller.hpp
    src/emblems.cpp src/emblems.hpp
    src/diagnostics.cpp src/diagnostics.hpp
    src/panel.cpp src/panel.hpp src/desklet.cpp src/desklet.hpp src/preferences.cpp src/preferences.hpp)
target_link_libraries(desklet_ui PUBLIC Qt6::Widgets)
target_include_directories(desklet_ui PUBLIC src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desklet_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(airctrl-desklet src/main.cpp assets/resources.qrc)
target_link_libraries(airctrl-desklet PRIVATE desklet_ui)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(fake-backend tests/fake_backend.cpp)
    target_link_libraries(fake-backend PRIVATE Qt6::Core)
    add_executable(desklet-tests tests/test_desklet.cpp assets/resources.qrc)
    add_executable(parent-probe tests/parent_probe.cpp)
    target_link_libraries(parent-probe PRIVATE desklet_ui)
    target_link_libraries(desklet-tests PRIVATE desklet_ui aioairctrl Qt6::Test)
    target_include_directories(desklet-tests PRIVATE ${AIRCTRL_SOURCE}/src)
    target_compile_definitions(desklet-tests PRIVATE
        FAKE_BACKEND="$<TARGET_FILE:fake-backend>"
        REAL_BACKEND="$<TARGET_FILE:airctrl-backend>"
        PARENT_PROBE="$<TARGET_FILE:parent-probe>")
    add_dependencies(desklet-tests fake-backend airctrl-backend parent-probe)
    add_test(NAME desklet COMMAND desklet-tests -platform offscreen)
    set_tests_properties(desklet PROPERTIES TIMEOUT 90)
endif()

install(TARGETS airctrl-desklet airctrl-backend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/airctrl-desklet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/airctrl-desklet.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/airctrl-desklet)
