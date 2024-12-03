cmake_minimum_required(VERSION 3.10)
project(vosk)

set(CMAKE_CXX_STANDARD 11)

include_directories(vosk)
link_directories(vosk)

add_executable(vosk main.c)

if(WIN32)
    target_link_libraries(vosk ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libvosk.lib)
    
    file(COPY 
        ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libvosk.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libgcc_s_seh-1.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libstdc++-6.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libwinpthread-1.dll
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
else()
    target_link_libraries(vosk ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libvosk.so)

    file(COPY 
    ${CMAKE_CURRENT_SOURCE_DIR}/vosk/libvosk.so
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()
