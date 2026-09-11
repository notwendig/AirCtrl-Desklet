# Lua 5.4.9 is embedded for client-side automation. The server never links it.
add_library(airctrl_client_common STATIC
    controlvalues.cpp controlvalues.hpp
    ipc.cpp ipc.hpp)
target_include_directories(airctrl_client_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(airctrl_client_common PUBLIC
    Qt6::Core
    Qt6::Network)

set(AIRCTRL_LUA_SOURCE "${PROJECT_SOURCE_DIR}/third_party/lua/src")
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

add_library(desklet_ui STATIC
    controller.cpp controller.hpp
    automation.cpp automation.hpp
    alerts.cpp alerts.hpp
    emblems.cpp emblems.hpp
    diagnostics.cpp diagnostics.hpp
    panel.cpp panel.hpp
    desklet.cpp desklet.hpp
    preferences.cpp preferences.hpp)
target_include_directories(desklet_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(desklet_ui PUBLIC
    airctrl_client_common
    airctrl_lua
    Qt6::Widgets
    Qt6::DBus)

add_executable(airctrl-client cli_main.cpp)
set_target_properties(airctrl-client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(airctrl-client PRIVATE airctrl_client_common)

add_executable(airctrl-desklet
    desklet_main.cpp
    ${PROJECT_SOURCE_DIR}/assets/resources.qrc)
set_target_properties(airctrl-desklet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(airctrl-desklet PRIVATE desklet_ui)
