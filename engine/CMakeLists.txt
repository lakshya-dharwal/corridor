cmake_minimum_required(VERSION 3.20)
project(corridor_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(corridor_engine
    src/physiology.cpp
    src/geo.cpp
    src/briefing.cpp
    src/http.cpp
    src/weather.cpp
    src/taf.cpp
    src/wind.cpp
    src/hazards.cpp
    src/websocket.cpp
    src/alerts.cpp
    src/saved_routes.cpp
    src/api.cpp)
target_include_directories(corridor_engine PUBLIC include)
target_compile_options(corridor_engine PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
target_link_libraries(corridor_engine PUBLIC Threads::Threads)

add_executable(corridor_cli apps/corridor_cli.cpp)
target_link_libraries(corridor_cli PRIVATE corridor_engine)

add_executable(corridor_server apps/corridor_server.cpp)
target_link_libraries(corridor_server PRIVATE corridor_engine)

enable_testing()
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE corridor_engine)
add_test(NAME engine COMMAND test_engine)
