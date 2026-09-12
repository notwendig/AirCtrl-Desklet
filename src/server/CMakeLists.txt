add_executable(airctrl-server
    main.cpp
    server.cpp
    server.h)
set_target_properties(airctrl-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(airctrl-server PRIVATE
    aioairctrl
    Threads::Threads)
target_include_directories(airctrl-server PRIVATE
    "${PROJECT_BINARY_DIR}/generated")
