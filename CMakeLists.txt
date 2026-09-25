cmake_minimum_required(VERSION 3.16)

# =============================================================================
# tcGUICore — 通用 GUI + 倍福 ADS 核心库
#
# 多套软件共用这一份源码，不要在各产品里复制 ImGui / AdsLib / NDI：
#   研发工具：NDI 磁导航 → 可视化 → Ads 写入倍福 PLC
#   触摸屏 HMI：读/写一台或多台 PLC 的大量变量
#
# 产品工程推荐用法：
#   add_subdirectory(path/to/tcGUICore)
#   add_executable(MyApp ...)
#   target_link_libraries(MyApp PRIVATE tcGUICore::tcGUICore)   # 默认窗口 + ADS
#   target_link_libraries(MyApp PRIVATE tcGUICore::ndi)         # 仅研发需要时再链
#
# CMake 目标：
#   tcGUICore::tcGUICore   对外核心（src/AdsManager、Viewer、Application）
#   tcGUICore::gui         ImGui + ImPlot + ImPlot3D + GLFW/OpenGL
#   tcGUICore::ads         独立 AdsLib（TCP/AMS，不依赖 TwinCAT 路由）
#   tcGUICore::ndi         NDI Combined API（可选）
# =============================================================================

# 版本号来自仓库根目录 project 文件（MAJOR/MINOR/PATCH），避免和源码各写一份。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/project" TCGUICORE_PROJECT_FILE)
string(REGEX REPLACE ".*MAJOR=([0-9]+).*" "\\1" TCGUICORE_VERSION_MAJOR "${TCGUICORE_PROJECT_FILE}")
string(REGEX REPLACE ".*MINOR=([0-9]+).*" "\\1" TCGUICORE_VERSION_MINOR "${TCGUICORE_PROJECT_FILE}")
string(REGEX REPLACE ".*PATCH=([0-9]+).*" "\\1" TCGUICORE_VERSION_PATCH "${TCGUICORE_PROJECT_FILE}")
string(STRIP "${TCGUICORE_VERSION_MAJOR}" TCGUICORE_VERSION_MAJOR)
string(STRIP "${TCGUICORE_VERSION_MINOR}" TCGUICORE_VERSION_MINOR)
string(STRIP "${TCGUICORE_VERSION_PATCH}" TCGUICORE_VERSION_PATCH)

project(tcGUICore
    VERSION ${TCGUICORE_VERSION_MAJOR}.${TCGUICORE_VERSION_MINOR}.${TCGUICORE_VERSION_PATCH}
    DESCRIPTION "Curve Robotics GUI Core Framework"
    LANGUAGES C CXX
)

# 作为子目录被其它软件引入时：不要改对方的编译器标准/输出路径。
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(TCGUICORE_IS_TOP_LEVEL ON)
else()
    set(TCGUICORE_IS_TOP_LEVEL OFF)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 功能开关：产品 CMake 里 set(... ON/OFF) 即可裁剪，缓存项可被上层覆盖。
option(TCGUICORE_ENABLE_GUI            "编译 ImGui/ImPlot/ImPlot3D 可视化（GLFW+OpenGL）" ON)
option(TCGUICORE_ENABLE_ADS            "编译独立 AdsLib（跨平台 TCP/AMS，多 PLC）" ON)
option(TCGUICORE_ENABLE_NDI            "编译 NDI Combined API（磁导航研发）" ON)
option(TCGUICORE_LINK_NDI              "把 NDI 链进核心库；默认关闭，由产品自行 link tcGUICore::ndi" OFF)
option(TCGUICORE_ENABLE_IMGUI_DEMO     "编入 imgui/implot 官方 Demo 源文件" OFF)
option(TCGUICORE_ENABLE_TWINCAT_CLIENT "仅 Windows：旧版 TwinCAT AdsClient（不跨平台）" OFF)
option(TCGUICORE_USE_FETCHCONTENT_GLFW "找不到本地 GLFW 源码时再下载" ON)
option(TCGUICORE_BUILD_EXAMPLES        "编译 example 下的产品样例" ${TCGUICORE_IS_TOP_LEVEL})
option(TCGUICORE_INSTALL               "生成 install / find_package 导出（日常开发保持关闭）" OFF)

cmake_policy(SET CMP0077 NEW)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 顶层构建时产物放到 bin/<系统>-<架构>、lib/<系统>-<架构>，方便拷到工控机。
# -----------------------------------------------------------------------------
# 平台三元组：win-x64 / lin-x64 / mac-arm64 …
if(WIN32)
    set(TCGUICORE_OS win)
elseif(APPLE)
    set(TCGUICORE_OS mac)
elseif(UNIX)
    set(TCGUICORE_OS lin)
else()
    set(TCGUICORE_OS ${CMAKE_SYSTEM_NAME})
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(TCGUICORE_ARCH arm64)
    else()
        set(TCGUICORE_ARCH x64)
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|armv6|arm")
        set(TCGUICORE_ARCH arm)
    else()
        set(TCGUICORE_ARCH x86)
    endif()
endif()
set(TCGUICORE_PLATFORM "${TCGUICORE_OS}-${TCGUICORE_ARCH}")

if(TCGUICORE_IS_TOP_LEVEL)
    # Debug/Release 不能共用同一个库文件，否则 MSVC CRT（MD/MDd）会冲突。
    # 单配置生成器（Ninja / Unix Makefiles）不看 *_DEBUG 变量，输出目录要自己带上配置名。
    if(NOT CMAKE_CONFIGURATION_TYPES)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" _single_cfg)
        if(_single_cfg STREQUAL "")
            set(_single_cfg "release")
        endif()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/${TCGUICORE_PLATFORM}/${_single_cfg}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}/${_single_cfg}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}/${_single_cfg}")
    endif()
    foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(TOLOWER "${_cfg}" _cfg_dir)
        if(_cfg STREQUAL "RELWITHDEBINFO")
            set(_cfg_dir "relwithdebinfo")
        elseif(_cfg STREQUAL "MINSIZEREL")
            set(_cfg_dir "minsizerel")
        elseif(_cfg STREQUAL "DEBUG")
            set(_cfg_dir "debug")
        else()
            set(_cfg_dir "release")
        endif()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${PROJECT_SOURCE_DIR}/bin/${TCGUICORE_PLATFORM}/${_cfg_dir}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}/${_cfg_dir}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}/${_cfg_dir}")
    endforeach()
    if(CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/${TCGUICORE_PLATFORM}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib/${TCGUICORE_PLATFORM}")
    endif()
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# 统一各产品编译选项：MSVC 开 UTF-8（源码中文）、多核、异常。
# -----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/MP /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(WIN64)
    else()
        add_compile_definitions(WIN32)
    endif()
elseif(MINGW)
    add_compile_definitions(WIN32 HAVE_GCC_DESTRUCTOR)
    add_compile_options(-Wno-deprecated)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(LINUX)
    add_compile_options(-fPIC -Wno-deprecated)
elseif(APPLE)
    add_compile_definitions(MACOSX)
    add_compile_options(-Qunused-arguments -Wno-deprecated)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(tcGUICoreAddApp)

add_subdirectory(thirdparty)
add_subdirectory(src)

# 预编译 GLFW 无法放进 CMake export 集；日常请用 add_subdirectory 引用本库。
if(TCGUICORE_INSTALL AND TARGET glfw)
    get_target_property(_tcguicore_glfw_imported glfw IMPORTED)
    if(_tcguicore_glfw_imported)
        message(WARNING
            "tcGUICore: GLFW is IMPORTED; install/export is disabled. "
            "Use add_subdirectory(tcGUICore) from product software.")
        set(TCGUICORE_INSTALL OFF)
    endif()
endif()

if(TCGUICORE_BUILD_EXAMPLES AND EXISTS "${PROJECT_SOURCE_DIR}/example/CMakeLists.txt")
    add_subdirectory(example)
endif()

# 可选：安装后 find_package(tcGUICore)。多产品维护仍建议 add_subdirectory。
if(TCGUICORE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(_tcguicore_export_targets tcGUICore)
    if(TARGET tcGUICore_imgui)
        list(APPEND _tcguicore_export_targets
            tcGUICore_imgui tcGUICore_implot tcGUICore_implot3d tcGUICore_gui)
    endif()
    if(TARGET AdsLib)
        list(APPEND _tcguicore_export_targets AdsLib)
    endif()
    if(TARGET NDI_CombinedApi)
        list(APPEND _tcguicore_export_targets NDI_CombinedApi)
    endif()

    install(TARGETS ${_tcguicore_export_targets}
        EXPORT tcGUICoreTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY src/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )

    if(TARGET tcGUICore_imgui)
        install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/thirdparty/imgui-1.92.8/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/imgui
            FILES_MATCHING PATTERN "*.h"
        )
        install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/thirdparty/implot/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/implot
            FILES_MATCHING PATTERN "*.h"
        )
        install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/thirdparty/implot3d/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/implot3d
            FILES_MATCHING PATTERN "*.h"
        )
    endif()
    if(TARGET AdsLib)
        install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/thirdparty/ADS-113.0.36-1/src/AdsLib/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/ads
            FILES_MATCHING PATTERN "*.h"
        )
    endif()
    if(TARGET NDI_CombinedApi)
        install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/thirdparty/NDI/library/include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/ndi
            FILES_MATCHING PATTERN "*.h"
        )
    endif()

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/tcGUICoreConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/tcGUICoreConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcGUICore
    )
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/tcGUICoreConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(EXPORT tcGUICoreTargets
        FILE tcGUICoreTargets.cmake
        NAMESPACE tcGUICore::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcGUICore
    )
    install(FILES
        "${PROJECT_BINARY_DIR}/tcGUICoreConfig.cmake"
        "${PROJECT_BINARY_DIR}/tcGUICoreConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcGUICore
    )

    export(EXPORT tcGUICoreTargets
        FILE "${PROJECT_BINARY_DIR}/tcGUICoreTargets.cmake"
        NAMESPACE tcGUICore::
    )
endif()

message(STATUS "========== tcGUICore ${PROJECT_VERSION} ==========")
message(STATUS "  platform         : ${TCGUICORE_PLATFORM}")
message(STATUS "  top-level        : ${TCGUICORE_IS_TOP_LEVEL}")
if(WIN32)
    message(STATUS "  renderer         : DirectX 11")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "  renderer         : OpenGL (GLFW/X11)")
    message(STATUS "  linux packages   : build-essential cmake libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev")
else()
    message(STATUS "  renderer         : OpenGL")
endif()
message(STATUS "  GUI / ImPlot     : ${TCGUICORE_ENABLE_GUI}")
message(STATUS "  AdsLib           : ${TCGUICORE_ENABLE_ADS}")
message(STATUS "  NDI target       : ${TCGUICORE_ENABLE_NDI}")
message(STATUS "  link NDI in core : ${TCGUICORE_LINK_NDI}")
message(STATUS "  examples         : ${TCGUICORE_BUILD_EXAMPLES}")
message(STATUS "  install/export   : ${TCGUICORE_INSTALL}")
message(STATUS "==============================================")
