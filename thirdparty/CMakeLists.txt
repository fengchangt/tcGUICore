# 第三方依赖各自独立成 CMake 目标，产品可按 GUI / ADS / NDI 裁剪。

include(FetchContent)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

# GLFW：窗口与输入（ImGui 后端）。查找顺序：
#   1) thirdparty/glfw 或 glfw-* 完整源码（当前为 glfw-3.5.1）
#   2) 预编译 .lib
#   3) find_package(glfw3)
#   4) FetchContent 下载
function(tcguicore_setup_glfw)
    if(TARGET glfw)
        return()
    endif()
    if(TARGET glfw::glfw)
        add_library(glfw ALIAS glfw::glfw)
        return()
    endif()

    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
    if(UNIX AND NOT APPLE)
        # Linux 默认走 X11（含 XWayland）。Wayland 需要额外开发包，不作为默认。
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()

    set(_glfw_root "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/CMakeLists.txt")
        set(_glfw_root "${CMAKE_CURRENT_SOURCE_DIR}/glfw")
    else()
        file(GLOB _glfw_versioned_dirs LIST_DIRECTORIES TRUE
            "${CMAKE_CURRENT_SOURCE_DIR}/glfw-*")
        list(SORT _glfw_versioned_dirs)
        list(REVERSE _glfw_versioned_dirs)
        foreach(_dir IN LISTS _glfw_versioned_dirs)
            if(EXISTS "${_dir}/CMakeLists.txt")
                set(_glfw_root "${_dir}")
                break()
            endif()
        endforeach()
    endif()

    if(_glfw_root)
        add_subdirectory("${_glfw_root}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
        if(NOT TARGET glfw)
            message(FATAL_ERROR "tcGUICore: GLFW subdirectory did not create target glfw (${_glfw_root})")
        endif()
        message(STATUS "tcGUICore: building vendored GLFW source (${_glfw_root})")
        return()
    endif()

    set(_glfw_root "${CMAKE_CURRENT_SOURCE_DIR}/glfw")

    set(_glfw_lib "")
    if(WIN32 AND MSVC)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            foreach(_vc IN ITEMS vc2022 vc2019 vc2017 vc2015 vc2013 vc2012 vc2010)
                if(EXISTS "${_glfw_root}/lib-${_vc}-64/glfw3.lib")
                    set(_glfw_lib "${_glfw_root}/lib-${_vc}-64/glfw3.lib")
                    break()
                endif()
            endforeach()
        else()
            foreach(_vc IN ITEMS vc2022 vc2019 vc2017 vc2015 vc2013 vc2012 vc2010)
                if(EXISTS "${_glfw_root}/lib-${_vc}-32/glfw3.lib")
                    set(_glfw_lib "${_glfw_root}/lib-${_vc}-32/glfw3.lib")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(_glfw_lib AND EXISTS "${_glfw_root}/include/GLFW/glfw3.h")
        add_library(glfw STATIC IMPORTED GLOBAL)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${_glfw_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_glfw_root}/include"
        )
        target_link_libraries(glfw INTERFACE OpenGL::GL)
        if(WIN32)
            target_link_libraries(glfw INTERFACE user32 gdi32 shell32)
        endif()
        message(STATUS "tcGUICore: using prebuilt GLFW (${_glfw_lib})")
        return()
    endif()

    find_package(glfw3 QUIET)
    if(TARGET glfw)
        message(STATUS "tcGUICore: using GLFW from find_package(glfw3)")
        return()
    endif()
    if(TARGET glfw3)
        add_library(glfw ALIAS glfw3)
        message(STATUS "tcGUICore: using GLFW target glfw3")
        return()
    endif()
    if(TARGET glfw::glfw)
        add_library(glfw ALIAS glfw::glfw)
        message(STATUS "tcGUICore: using GLFW target glfw::glfw")
        return()
    endif()

    if(NOT TCGUICORE_USE_FETCHCONTENT_GLFW)
        message(FATAL_ERROR
            "GLFW not found. Place full GLFW source (with CMakeLists.txt) under thirdparty/glfw, "
            "install glfw3, or configure with -DTCGUICORE_USE_FETCHCONTENT_GLFW=ON")
    endif()

    message(STATUS "tcGUICore: fetching GLFW 3.4 (headers-only tree under thirdparty/glfw)")
    FetchContent_Declare(glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    )
    FetchContent_MakeAvailable(glfw)
endfunction()

if(TCGUICORE_ENABLE_GUI)
    find_package(OpenGL REQUIRED)
    tcguicore_setup_glfw()
endif()

# Dear ImGui + ImPlot + ImPlot3D：机器人/PLC 可视化宿主（跨平台 OpenGL3）。
if(TCGUICORE_ENABLE_GUI)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui-1.92.8")
    set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/implot")
    set(IMPLOT3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/implot3d")

    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    if(WIN32)
        list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
    endif()
    if(TCGUICORE_ENABLE_IMGUI_DEMO)
        list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/imgui_demo.cpp)
    endif()

    add_library(tcGUICore_imgui STATIC ${IMGUI_SOURCES})
    add_library(tcGUICore::imgui ALIAS tcGUICore_imgui)
    target_include_directories(tcGUICore_imgui PUBLIC
        $<BUILD_INTERFACE:${IMGUI_DIR}>
        $<BUILD_INTERFACE:${IMGUI_DIR}/backends>
        $<BUILD_INTERFACE:${IMGUI_DIR}/misc/cpp>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/imgui>
    )
    target_compile_definitions(tcGUICore_imgui PUBLIC
        IMGUI_DEFINE_MATH_OPERATORS
        TCGUICORE_HAS_GUI=1
    )
    target_link_libraries(tcGUICore_imgui PUBLIC glfw OpenGL::GL)
    if(WIN32)
        target_link_libraries(tcGUICore_imgui PUBLIC gdi32 d3d11 dxgi)
    endif()
    set_target_properties(tcGUICore_imgui PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "thirdparty"
        EXPORT_NAME imgui
    )

    set(IMPLOT_SOURCES
        ${IMPLOT_DIR}/implot.cpp
        ${IMPLOT_DIR}/implot_items.cpp
    )
    if(TCGUICORE_ENABLE_IMGUI_DEMO)
        list(APPEND IMPLOT_SOURCES ${IMPLOT_DIR}/implot_demo.cpp)
    endif()

    add_library(tcGUICore_implot STATIC ${IMPLOT_SOURCES})
    add_library(tcGUICore::implot ALIAS tcGUICore_implot)
    target_include_directories(tcGUICore_implot PUBLIC
        $<BUILD_INTERFACE:${IMPLOT_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/implot>
    )
    target_link_libraries(tcGUICore_implot PUBLIC tcGUICore_imgui)
    set_target_properties(tcGUICore_implot PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "thirdparty"
        EXPORT_NAME implot
    )

    set(IMPLOT3D_SOURCES
        ${IMPLOT3D_DIR}/implot3d.cpp
        ${IMPLOT3D_DIR}/implot3d_items.cpp
        ${IMPLOT3D_DIR}/implot3d_meshes.cpp
    )
    if(TCGUICORE_ENABLE_IMGUI_DEMO)
        list(APPEND IMPLOT3D_SOURCES ${IMPLOT3D_DIR}/implot3d_demo.cpp)
    endif()

    add_library(tcGUICore_implot3d STATIC ${IMPLOT3D_SOURCES})
    add_library(tcGUICore::implot3d ALIAS tcGUICore_implot3d)
    target_include_directories(tcGUICore_implot3d PUBLIC
        $<BUILD_INTERFACE:${IMPLOT3D_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/implot3d>
    )
    target_link_libraries(tcGUICore_implot3d PUBLIC tcGUICore_implot)
    set_target_properties(tcGUICore_implot3d PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "thirdparty"
        EXPORT_NAME implot3d
    )

    add_library(tcGUICore_gui INTERFACE)
    add_library(tcGUICore::gui ALIAS tcGUICore_gui)
    target_link_libraries(tcGUICore_gui INTERFACE tcGUICore_implot3d)
    set_target_properties(tcGUICore_gui PROPERTIES EXPORT_NAME gui)
endif()

# 倍福 ADS：独立 AdsLib。一台 AdsDevice 对应一台 PLC；AdsAddRoute 可挂多台。
if(TCGUICORE_ENABLE_ADS)
    set(ADS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ADS-113.0.36-1/src/AdsLib")

    add_library(AdsLib STATIC
        ${ADS_LIB_DIR}/AdsDef.cpp
        ${ADS_LIB_DIR}/AdsDevice.cpp
        ${ADS_LIB_DIR}/AdsFile.cpp
        ${ADS_LIB_DIR}/AdsLib.cpp
        ${ADS_LIB_DIR}/ECatAccess.cpp
        ${ADS_LIB_DIR}/Frame.cpp
        ${ADS_LIB_DIR}/LicenseAccess.cpp
        ${ADS_LIB_DIR}/Log.cpp
        ${ADS_LIB_DIR}/MasterDcStatAccess.cpp
        ${ADS_LIB_DIR}/RTimeAccess.cpp
        ${ADS_LIB_DIR}/RegistryAccess.cpp
        ${ADS_LIB_DIR}/RouterAccess.cpp
        ${ADS_LIB_DIR}/Sockets.cpp
        ${ADS_LIB_DIR}/SymbolAccess.cpp
        ${ADS_LIB_DIR}/SysServAccess.cpp
        ${ADS_LIB_DIR}/bhf/ParameterList.cpp
        ${ADS_LIB_DIR}/standalone/AdsLib.cpp
        ${ADS_LIB_DIR}/standalone/AmsConnection.cpp
        ${ADS_LIB_DIR}/standalone/AmsNetId.cpp
        ${ADS_LIB_DIR}/standalone/AmsPort.cpp
        ${ADS_LIB_DIR}/standalone/AmsRouter.cpp
        ${ADS_LIB_DIR}/standalone/NotificationDispatcher.cpp
    )
    add_library(tcGUICore::ads ALIAS AdsLib)
    target_include_directories(AdsLib PUBLIC
        $<BUILD_INTERFACE:${ADS_LIB_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/ads>
    )
    target_compile_definitions(AdsLib PUBLIC
        TCGUICORE_HAS_ADS=1
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        CONFIG_DEFAULT_LOGLEVEL=1
    )
    target_link_libraries(AdsLib PUBLIC Threads::Threads)
    if(WIN32)
        target_link_libraries(AdsLib PUBLIC ws2_32 wsock32)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(AdsLib PUBLIC ${CMAKE_DL_LIBS})
    endif()
    set_target_properties(AdsLib PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "thirdparty"
        EXPORT_NAME ads
    )
    if(MSVC)
        target_compile_options(AdsLib PRIVATE /wd4996 /wd4267 /wd4244)
    endif()

    # Legacy TwinCAT C-API sample (AdsPortOpen + windows.h). Not used for Linux/macOS.
    if(TCGUICORE_ENABLE_TWINCAT_CLIENT AND WIN32)
        set(ADS_WRAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ADS-113.0.36-1")
        if(EXISTS "${ADS_WRAP_DIR}/src/AdsClient.cpp")
            add_library(tcGUICore_ads_twincat STATIC
                ${ADS_WRAP_DIR}/src/AdsClient.cpp
            )
            target_include_directories(tcGUICore_ads_twincat PUBLIC
                $<BUILD_INTERFACE:${ADS_WRAP_DIR}/include>
                $<BUILD_INTERFACE:${ADS_LIB_DIR}>
            )
            target_link_libraries(tcGUICore_ads_twincat PUBLIC AdsLib)
            target_compile_definitions(tcGUICore_ads_twincat PUBLIC TCGUICORE_TWINCAT_CLIENT)
            set_target_properties(tcGUICore_ads_twincat PROPERTIES FOLDER "thirdparty")
            add_library(tcGUICore::ads_twincat ALIAS tcGUICore_ads_twincat)
            message(WARNING
                "tcGUICore: AdsClient uses TwinCAT AdsPortOpen()/windows.h. "
                "Prefer AdsLib (AdsPortOpenEx + AdsAddRoute) for cross-platform multi-PLC access.")
        endif()
    endif()
endif()

# NDI Combined API：仅磁导航研发产品需要；HMI 不要把 NDI 链进核心。
set(NDI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/NDI")
if(TCGUICORE_ENABLE_NDI AND EXISTS "${NDI_ROOT}/library/include/CombinedApi.h")
    set(NDI_PUBLIC_INC "${NDI_ROOT}/library/include")
    set(NDI_PRIVATE_INC "${NDI_ROOT}/library/src/include")

    file(GLOB NDI_SOURCES CONFIGURE_DEPENDS
        "${NDI_ROOT}/library/src/*.cpp"
    )

    add_library(NDI_CombinedApi STATIC ${NDI_SOURCES})
    add_library(tcGUICore::ndi ALIAS NDI_CombinedApi)
    target_include_directories(NDI_CombinedApi
        PUBLIC
            $<BUILD_INTERFACE:${NDI_PUBLIC_INC}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tcGUICore/ndi>
        PRIVATE
            ${NDI_PRIVATE_INC}
    )
    target_compile_definitions(NDI_CombinedApi PUBLIC TCGUICORE_HAS_NDI=1)
    # CombinedApi.h uses dllexport/dllimport. Building static + CAPICOMMON_EXPORTS
    # keeps consumers compiling against the same translation unit ABI.
    if(WIN32)
        target_compile_definitions(NDI_CombinedApi PUBLIC CAPICOMMON_EXPORTS)
        target_link_libraries(NDI_CombinedApi PUBLIC ws2_32)
    endif()
    if(APPLE)
        target_link_libraries(NDI_CombinedApi PUBLIC
            "-framework IOKit"
            "-framework CoreFoundation"
        )
    endif()
    if(UNIX)
        target_link_libraries(NDI_CombinedApi PUBLIC Threads::Threads)
    endif()
    set_target_properties(NDI_CombinedApi PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER "thirdparty"
        OUTPUT_NAME ndicapi
        EXPORT_NAME ndi
    )
    if(MSVC)
        target_compile_options(NDI_CombinedApi PRIVATE /wd4996)
    endif()
elseif(TCGUICORE_ENABLE_NDI)
    message(WARNING "tcGUICore: TCGUICORE_ENABLE_NDI=ON but CombinedApi.h was not found under thirdparty/NDI")
endif()
