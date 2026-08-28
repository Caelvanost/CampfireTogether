cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CFT_VERSION)
string(STRIP "${CFT_VERSION}" CFT_VERSION)

project(
    CampfireTogether
    VERSION ${CFT_VERSION}
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES
        src/main.cpp
        src/CampfireSync.cpp
        src/PapyrusBridge.cpp
        src/STRPMApi.cpp
        src/STRPMClient.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        src/PCH.h
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        _UNICODE
        UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        CAMPFIRE_TOGETHER_VERSION="${CFT_VERSION}"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /W4 /utf-8)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "CampfireTogether"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/package/SKSE/Plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_BINARY_DIR}/package/SKSE/Plugins/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
)
