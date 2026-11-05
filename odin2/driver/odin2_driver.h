// Gtavport — seleção do driver Vulkan (Etapa 0.5).
// Biblioteca própria (libodin2driver.so) para que o runtime do ReXGlue e o plume, que carregam o
// Vulkan cada um por conta própria, usem o MESMO driver no processo. Eles a abrem com
// dlopen("libodin2driver.so"), sem dependência de link: sem a biblioteca no APK, tudo fica como
// no upstream.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Handle de dlopen de um libvulkan.so que carrega o driver escolhido, ou nullptr para usar o
// carregador do sistema normalmente. Abre uma vez por processo (thread-safe); chamadas seguintes
// devolvem o mesmo handle. Nunca é fechado.
//
// Variáveis de ambiente (definidas pela GameActivity antes de carregar as bibliotecas nativas):
//   ODIN2_VK_DRIVER      nome do driver escolhido ("" ou ausente = driver do sistema)
//   ODIN2_VK_DRIVER_DIR  diretório interno com o driver extraído (getFilesDir()/drivers/<nome>/)
//   ODIN2_VK_DRIVER_LIB  nome do .so do driver dentro do diretório (meta.json "libraryName")
//   ODIN2_VK_HOOK_DIR    getApplicationInfo().nativeLibraryDir (hooks do adrenotools)
//   ODIN2_VK_TMP_DIR     diretório gravável para o adrenotools
void* odin2_vulkan_open(void);

// Nome do driver efetivamente em uso: "system" ou o valor de ODIN2_VK_DRIVER. Se a abertura do
// driver customizado falhar, volta para "system" (registrado no log como erro): o benchmark
// registra este valor, nunca o pedido.
const char* odin2_vulkan_driver_name(void);

typedef void* (*odin2_vulkan_open_fn)(void);

#ifdef __cplusplus
}
#endif
