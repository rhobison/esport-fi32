# Helper script: gzip-compress a single file at build time.
# Invoked by add_custom_command in main/CMakeLists.txt.
# Required -D variables: GZIP_EXEC, SRC, DST
execute_process(
    COMMAND "${GZIP_EXEC}" -9 -c "${SRC}"
    OUTPUT_FILE "${DST}"
    RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR "gzip compression failed for: ${SRC}")
endif()
