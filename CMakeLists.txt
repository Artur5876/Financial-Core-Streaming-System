cmake_minimum_required(VERSION 3.20)
project(fincore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)


# CURL
find_package(CURL REQUIRED)

find_package(hiredis CONFIG QUIET)

if(hiredis_FOUND)
    find_package(redis++ CONFIG QUIET)
endif()

if(NOT TARGET redis++::redis++)
    message(STATUS "Using manual redis++ / hiredis paths")

    find_path(REDIS_PLUS_PLUS_INCLUDE_DIR
        NAMES sw/redis++/redis++.h
        PATHS
            /usr/local/include
            /usr/include
    )

    find_library(REDIS_PLUS_PLUS_LIBRARY
        NAMES redis++
        PATHS
            /usr/local/lib
            /usr/lib
            /usr/lib/x86_64-linux-gnu
    )

    find_library(HIREDIS_LIBRARY
        NAMES hiredis
        PATHS
            /usr/local/lib
            /usr/lib
            /usr/lib/x86_64-linux-gnu
    )

    if(NOT REDIS_PLUS_PLUS_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find redis++ headers, expected sw/redis++/redis++.h")
    endif()

    if(NOT REDIS_PLUS_PLUS_LIBRARY)
        message(FATAL_ERROR "Could not find redis++ library, expected libredis++.a or libredis++.so")
    endif()

    if(NOT HIREDIS_LIBRARY)
        message(FATAL_ERROR "Could not find hiredis library, install libhiredis-dev")
    endif()

    add_library(redis++::redis++ INTERFACE IMPORTED)

    target_include_directories(redis++::redis++ INTERFACE
        ${REDIS_PLUS_PLUS_INCLUDE_DIR}
    )

    target_link_libraries(redis++::redis++ INTERFACE
        ${REDIS_PLUS_PLUS_LIBRARY}
        ${HIREDIS_LIBRARY}
    )
endif()

#Libraries
# Shared include path used by all targets
set(FINCORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# order_book — no external deps
add_library(order_book STATIC
    src/core/order_book.cpp
)
target_include_directories(order_book PUBLIC ${FINCORE_INCLUDE_DIR})


# process_metrics — Linux process/resource measurements
add_library(process_metrics STATIC
        src/cli/process_metrics.cpp
)

target_include_directories(process_metrics
        PUBLIC
        ${FINCORE_INCLUDE_DIR}
)

# alpha_vantage_client will need libcurl
add_library(alpha_vantage_client STATIC
    src/api/alpha_vantage_client.cpp
        #tests/unit/process_metrics_test.cpp
)
target_include_directories(alpha_vantage_client PUBLIC ${FINCORE_INCLUDE_DIR})
target_link_libraries(alpha_vantage_client PUBLIC CURL::libcurl)

# redis_client --> needs redis-plus-plus + hiredis
add_library(redis_client STATIC
    src/storage/redis_client.cpp
)
target_include_directories(redis_client PUBLIC ${FINCORE_INCLUDE_DIR})
target_link_libraries(redis_client PUBLIC redis++::redis++)

#fincore_cli processing as application workflow
add_library(fincore_cli STATIC
    src/cli/fincore_cli.cpp
)

target_include_directories(fincore_cli
    PUBLIC
        ${FINCORE_INCLUDE_DIR}
)

target_link_libraries (fincore_cli
    PUBLIC
        order_book
        alpha_vantage_client
        redis_client
        process_metrics
)

# Main executable file
add_executable(fincore_app
        src/core/main.cpp
)

target_link_libraries(fincore_app
        PRIVATE
            fincore_cli
)












#Tests
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)



#test orderbook
add_executable(order_book_test
    tests/unit/order_book_test.cpp
)

target_link_libraries(order_book_test
    PRIVATE
        order_book
        GTest::gtest_main
)

gtest_discover_tests(order_book_test)


#test process_metrics
add_executable(process_metrics_test
        tests/unit/process_metrics_test.cpp
)

target_link_libraries(process_metrics_test
        PRIVATE
        process_metrics
        GTest::gtest_main
)
gtest_discover_tests(process_metrics_test)


#test fincore_cli
add_executable(fincore_cli_test
    tests/unit/fincore_cli_test.cpp)

target_link_libraries(fincore_cli_test
    PRIVATE
        fincore_cli
        GTest::gtest_main
)

gtest_discover_tests(fincore_cli_test)


#test alpha_vantage
add_executable(alpha_vantage_client_test
    tests/unit/alpha_vantage_client_test.cpp
)

target_link_libraries(alpha_vantage_client_test
    PRIVATE
        alpha_vantage_client
        GTest::gtest_main
)

gtest_discover_tests(alpha_vantage_client_test)


#redis tests
add_executable(test_redis_client
    tests/unit/test_redis_client.cpp
)

target_link_libraries(test_redis_client
    PRIVATE
        redis_client
        GTest::gtest_main
)

gtest_discover_tests(test_redis_client)

# Redis-backed integration tests. Tests skip cleanly when the configured Redis
# instance is unavailable. Set REDIS_HOST/REDIS_PORT to override localhost:6379.
add_executable(redis_client_integration_test
    tests/integration/redis_client_integration_test.cpp
)

target_link_libraries(redis_client_integration_test
    PRIVATE
        redis_client
        GTest::gtest_main
)

gtest_discover_tests(redis_client_integration_test
    PROPERTIES LABELS integration
)



## test_redis_client --  pure logic (uses the self-contained stub in the file)
#if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_redis_client.cpp")
#    add_executable(test_redis_client test_redis_client.cpp)
#    target_link_libraries(test_redis_client
#        PRIVATE
#            GTest::gtest_main
#    )
#    gtest_discover_tests(test_redis_client)
#endif()

#Run all tests:         cmake --build build && ctest --test-dir build -V
#Run only order_book:   ctest --test-dir build -R test_order_book -V
