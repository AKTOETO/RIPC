add_executable(client_test client.cpp)
add_executable(server_test server.cpp)

target_link_libraries(client_test PUBLIC RIPC_lib)
target_link_libraries(server_test PUBLIC RIPC_lib)