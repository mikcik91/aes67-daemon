cmake_minimum_required(VERSION 3.16)
project(aes67_daemon VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt GUI (AES67 Discovery Tool) — optional. When Qt6 is not installed the
# daemon + tests still build; install qt6-base-dev to enable the GUI target.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets Network QUIET)

option(BUILD_GUI "Build the Qt6 GUI application (aes67_gui)" ON)
if(BUILD_GUI AND NOT Qt6_FOUND)
    message(STATUS "Qt6 not found - skipping aes67_gui (install qt6-base-dev to enable)")
endif()

# Flags -pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Dependencies
# boost_system has been header-only since Boost 1.69 and modern distros
# (Boost >= 1.90) no longer ship a separate boost_system config package,
# so we only need the Boost headers (Asio/error_code are header-only).
find_package(Boost REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

# Executable
add_executable(aes67_daemon src/main.cpp)

target_include_directories(aes67_daemon PRIVATE src)

# Target linking & include directories
target_link_libraries(aes67_daemon
    PRIVATE
        Boost::headers
        nlohmann_json::nlohmann_json
        Threads::Threads
)

target_compile_options(aes67_daemon PRIVATE -Wall -Wextra -Wpedantic)

# Qt GUI application (built only when Qt6 was found and BUILD_GUI is ON)
if(BUILD_GUI AND Qt6_FOUND)
    add_executable(aes67_gui
        src/gui/main_gui.cpp
        src/gui/MainWindow.cpp
        src/gui/DeviceTableModel.cpp
    )
    target_include_directories(aes67_gui PRIVATE src)
    target_link_libraries(aes67_gui PRIVATE Qt6::Widgets Qt6::Network)
    target_compile_options(aes67_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests (unit tests with GoogleTest, enabled by default)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(unit_tests tests/test_sdp_parser.cpp tests/test_device_registry.cpp)
    target_include_directories(unit_tests PRIVATE src)
    target_link_libraries(unit_tests PRIVATE gtest_main)
    target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME unit_tests COMMAND unit_tests)

    # Functional smoke tests: spawn the daemon, send real SAP packets over
    # multicast, assert on the REST API. Requires python3; skipped gracefully
    # if python3 is missing.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME functional_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/functional/run_functional_tests.py
                    $<TARGET_FILE:aes67_daemon>
        )
        set_tests_properties(functional_tests PROPERTIES TIMEOUT 90)
    endif()
endif()