cmake_minimum_required(VERSION 3.20)

project(remote-ui)

find_package(OpenGL REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options log)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# GLFW does not compile without warnings:
add_compile_options(-Wno-sign-compare -Wno-missing-field-initializers -Wno-type-limits)

# Configure nanogui subproject:
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
set(NANOGUI_INSTALL       ON CACHE BOOL " " FORCE)
add_subdirectory(external/nanogui)
set_property(TARGET glfw glfw_objects nanogui PROPERTY FOLDER "dependencies")
add_definitions(${NANOGUI_EXTRA_DEFS})

# Configure packetcomms suboroject:
add_subdirectory(external/packetcomms)
add_subdirectory(external/videolib)

include_directories(${NANOGUI_EXTRA_INCS} ${PACKETCOMMS_INCLUDES} ${VIDEOLIB_INCLUDES})

# Build the interface app:
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.hpp ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(remote-ui ${SRC})
target_link_libraries(
  remote-ui
  Boost::program_options Boost::log nanogui
  ${NANOGUI_EXTRA_LIBS} ${OPENGL_LIBRARIES}
  ${PACKETCOMMS_LIBRARIES} ${VIDEOLIB_LIBRARIES}
)

target_compile_definitions(remote-ui PRIVATE -DBOOST_LOG_DYN_LINK)