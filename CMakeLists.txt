cmake_minimum_required(VERSION 3.25)
project(AntSQL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(antsql_router
  engine/src/router.cpp
  engine/src/gateway.cpp
  engine/src/in_process_forwarder.cpp
  engine/src/in_memory_topology.cpp
  engine/src/sql.cpp)
target_include_directories(antsql_router PUBLIC engine/include)

add_executable(antsql_router_tests
  engine/tests/router_test.cpp
  engine/tests/gateway_test.cpp
  engine/tests/in_process_forwarder_test.cpp
  engine/tests/sql_test.cpp)
target_link_libraries(antsql_router_tests PRIVATE antsql_router)

enable_testing()
add_test(NAME antsql_router_tests COMMAND antsql_router_tests)
