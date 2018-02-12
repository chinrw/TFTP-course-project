cmake_minimum_required(VERSION 3.9)
project(TFTP_course_project)

set(CMAKE_CXX_STANDARD 11)
set(follow-fork-mode child)
add_executable(TFTP_course_project
        main.cpp helpers.h)
