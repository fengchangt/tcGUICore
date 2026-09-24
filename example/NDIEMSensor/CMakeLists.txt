# NDI 磁导航上位机：配置 USB 串口和 PLC 的 IP/端口。
# 四通道位姿按 S_EMSensorPose 写入 EMSensor.stAdsInput.sEMSensorPose。

set(EXAMPLE_NAME NDIEMSensor)

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
