cmake_minimum_required(VERSION 3.25)
project(BoonPanel LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BoonPanel is a Windows x64 Guild Wars 2 DLL. Build it with Windows or GitHub Actions windows-2022.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "BoonPanel must be built as x64.")
endif()

include(FetchContent)

# Nexus' current ImGui ABI is Dear ImGui 1.80 (regular branch).
FetchContent_Declare(
    imgui_180
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.80.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(imgui_180)
if(NOT imgui_180_POPULATED)
    FetchContent_Populate(imgui_180)
endif()

# Fetch the official Nexus API header repository (Nexus.h is at its root).
FetchContent_Declare(
    nexus_api
    GIT_REPOSITORY https://github.com/RaidcoreGG/Nexus-API.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(nexus_api)
if(NOT nexus_api_POPULATED)
    FetchContent_Populate(nexus_api)
endif()

set(NEXUS_INCLUDE_DIR "${nexus_api_SOURCE_DIR}")
if(NOT EXISTS "${NEXUS_INCLUDE_DIR}/Nexus.h")
    message(FATAL_ERROR "Could not locate Nexus.h in the fetched RaidcoreGG/Nexus-API repository.")
endif()

add_library(imgui180 STATIC
    "${imgui_180_SOURCE_DIR}/imgui.cpp"
    "${imgui_180_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_180_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_180_SOURCE_DIR}/imgui_widgets.cpp"
)
target_include_directories(imgui180 PUBLIC "${imgui_180_SOURCE_DIR}")
target_compile_features(imgui180 PUBLIC cxx_std_17)

add_library(BoonPanel SHARED
    src/nexus_addon.cpp
    src/arc_extension.cpp
    src/shared_state.cpp
)
target_compile_features(BoonPanel PRIVATE cxx_std_20)
target_include_directories(BoonPanel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${NEXUS_INCLUDE_DIR}"
    "${imgui_180_SOURCE_DIR}"
)
target_link_libraries(BoonPanel PRIVATE imgui180)
target_compile_definitions(BoonPanel PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set_target_properties(BoonPanel PROPERTIES
    OUTPUT_NAME "BoonPanel"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
)

if(MSVC)
    target_compile_options(BoonPanel PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(imgui180 PRIVATE /W3 /EHsc /utf-8)
endif()

# Installable layout created by both local builds and the CI workflow.
add_custom_command(TARGET BoonPanel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/addons"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/bin64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:BoonPanel>" "${CMAKE_BINARY_DIR}/package/addons/BoonPanel.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:BoonPanel>" "${CMAKE_BINARY_DIR}/package/bin64/arcdps_BoonPanel.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_BINARY_DIR}/package/README.md"
    VERBATIM
)
