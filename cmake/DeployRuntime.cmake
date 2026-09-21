# Keep every executable runnable from its own build directory (including CLion).
# Imported targets resolve to the actual DLLs, even for release-only Qt SDKs.
function(rock_deploy_runtime target uses_qt)
    if(NOT WIN32)
        return()
    endif()
    if(MINGW)
        get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(compiler_dlls)
        foreach(name libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            if(NOT EXISTS "${compiler_bin}/${name}")
                message(FATAL_ERROR "Required MinGW x64 runtime missing: ${compiler_bin}/${name}")
            endif()
            list(APPEND compiler_dlls "${compiler_bin}/${name}")
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${compiler_dlls} "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploying MinGW runtime for ${target}" VERBATIM)
    endif()
    if(uses_qt)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}> "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMENT "Deploying Qt runtime and platform plugins for ${target}"
            COMMAND_EXPAND_LISTS VERBATIM)
    endif()
endfunction()
