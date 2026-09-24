# 研发样例：只配置 NDI/PLC 变量并调用 tcGUICore 接口，界面由核心库提供。

set(EXAMPLE_NAME PLCHmiDemo)

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

if(NOT TARGET NDI_CombinedApi)
    message(WARNING "${EXAMPLE_NAME}: 缺少 NDI 目标，跳过")
    return()
endif()

tcguicore_add_app(${EXAMPLE_NAME}
    NDI
    SOURCES ${EXAMPLE_SOURCES}
    HEADERS ${EXAMPLE_HEADERS}
)
