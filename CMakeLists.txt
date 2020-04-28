cmake_minimum_required(VERSION 2.6)
project(LAD_POSIX)

add_executable(lad-posix main.c sysv_msg.c)

target_link_libraries(lad-posix rt pthread)

#install(TARGETS say_hello DESTINATION bin)
