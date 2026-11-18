# Gtavport — seção Performance (odin2/perf). Incluído pelo CMakeLists.txt do jogo (patch 0007).
target_sources(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR}/odin2_perf.cpp ${CMAKE_CURRENT_LIST_DIR}/odin2_stall.cpp
            ${CMAKE_CURRENT_LIST_DIR}/odin2_diag.cpp ${CMAKE_CURRENT_LIST_DIR}/odin2_adpf.cpp)
target_include_directories(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
# Gtavport: geração de quadros (odin2/framegen -> ../odin2_framegen), copiada por tools/prepare.sh.
include(${CMAKE_CURRENT_LIST_DIR}/../odin2_framegen/odin2_framegen.cmake)
