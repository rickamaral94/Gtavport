# Gtavport — seção Performance (odin2/perf). Incluído pelo CMakeLists.txt do jogo (patch 0007).
target_sources(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR}/odin2_perf.cpp)
target_include_directories(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
