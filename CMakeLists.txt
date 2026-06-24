cmake_minimum_required(VERSION 3.16)
project(sixlift
        VERSION 1.0.0
        DESCRIPTION "IPv4 over IPv6 (NAT64/DNS64) with a self-healing watchdog"
        LANGUAGES C)

# sixlift speaks rtnetlink and drives systemd/NetworkManager: it is Linux-only.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "sixlift targets Linux (netlink, systemd, NetworkManager).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(sixlift
  src/main.c
  src/config.c
  src/proc.c
  src/probe.c
  src/route.c
  src/service.c)

target_include_directories(sixlift PRIVATE include)
target_compile_definitions(sixlift PRIVATE _GNU_SOURCE)
target_compile_options(sixlift PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS sixlift RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
