cmake_minimum_required(VERSION 3.5)

project(remote-ui)

# Apply a nanogui patch using git and check it was successful
set(CURRENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NANOGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/nanogui)
set(NANOGUI_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/patches/nanogui.patch)

execute_process(
  COMMAND bash -c "cd ${NANOGUI_DIR} && git apply ${NANOGUI_PATCH} && cd ${CURRENT_DIR}"
  RESULT_VARIABLE GIT_RESULT
  OUTPUT_VARIABLE GIT_OUTPUT
  ERROR_VARIABLE GIT_ERROR
)

if(NOT ${GIT_RESULT} EQUAL 0)
  message(WARNING "Failed to apply nanogui patch: ${GIT_ERROR}")
else()
  message(STATUS "Successfully applied nanogui patch")
endif()

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