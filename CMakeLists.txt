cmake_minimum_required(VERSION 3.0.0)

project(quickpool VERSION 1.6.0)

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

add_library(quickpool INTERFACE)

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Project built standalone
  set(SYSTEM "")
else()
  set(SYSTEM "SYSTEM")
endif()

target_include_directories(quickpool
  ${SYSTEM} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(quickpool
  INTERFACE
    $<$<BOOL:${LIB_ATOMIC}>:${LIB_ATOMIC}>
    $<TARGET_NAME_IF_EXISTS:Threads::Threads>
)

if(QUICKPOOL_TEST)
  include(CTest)

  add_executable(quickpool_test "test.cpp")

  add_test(
    NAME quickpool_test
    COMMAND "quickpool_test"
  )

  target_link_libraries(quickpool_test
    PRIVATE
      quickpool
  )

  if(QUICKPOOL_TEST_SANITIZE)
    target_compile_options(quickpool_test
      PRIVATE
        "-fno-omit-frame-pointer"
        "-fsanitize=address,undefined,leak,bounds,alignment"
    )

    target_link_options(quickpool_test
      PRIVATE
        "-fno-omit-frame-pointer"
        "-fsanitize=address,undefined,leak,bounds,alignment"
    )
  endif()
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
