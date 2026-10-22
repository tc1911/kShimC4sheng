function(kshim_enable_lvgl_smp_adapter target lvgl_root)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "LVGL target '${target}' does not exist")
    endif()
    if(NOT IS_DIRECTORY "${lvgl_root}/src")
        message(FATAL_ERROR "Invalid LVGL source root: ${lvgl_root}")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_draw_sw.py")
    set(upstream "${lvgl_root}/src/draw/sw/lv_draw_sw.c")
    set(generated_dir "${CMAKE_BINARY_DIR}/generated/kshim-lvgl")
    set(generated "${generated_dir}/lv_draw_sw.c")

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${generator}" "${upstream}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
            --source "${upstream}" --output "${generated}"
        RESULT_VARIABLE generator_status
        OUTPUT_VARIABLE generator_stdout
        ERROR_VARIABLE generator_stderr)
    if(NOT generator_status EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate the kShim LVGL adapter:\n"
            "${generator_stdout}${generator_stderr}")
    endif()

    get_target_property(lvgl_sources "${target}" SOURCES)
    list(FIND lvgl_sources "${upstream}" upstream_index)
    if(upstream_index EQUAL -1)
        message(FATAL_ERROR
            "${target} does not contain the expected pinned lv_draw_sw.c")
    endif()
    list(REMOVE_AT lvgl_sources ${upstream_index})
    list(APPEND lvgl_sources "${generated}")
    set_property(TARGET "${target}" PROPERTY SOURCES "${lvgl_sources}")
    set_source_files_properties("${generated}" PROPERTIES GENERATED TRUE)
    target_include_directories("${target}" PRIVATE
        "${CMAKE_SOURCE_DIR}/src/include")
endfunction()
