cmake_minimum_required(VERSION 2.6)

project(TLSMeasurement)
set(TARGET_MAIN "tls")

file(GLOB_RECURSE SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")


find_package(Boost 1.55 REQUIRED COMPONENTS system thread)
find_package(Threads REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

set(DEPRS
        ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++11")

add_executable (${TARGET_MAIN} ${SOURCES})
target_link_libraries(${TARGET_MAIN} ${DEPRS})
