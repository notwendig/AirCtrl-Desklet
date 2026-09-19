add_executable(airctrl-server
    main.cpp
    automation.cpp
    automation.h
    server.cpp
    server.h
    statuslog.cpp
    statuslog.h
    ../common/control_values.cpp
    ../common/control_values.hpp)
set_target_properties(airctrl-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(airctrl-server PRIVATE
    aioairctrl
    airctrl_lua
    Threads::Threads)
target_include_directories(airctrl-server PRIVATE
    "${PROJECT_SOURCE_DIR}/src/common"
    "${PROJECT_BINARY_DIR}/generated")
