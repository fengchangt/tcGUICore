# 主从机器人控制台。Ubuntu 虚拟机与树莓派都走 GLFW + OpenGL，不链接 NDI。
# 依赖：build-essential cmake pkg-config
#   libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
# 中文：fonts-noto-cjk 或 fonts-wqy-microhei

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
