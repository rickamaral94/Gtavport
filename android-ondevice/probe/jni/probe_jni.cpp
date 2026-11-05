// Gtavport — APK de prova (P1/P2/P3): executa ferramentas empacotadas e mede cada processo.
#include <jni.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <spawn.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

extern char** environ;

namespace {
double NowMs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
std::string Str(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r(c);
  env->ReleaseStringUTFChars(s, c);
  return r;
}
}  // namespace

// long[] {exit (ou -errno se não iniciou), wall_ms, maxrss_kb, user_ms, sys_ms, sinal}
// cpuMask: bits dos núcleos onde o processo roda (0 = sem restrição); o filho herda a afinidade
// da thread que o cria, então ela é aplicada nesta thread e restaurada depois.
extern "C" JNIEXPORT jlongArray JNICALL Java_com_gtavport_probe_Native_runTool(
    JNIEnv* env, jclass, jobjectArray jargv, jstring jcwd, jstring jlog, jlong cpuMask) {
  std::vector<std::string> args;
  for (jsize i = 0; i < env->GetArrayLength(jargv); ++i)
    args.push_back(Str(env, (jstring)env->GetObjectArrayElement(jargv, i)));
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  std::string cwd = Str(env, jcwd), log = Str(env, jlog);

  cpu_set_t old_set, set;
  bool pinned = false;
  if (cpuMask) {
    CPU_ZERO(&set);
    for (int c = 0; c < 64; ++c)
      if (cpuMask & (1LL << c)) CPU_SET(c, &set);
    pinned = sched_getaffinity(0, sizeof(old_set), &old_set) == 0 &&
             sched_setaffinity(0, sizeof(set), &set) == 0;
  }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  if (!log.empty()) {
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
  }
  // Bionic não tem addchdir em todas as versões: muda o cwd do filho via /proc não é possível,
  // então o chamador passa caminhos absolutos e cwd só é usado se suportado (API 34+).
#if __ANDROID_API__ >= 34
  if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
#endif
  double t0 = NowMs();
  pid_t pid = -1;
  int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  if (pinned) sched_setaffinity(0, sizeof(old_set), &old_set);

  jlong out[6] = {-rc, 0, 0, 0, 0, 0};
  if (rc == 0) {
    int status = 0;
    rusage ru{};
    while (wait4(pid, &status, 0, &ru) < 0 && errno == EINTR) {}
    out[0] = WIFEXITED(status) ? WEXITSTATUS(status) : -1000;
    out[1] = jlong(NowMs() - t0);
    out[2] = ru.ru_maxrss;  // KB no Linux
    out[3] = ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000;
    out[4] = ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000;
    out[5] = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
  }
  jlongArray r = env->NewLongArray(6);
  env->SetLongArrayRegion(r, 0, 6, out);
  return r;
}

// P2: dlopen de uma .so gerada no aparelho e chamada de int fn(int). Devolve "ok:<valor>" ou o erro.
extern "C" JNIEXPORT jstring JNICALL Java_com_gtavport_probe_Native_dlopenCall(
    JNIEnv* env, jclass, jstring jpath, jstring jsym, jint arg) {
  std::string path = Str(env, jpath), sym = Str(env, jsym);
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) return env->NewStringUTF((std::string("dlopen: ") + dlerror()).c_str());
  auto fn = reinterpret_cast<int (*)(int)>(dlsym(h, sym.c_str()));
  if (!fn) return env->NewStringUTF((std::string("dlsym: ") + dlerror()).c_str());
  return env->NewStringUTF(("ok:" + std::to_string(fn(arg))).c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_com_gtavport_probe_Native_strerror(JNIEnv* env, jclass, jint e) {
  return env->NewStringUTF(strerror(e));
}
