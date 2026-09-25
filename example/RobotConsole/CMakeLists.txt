# 医生控制台扶手触摸屏（树莓派 5 / Ubuntu / Windows）。
# 同一工程跨平台：产物统一为 SurgeonConsole（无空格）。
# 树莓派编译：scripts/build_surgeon_console_pi.sh
#
# 依赖（Linux）：
#   sudo apt install build-essential cmake pkg-config \
#     libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
#     fonts-noto-cjk

set(EXAMPLE_NAME RobotConsole)

file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)
file(GLOB_RECURSE EXAMPLE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

if(NOT EXAMPLE_SOURCES)
    message(STATUS "${EXAMPLE_NAME}: 尚无源文件，跳过")
    return()
endif()

tcguicore_add_app(${EXAMPLE_NAME}
    SOURCES ${EXAMPLE_SOURCES}
    HEADERS ${EXAMPLE_HEADERS}
)

# Win / Linux / macOS 统一可执行文件名（无空格），窗口标题仍用 "Surgeon Console"。
set_target_properties(${EXAMPLE_NAME} PROPERTIES
    OUTPUT_NAME "SurgeonConsole"
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN"
)
