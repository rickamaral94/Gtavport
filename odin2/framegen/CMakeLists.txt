# Gtavport — geração de quadros (odin2/framegen/README.md). Duas bibliotecas, compiladas por build.sh:
#   liblsfg_framegen.so  o framegen do lsfg-vk-android (LSFG 3.1/3.1P, MIT) com o volk dele, exportando só lsfgfg_*:
#                        o device adotado zera o vkCmdPipelineBarrier2 global do volk, que não pode ser o do plume.
#   liblsfg_extract.so   a importação do Lossless.dll do usuário no app (pe-parse MIT, dxbc do DXVK zlib), via JNI.
cmake_minimum_required(VERSION 3.22)
project(odin2_framegen C CXX)
set(LSFG ${LSFG_DIR})
if(NOT EXISTS ${LSFG}/framegen/v3.1_src OR NOT EXISTS ${LSFG}/thirdparty/dxbc/src/dxbc/dxbc_module.cpp)
    message(FATAL_ERROR "LSFG_DIR sem o lsfg-vk-android completo (tools/prepare.sh baixa em work/deps/lsfg-vk-android)")
endif()

set(LSFG_FG ${LSFG}/framegen)
file(GLOB LSFG_FG_SRC ${LSFG_FG}/src/common/*.cpp ${LSFG_FG}/src/core/*.cpp ${LSFG_FG}/src/pool/*.cpp ${LSFG_FG}/v3.1_src/*.cpp
     ${LSFG_FG}/v3.1_src/shaders/*.cpp ${LSFG_FG}/v3.1p_src/*.cpp ${LSFG_FG}/v3.1p_src/shaders/*.cpp)
add_library(lsfg_framegen SHARED ${LSFG_FG_SRC} ${LSFG}/thirdparty/volk/volk.c lsfg_framegen_c.cpp)
set_target_properties(lsfg_framegen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON C_VISIBILITY_PRESET hidden
                      CXX_VISIBILITY_PRESET hidden)
target_include_directories(lsfg_framegen PRIVATE ${LSFG_FG}/include ${LSFG_FG}/public ${LSFG_FG}/v3.1_include
                           ${LSFG_FG}/v3.1p_include ${LSFG}/thirdparty/volk ${VULKAN_HEADERS_DIR})
target_compile_definitions(lsfg_framegen PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(lsfg_framegen PRIVATE -O2 -w)
target_link_options(lsfg_framegen PRIVATE -Wl,-Bsymbolic -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lsfg_framegen.map)
target_link_libraries(lsfg_framegen PRIVATE log android dl)

set(PEPARSE ${LSFG}/thirdparty/pe-parse/pe-parser-library)
file(GLOB LSFG_DXBC_SRC ${LSFG}/thirdparty/dxbc/src/dxbc/*.cpp ${LSFG}/thirdparty/dxbc/src/spirv/*.cpp
     ${LSFG}/thirdparty/dxbc/src/util/*.cpp)
add_library(lsfg_extract SHARED lsfg_extract.cpp lsfg_translate.cpp lsfg_jni.cpp ${PEPARSE}/src/buffer.cpp
            ${PEPARSE}/src/parse.cpp ${PEPARSE}/src/unicode_codecvt.cpp ${LSFG_DXBC_SRC})
set_target_properties(lsfg_extract PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(lsfg_extract PRIVATE ${PEPARSE}/include ${LSFG}/thirdparty/dxbc/include/dxbc
                           ${LSFG}/thirdparty/dxbc/include/spirv ${LSFG}/thirdparty/dxbc/include/util
                           ${LSFG}/thirdparty/dxbc/include/dxvk ${VULKAN_HEADERS_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../driver)
target_compile_definitions(lsfg_extract PRIVATE LSFG_VK_COMMIT="${LSFG_VK_COMMIT}" LSFG_DXBC_COMMIT="${LSFG_DXBC_COMMIT}")
target_compile_options(lsfg_extract PRIVATE -O2)
set_source_files_properties(${PEPARSE}/src/buffer.cpp ${PEPARSE}/src/parse.cpp ${PEPARSE}/src/unicode_codecvt.cpp
                            ${LSFG_DXBC_SRC} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(lsfg_extract PRIVATE log)
