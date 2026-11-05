# Gtavport — pós-processamento (odin2/post). Incluído pelo CMakeLists.txt do jogo (patch 0006) depois
# de definir o alvo `gtav`, GTAV_DXC e GTAV_NATIVE_SHADER_DIR. Os shaders são compilados como os do
# próprio renderer: DXC do XenosRecomp -> SPIR-V em headers (-Fh), Vulkan 1.1.
set(ODIN2_POST_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ODIN2_POST_HLSL ${ODIN2_POST_DIR}/odin2_post.hlsl)
set(ODIN2_POST_ENTRIES "vs_6_0:VSPost" "ps_6_0:PSFxaa" "ps_6_0:PSSmaaEdge" "ps_6_0:PSSmaaWeight"
                       "ps_6_0:PSSmaaBlend" "ps_6_0:PSEasu" "ps_6_0:PSRcas" "ps_6_0:PSSgsr1")
set(_odin2_cmds)
set(_odin2_includes)
foreach(_e ${ODIN2_POST_ENTRIES})
    string(REPLACE ":" ";" _p ${_e})
    list(GET _p 0 _profile)
    list(GET _p 1 _entry)
    list(APPEND _odin2_cmds COMMAND ${GTAV_DXC} -T ${_profile} -E ${_entry} -spirv -fspv-target-env=vulkan1.1
         -I ${ODIN2_POST_DIR} -Vn g_odin2_${_entry} -Fh ${GTAV_NATIVE_SHADER_DIR}/odin2_post_${_entry}.h ${ODIN2_POST_HLSL})
    list(APPEND _odin2_includes COMMAND ${CMAKE_COMMAND} -E echo "#include \"odin2_post_${_entry}.h\""
         >> ${GTAV_NATIVE_SHADER_DIR}/odin2_post_shaders.h)
endforeach()
add_custom_command(
    OUTPUT ${GTAV_NATIVE_SHADER_DIR}/odin2_post_shaders.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_NATIVE_SHADER_DIR}
    ${_odin2_cmds}
    COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" > ${GTAV_NATIVE_SHADER_DIR}/odin2_post_shaders.h
    ${_odin2_includes}
    DEPENDS ${ODIN2_POST_HLSL} ${ODIN2_POST_DIR}/third_party/smaa/SMAA.hlsl
    VERBATIM)
add_custom_target(odin2_post_shaders DEPENDS ${GTAV_NATIVE_SHADER_DIR}/odin2_post_shaders.h)
add_dependencies(gtav odin2_post_shaders)
target_sources(gtav PRIVATE ${ODIN2_POST_DIR}/odin2_post.cpp)
target_include_directories(gtav PRIVATE ${ODIN2_POST_DIR})
