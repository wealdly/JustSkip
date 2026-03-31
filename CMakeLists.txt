cmake_minimum_required(VERSION 3.20)
project(JustSkip LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch MinHook (official repo, has CMake since v1.3.4)
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
)
FetchContent_MakeAvailable(minhook)

# Build the DLL
add_library(JustSkip SHARED source/JustSkip.cpp)
target_link_libraries(JustSkip PRIVATE minhook)

# Output as .asi instead of .dll
set_target_properties(JustSkip PROPERTIES
    SUFFIX ".asi"
    OUTPUT_NAME "JustSkip"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy INI next to output
add_custom_command(TARGET JustSkip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/JustSkip.ini"
        "$<TARGET_FILE_DIR:JustSkip>/JustSkip.ini"
    COMMENT "Copying JustSkip.ini to output"
)
