# 产品侧静态库：Ads 会话、默认暗色 Shell、符号表。
#
# 目录约定（与产品职责分离）：
#   AdsManager/   多 PLC 连接、符号读写
#   Viewer/       默认窗口、连接栏、主题
#   Common/       类型、日志、平台路径
#   Application   产品只调用 addPlc / addSymbol / run
#
# 有 .cpp 时生成 STATIC；暂时没有源码时退回 INTERFACE，上层链接名不变。

include(GNUInstallDirs)

file(GLOB_RECURSE TCGUICORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)
file(GLOB_RECURSE TCGUICORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

set(TCGUICORE_PUBLIC_LIBS)
if(TARGET tcGUICore_gui)
    list(APPEND TCGUICORE_PUBLIC_LIBS tcGUICore_gui)
endif()
if(TARGET AdsLib)
    list(APPEND TCGUICORE_PUBLIC_LIBS AdsLib)
endif()
if(TCGUICORE_LINK_NDI AND TARGET NDI_CombinedApi)
    list(APPEND TCGUICORE_PUBLIC_LIBS NDI_CombinedApi)
endif()

set(TCGUICORE_PUBLIC_INCLUDES
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/AdsManager>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Viewer>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Common>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore>
)

if(TCGUICORE_SOURCES)
    add_library(tcGUICore STATIC ${TCGUICORE_SOURCES} ${TCGUICORE_HEADERS})
    target_link_libraries(tcGUICore PUBLIC ${TCGUICORE_PUBLIC_LIBS})
    if(WIN32)
        target_link_libraries(tcGUICore PUBLIC d3d11 dxgi)
    endif()
    if(UNIX)
        # std::thread / dlopen。Windows 上 Threads::Threads 为空。
        target_link_libraries(tcGUICore PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
    target_include_directories(tcGUICore PUBLIC ${TCGUICORE_PUBLIC_INCLUDES})
    set_target_properties(tcGUICore PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        FOLDER "tcGUICore"
        EXPORT_NAME tcGUICore
    )

    foreach(_file IN LISTS TCGUICORE_SOURCES TCGUICORE_HEADERS)
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_file}")
        get_filename_component(_group "${_rel}" DIRECTORY)
        string(REPLACE "/" "\\" _group "${_group}")
        source_group("${_group}" FILES "${_file}")
    endforeach()
else()
    add_library(tcGUICore INTERFACE)
    target_link_libraries(tcGUICore INTERFACE ${TCGUICORE_PUBLIC_LIBS})
    target_include_directories(tcGUICore INTERFACE ${TCGUICORE_PUBLIC_INCLUDES})
    set_target_properties(tcGUICore PROPERTIES EXPORT_NAME tcGUICore)
    message(STATUS "tcGUICore: src/ 尚无 .cpp — 使用 INTERFACE 转发出 GUI/ADS")
endif()

add_library(tcGUICore::tcGUICore ALIAS tcGUICore)
