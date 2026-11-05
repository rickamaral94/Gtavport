# XenosRecomp (com o patch do upstream) para aarch64-android, ligado ao libdxcompiler.so do
# build_dxc.sh. O alvo Microsoft::DirectXShaderCompiler é definido aqui antes, então o dxc-bin do
# XenosRecomp (binários glibc) não é usado.
cmake_minimum_required(VERSION 3.20)
project(gtavport-shadertools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT XENOS_DIR OR NOT DXC_LIB OR NOT DXC_INC)
  message(FATAL_ERROR "defina XENOS_DIR, DXC_LIB (libdxcompiler.so) e DXC_INC")
endif()
add_library(Microsoft::DirectXShaderCompiler SHARED IMPORTED GLOBAL)
set_property(TARGET Microsoft::DirectXShaderCompiler PROPERTY IMPORTED_LOCATION "${DXC_LIB}")
set_property(TARGET Microsoft::DirectXShaderCompiler PROPERTY IMPORTED_NO_SONAME TRUE)
target_include_directories(Microsoft::DirectXShaderCompiler INTERFACE "${DXC_INC}")
add_subdirectory("${XENOS_DIR}" xenosrecomp)
