cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Community firmware replaces the bootloader when installed over USB. Keep the
# factory Recovery entry gesture in every derivative build so USB development
# does not strand the device outside the mini-program BLE install path.
idf_build_set_property(
    BOOTLOADER_EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/bootloader_components/"
    APPEND)

project(FoloToy-AI-Passport)

# Only the immutable journal header is an install payload. Runtime log sectors
# start at 0x311000 and must survive subsequent segmented USB updates.
set(diag_seed "${CMAKE_BINARY_DIR}/diag_log_seed.bin")
add_custom_command(OUTPUT "${diag_seed}"
    COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/device_diagnostics.py" seed "${diag_seed}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/device_diagnostics.py"
    VERBATIM)
add_custom_target(diag_log_seed ALL DEPENDS "${diag_seed}")
esptool_py_flash_to_partition(flash "diag_log" "${diag_seed}")
add_dependencies(flash diag_log_seed)
