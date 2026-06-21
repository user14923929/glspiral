cmake_minimum_required(VERSION 3.16)
project(glspiral VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────

find_package(OpenGL REQUIRED)
find_package(glfw3  REQUIRED)
find_package(GLEW   REQUIRED)

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(glspiral src/main.cpp)

target_compile_options(glspiral PRIVATE
    -Wall -Wextra -O2
)

target_link_libraries(glspiral PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
)

# ── Install ──────────────────────────────────────────────────────────────────

include(GNUInstallDirs)
install(TARGETS glspiral RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Info ─────────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "  glspiral ${PROJECT_VERSION}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install to : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
