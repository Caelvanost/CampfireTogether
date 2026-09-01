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
        src/CellTracker.cpp
        src/LocalBuildIntent.cpp
        src/PapyrusBridge.cpp
        src/Serialization.cpp
        src/SpatialRetry.cpp
        src/STRPMApi.cpp
        src/STRPMClient.cpp
)

# CommonLibSSE-NG 3.5.3 generates a metadata translation unit that uses the
# std::string_view literal suffix ("sv") without importing std::literals.
# MSVC 18 rejects that generated source, so make the generated TU self-contained.
set(CFT_COMMONLIB_PLUGIN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/__${PROJECT_NAME}Plugin.cpp")
if(EXISTS "${CFT_COMMONLIB_PLUGIN_SOURCE}")
    file(READ "${CFT_COMMONLIB_PLUGIN_SOURCE}" CFT_COMMONLIB_PLUGIN_CONTENT)
    file(WRITE "${CFT_COMMONLIB_PLUGIN_SOURCE}"
        "#include <string_view>\n"
        "using namespace std::literals;\n"
        "${CFT_COMMONLIB_PLUGIN_CONTENT}"
    )
endif()

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
