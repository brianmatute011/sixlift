cmake_minimum_required(VERSION 3.16)
project(sixlift
        VERSION 1.1.0
        DESCRIPTION "IPv4 over IPv6 (NAT64/DNS64) with a self-healing watchdog"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Platform-independent core.
set(SIXLIFT_SOURCES
  src/main.c
  src/config.c
  src/probe.c
  src/service.c)

# Per-OS backend.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND SIXLIFT_SOURCES src/platform_linux.c src/proc.c src/route.c)
elseif(WIN32)
  list(APPEND SIXLIFT_SOURCES src/platform_windows.c)
else()
  message(FATAL_ERROR
    "Unsupported platform '${CMAKE_SYSTEM_NAME}'. sixlift targets Linux and Windows.")
endif()

add_executable(sixlift ${SIXLIFT_SOURCES})
target_include_directories(sixlift PRIVATE include)

if(MSVC)
  target_compile_options(sixlift PRIVATE /W3)
  target_compile_definitions(sixlift PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(sixlift PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(sixlift PRIVATE _GNU_SOURCE)
elseif(WIN32)
  # Winsock, IP Helper (adapters), and token/SID APIs.
  target_link_libraries(sixlift PRIVATE ws2_32 iphlpapi advapi32)
endif()

include(GNUInstallDirs)
install(TARGETS sixlift RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
