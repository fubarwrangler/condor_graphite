CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(HTCGR)
ADD_DEFINITIONS(-Wall -pedantic -Wextra)
ADD_DEFINITIONS(-std=c99 -D_POSIX_C_SOURCE=200809L)
ADD_EXECUTABLE(htcgread cgroup.c)
FIND_LIBRARY(
    LIBCGROUP NAMES libcgroup cgroup
    PATHS /usr/local/ /usr/
)
TARGET_LINK_LIBRARIES( htcgread "${LIBCGROUP}" )