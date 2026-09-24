# 所有产品可执行文件用同一套链接方式，避免各工程复制一遍依赖。
#
#   tcguicore_add_app(MyHmi SOURCES main.cpp ...)
#   tcguicore_add_app(NdiTool NDI SOURCES main.cpp ...)

function(tcguicore_add_app name)
    cmake_parse_arguments(APP "NDI" "" "SOURCES;HEADERS" ${ARGN})

    if(NOT APP_SOURCES)
        message(FATAL_ERROR "tcguicore_add_app(${name}): 必须提供 SOURCES")
    endif()

    add_executable(${name} ${APP_SOURCES} ${APP_HEADERS})
    target_link_libraries(${name} PRIVATE tcGUICore::tcGUICore)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "examples"
    )

    if(APP_NDI)
        if(NOT TARGET NDI_CombinedApi)
            message(FATAL_ERROR
                "tcguicore_add_app(${name}): 需要 NDI，但未生成 NDI_CombinedApi。"
                "请打开 TCGUICORE_ENABLE_NDI 并保留 thirdparty/NDI。")
        endif()
        target_link_libraries(${name} PRIVATE tcGUICore::ndi)
        target_compile_definitions(${name} PRIVATE TCGUICORE_APP_HAS_NDI=1)
    endif()

    # JSON 与 PLC 共用（plc_symbols.json），运行时复制到可执行文件目录。
    foreach(_cfg IN ITEMS plc_symbols.json plc_symbols plc_config.json)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_cfg}")
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_cfg}"
                    "$<TARGET_FILE_DIR:${name}>/plc_symbols.json"
                COMMENT "复制 ${name} 的 ${_cfg} 为 plc_symbols.json"
            )
            break()
        endif()
    endforeach()
endfunction()
