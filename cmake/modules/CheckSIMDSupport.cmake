include(CheckCXXCompilerFlag)

macro(check_and_add_simd_flag target flag_def flag_name compiler_flag)
    check_cxx_compiler_flag("${compiler_flag}" SUPPORTS_${flag_def})
    if(SUPPORTS_${flag_def})
        target_compile_options(${target} PRIVATE ${compiler_flag})
        target_compile_definitions(${target} PRIVATE -D${flag_def})
        set(ACTIVATED_SIMD_FLAGS "${ACTIVATED_SIMD_FLAGS}${flag_name}, ")
    endif()
endmacro()

function(configure_simd_flags target)
    unset(ACTIVATED_SIMD_FLAGS)

    if(MSVC)
        check_and_add_simd_flag(${target} "__AVX512F__" "AVX-512" "/arch:AVX512")
        check_and_add_simd_flag(${target} "__AVX2__" "AVX2" "/arch:AVX2")
        check_and_add_simd_flag(${target} "__AVX__" "AVX" "/arch:AVX")
        check_and_add_simd_flag(${target} "__SSE4_2__" "SSE4.2" "/arch:SSE4.2")
        check_and_add_simd_flag(${target} "__SSE4_1__" "SSE4.1" "/arch:SSE4.1")
        check_and_add_simd_flag(${target} "__SSE2__" "SSE2" "/arch:SSE2")
        check_and_add_simd_flag(${target} "__SSE__" "SSE" "/arch:SSE")
        check_and_add_simd_flag(${target} "__BMI__" "BMI" "/arch:BMI")
    else()
        check_and_add_simd_flag(${target} "__AVX512F__" "AVX-512" "-mavx512f")
        check_and_add_simd_flag(${target} "__AVX2__" "AVX2" "-mavx2")
        check_and_add_simd_flag(${target} "__AVX__" "AVX" "-mavx")
        check_and_add_simd_flag(${target} "__SSE4_2__" "SSE4.2" "-msse4.2")
        check_and_add_simd_flag(${target} "__SSE4_1__" "SSE4.1" "-msse4.1")
        check_and_add_simd_flag(${target} "__SSE2__" "SSE2" "-msse2")
        check_and_add_simd_flag(${target} "__SSE__" "SSE" "-msse")
        check_and_add_simd_flag(${target} "__BMI__" "BMI" "-mbmi")
    endif()

    string(REGEX REPLACE ", $" "" ACTIVATED_SIMD_FLAGS "${ACTIVATED_SIMD_FLAGS}")

    if(!ACTIVATED_SIMD_FLAGS)
        log_option_disabled("No SIMD flags were activated for target ${target}.")
    endif()
endfunction()