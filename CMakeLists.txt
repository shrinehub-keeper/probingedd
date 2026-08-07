# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Bluepad32 (Bluetooth gamepad stack) is vendored as a git submodule under
# third_party/bluepad32. Its ESP-IDF components (bluepad32 + btstack) are
# pulled in as extra component dirs, same as upstream's own examples do.
set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bluepad32)

set(EXTRA_COMPONENT_DIRS ${BLUEPAD32_ROOT}/src/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(probingedd)
