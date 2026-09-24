# 每个子目录是一套产品样例，全部链接同一份 tcGUICore。
# 新增软件：新建文件夹 + CMakeLists.txt，不要分叉核心库。

file(GLOB _example_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*")
foreach(_dir IN LISTS _example_dirs)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
        add_subdirectory(${_dir})
    endif()
endforeach()
