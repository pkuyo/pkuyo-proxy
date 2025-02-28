cmake_minimum_required(VERSION 3.30)
project(proxy)
find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 20)

set(JANSSON_DIR "${CMAKE_SOURCE_DIR}/external/jansson")
set(SPDLOG_DIR "${CMAKE_SOURCE_DIR}/external/spdlog")


include_directories(${JANSSON_DIR}/src)
include_directories(include)


add_executable(proxy src/main.cpp
        src/config_loader.cpp
        src/init_listener.cpp
        src/proxy_handler.cpp
        src/worker.cpp
        src/master.cpp
        src/logger.cpp
        src/def.cpp
        src/process.cpp
        src/conn_handler.cpp
        src/listen_handler.cpp)



set(JANSSON_BUILD_DOCS OFF CACHE BOOL "Disable building Jansson documentation")
add_subdirectory(${JANSSON_DIR})
add_subdirectory(${SPDLOG_DIR})

target_link_libraries(proxy PRIVATE ${JANSSON_LIBRARIES})
target_link_libraries(proxy PRIVATE spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
target_link_libraries(proxy PRIVATE OpenSSL::SSL OpenSSL::Crypto)