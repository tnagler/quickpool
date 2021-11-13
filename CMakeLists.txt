cmake_minimum_required(VERSION 3.0.0)
project(tpool VERSION 0.1.0)

## Project-wide setup
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Threads)


include(CTest)
enable_testing()

add_executable(tpool tests.cpp)
target_link_libraries(tpool ${CMAKE_THREAD_LIBS_INIT})

file(GLOB_RECURSE include_files ${PROJECT_SOURCE_DIR}/include)
install(FILES ${include_files} DESTINATION "${include_install_dir}")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
