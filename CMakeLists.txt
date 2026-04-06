# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# Enable lwip MIB2 per-interface byte counters for reward AP throughput measurement.
idf_build_set_property(COMPILE_DEFINITIONS "MIB2_STATS=1" APPEND)
set(PROJECT_VER "2.0.0")
project(esport-fi32)
