cmake_minimum_required(VERSION 3.5)

project(server LANGUAGES CXX)
project(test   LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(SYSTEM "/usr/local/include")

#find_package(Boost COMPONENTS thread REQUIRED)

include_directories( "net" "streaming")

file(GLOB SROURSES . net/*.cpp; streaming/*.cpp)
file(GLOB HEADERS  . net/*.h    streaming/*.h)

add_executable (server server.cpp      ${SROURSES} ${HEADERS})
add_executable (test   stressTest.cpp  ${SROURSES} ${HEADERS})

#target_link_libraries(server ${Boost_LIBRARIES})
#target_link_libraries(test ${Boost_LIBRARIES})
