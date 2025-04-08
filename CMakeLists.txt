cmake_minimum_required(VERSION 3.5)

project(remote-ui)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

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
find_package(OpenCV REQUIRED)

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

# Configure packetcomms and videolib subprojects:
add_subdirectory(external/packetcomms)
add_subdirectory(external/videolib)

include_directories(${NANOGUI_EXTRA_INCS} ${PACKETCOMMS_INCLUDES} ${VIDEOLIB_INCLUDES} ${OpenCV_INCLUDE_DIRS})

# Configure nanobind module:
find_package(Python 3.10 EXACT COMPONENTS Interpreter Development.Module REQUIRED)

# Detect the installed nanobind package and import it into CMake
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_DIR)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(gui_server python/gui_server.cpp)

# Build the interface app:
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.hpp ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(remote-ui ${SRC})

# Build a server executable for testing:
file(GLOB_RECURSE SERVER_SRC ${PROJECT_SOURCE_DIR}/test_server/*.hpp ${PROJECT_SOURCE_DIR}/test_server/*.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(test-server ${SERVER_SRC})

SET(LIBS
  Boost::program_options Boost::log nanogui
  ${NANOGUI_EXTRA_LIBS} ${OPENGL_LIBRARIES}
  ${PACKETCOMMS_LIBRARIES} ${VIDEOLIB_LIBRARIES}
  ${OpenCV_LIBS}
)

target_link_libraries(remote-ui ${LIBS})
target_link_libraries(test-server ${LIBS})

target_compile_definitions(remote-ui PRIVATE -DBOOST_LOG_DYN_LINK)
