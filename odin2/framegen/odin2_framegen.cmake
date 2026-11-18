# Gtavport — geração de quadros no renderer (odin2/framegen/odin2_framegen.cpp; as bibliotecas do lsfg são compiladas à
# parte por odin2/framegen/build.sh). Incluído por odin2_perf.cmake.
if(ANDROID)
    target_sources(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR}/odin2_framegen.cpp)
    target_include_directories(gtav PRIVATE ${CMAKE_CURRENT_LIST_DIR})
endif()
