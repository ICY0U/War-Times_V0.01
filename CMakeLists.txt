cmake_minimum_required(VERSION 3.20)
project(WarTimes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# ---- Dear ImGui ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
set(IMGUI_HEADERS
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/backends/imgui_impl_win32.h
    ${IMGUI_DIR}/backends/imgui_impl_dx11.h
)

# ---- ufbx (single-file FBX loader) ----
set(UFBX_SOURCES ${CMAKE_SOURCE_DIR}/src/ThirdParty/ufbx.c)
set_source_files_properties(${UFBX_SOURCES} PROPERTIES LANGUAGE C)

# Collect source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.h"
)

# Collect shader files (not compiled by C++ — just for IDE visibility)
file(GLOB_RECURSE SHADERS
    "shaders/*.hlsl"
    "shaders/*.hlsli"
)

# Mark shaders as non-compiled
set_source_files_properties(${SHADERS} PROPERTIES
    VS_TOOL_OVERRIDE "None"
    HEADER_FILE_ONLY TRUE
)

# Create executable (WIN32 = no console window, uses WinMain)
add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${SHADERS}
    ${IMGUI_SOURCES}
    ${IMGUI_HEADERS}
    ${UFBX_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link DirectX 11 and Win32 libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dxguid
    winmm
)

# Copy shaders to output directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders to output directory"
)

# Copy models to output directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/models
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
    COMMENT "Copying models to output directory"
)

# Copy textures to output directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/textures
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures
    COMMENT "Copying textures to output directory"
)

# Compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX- /MP)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Debug/Release definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Source groups for IDE (Visual Studio filters)
source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX "Source" FILES ${SOURCES})
source_group(TREE ${CMAKE_SOURCE_DIR}/shaders PREFIX "Shaders" FILES ${SHADERS})
source_group("ThirdParty\\ImGui" FILES ${IMGUI_SOURCES} ${IMGUI_HEADERS})
