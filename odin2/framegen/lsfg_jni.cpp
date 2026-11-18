// Gtavport — o lado do app da importação do Lossless.dll (Odin2Framegen.java): o arquivo que o usuário escolhe vira
// shaders SPIR-V em <filesDir>/lsfg/spirv (lsfg_extract.cpp, do TOTKRecompAndroid). O DLL não é copiado nem guardado;
// nada dele vai no APK. O jogo (processo :game) lê a mesma pasta (GTAVPORT_LSFG_DIR, Odin2DeviceBuild.configureRuntime).
#include <jni.h>
#include <stdio.h>
#include <unistd.h>

#include <string>

#include "lsfg_extract.h"

namespace lsfg {
bool dxbc_translate(const std::vector<uint8_t> &dxbc, std::vector<uint32_t> &out, std::string &err);
bool spirv_validate(const std::vector<uint32_t> &spirv, std::string &err);
}  // namespace lsfg

namespace {
std::string Str(JNIEnv *env, jstring s) {
    if (!s) return {};
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}
}  // namespace

// "<código>|<detalhe>": 0 instalado; os outros, lsfg::Result (o detalhe diz o que falhou)
extern "C" JNIEXPORT jstring JNICALL Java_com_gtavrecomp_app_Odin2Framegen_nativeImport(JNIEnv *env, jclass, jint fd, jstring root) {
    std::string r = Str(env, root);
    lsfg::Report rep;
    lsfg::Result res = r.empty() ? lsfg::WRITE : lsfg::extract_fd(fd, r, lsfg::dxbc_translate, lsfg::spirv_validate, rep);
    if (r.empty() && fd >= 0) close(fd);
    std::string caps;
    for (auto &c : rep.capabilities) caps += " " + c;
    std::string out = std::to_string(int(res)) + "|" + rep.detail;
    if (res == lsfg::OK) out += "\nsha256 " + rep.dll_sha256 + "\ncapabilities:" + caps;
    return env->NewStringUTF(out.c_str());
}

// "1|<resumo>" com um conjunto completo instalado, senão "0|"
extern "C" JNIEXPORT jstring JNICALL Java_com_gtavrecomp_app_Odin2Framegen_nativeState(JNIEnv *env, jclass, jstring root) {
    std::string r = Str(env, root), s;
    if (!r.empty()) lsfg::recover(r);
    bool ok = !r.empty() && lsfg::installed(r, &s);
    return env->NewStringUTF(((ok ? "1|" : "0|") + s).c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_gtavrecomp_app_Odin2Framegen_nativeRemove(JNIEnv *env, jclass, jstring root) {
    std::string r = Str(env, root);
    if (!r.empty()) lsfg::remove_all(r);
}
