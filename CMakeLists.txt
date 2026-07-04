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
# NAPT hardening for long uptime with several streaming AP clients: enlarge the
# NAT translation table (default 512) and reclaim stale TCP flows faster than the
# 30-minute lwIP default so the table cannot silently fill and drop new
# connections while the AP stays associated.
idf_build_set_property(COMPILE_DEFINITIONS "IP_NAPT_MAX=1024" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "IP_NAPT_TIMEOUT_MS_TCP=600000" APPEND)
set(PROJECT_VER "2.2.5")
project(esport-fi32)
