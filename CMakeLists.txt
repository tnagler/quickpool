cmake_minimum_required(VERSION 3.0.0)

project(quickpool VERSION 1.5.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

option(QUICKPOOL_TEST          "Build test" ON)
option(QUICKPOOL_TEST_SANITIZE "Build test with sanitizer" OFF)

find_library(LIB_ATOMIC
  NAMES
    "atomic"
    "libatomic.so"
    "libatomic.so.1"
)

find_package(Threads)

add_library(quickpool
  INTERFACE
    "quickpool.hpp"
)

target_include_directories(quickpool
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(quickpool
  INTERFACE
    $<$<BOOL:${LIB_ATOMIC}>:${LIB_ATOMIC}>
    $<TARGET_NAME_IF_EXISTS:Threads::Threads>
)

if(QUICKPOOL_TEST)
  add_executable(test "test.cpp")

  target_link_libraries(test
    PRIVATE
      quickpool
  )

  if(QUICKPOOL_TEST_SANITIZE)
    target_compile_options(test
      PRIVATE
        "-fno-omit-frame-pointer"
        "-fsanitize=address,undefined,leak,bounds,alignment"
    )

    target_link_options(test
      PRIVATE
        "-fno-omit-frame-pointer"
        "-fsanitize=address,undefined,leak,bounds,alignment"
    )
  endif()
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
