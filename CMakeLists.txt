cmake_minimum_required(VERSION 3.15)
project(Orderbook VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(orderbook_lib STATIC Orderbook.cpp)
target_include_directories(orderbook_lib PUBLIC ${CMAKE_SOURCE_DIR})

target_compile_features(orderbook_lib PUBLIC cxx_std_17)

add_executable(OrderbookDemo main.cpp)
target_link_libraries(OrderbookDemo PRIVATE orderbook_lib)

enable_testing()
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_executable(OrderbookTests OrderbookTest/test.cpp)
target_include_directories(OrderbookTests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(OrderbookTests PRIVATE orderbook_lib GTest::gtest_main Threads::Threads)

add_test(NAME OrderbookTests COMMAND OrderbookTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
