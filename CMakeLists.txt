cmake_minimum_required(VERSION 3.16)
project(stratum_test_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(stratum_test_client
    main.cpp
    StratumTestClient.cpp
)

target_include_directories(stratum_test_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
