// The frame generation's shaders from the user's own Lossless.dll (lsfg_extract.h, docs/FRAMEGEN.md § B1).
// Checked in four layers before anything is installed: every shader the framegen loads is in the DLL (by its resource
// id); each translates; each is valid SPIR-V (SPIRV-Tools); each has the bindings the framegen gives it (lsfg_expect.inc,
// generated from the framegen's sources). The set goes into lsfg/spirv.importing with a MANIFEST marked complete
// (fsync), then replaces lsfg/spirv by renames: an interrupted install leaves the last good set.
#include "lsfg_extract.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <set>

#include <pe-parse/parse.h>

#include "odin2_sha256.h"  // (Gtavport: odin2/driver, FIPS 180-4, tested against sha256sum)

#ifndef LSFG_VK_COMMIT
#define LSFG_VK_COMMIT "unknown"
#endif
#ifndef LSFG_DXBC_COMMIT
#define LSFG_DXBC_COMMIT "unknown"
#endif
// (bumped when the extraction's output changes: a set from another version is imported again)
#define LSFG_EXTRACTOR_VERSION 1

namespace lsfg {

// The names the framegen asks its loader for and their RT_RCDATA ids in Lossless.dll - lsfg-vk-android's
// src/extract/extract.cpp (MIT), the same for LSFG 3.1 and 3.1P (p_). gamma[0] / delta[0] share a shader, as do the
// p_ ones.
const std::vector<Shader> &shader_table() {
    static const std::vector<Shader> t = {
        {"mipmaps", 255},    {"alpha[0]", 267},   {"alpha[1]", 268},   {"alpha[2]", 269},   {"alpha[3]", 270},
        {"beta[0]", 275},    {"beta[1]", 276},    {"beta[2]", 277},    {"beta[3]", 278},    {"beta[4]", 279},
        {"gamma[0]", 257},   {"gamma[1]", 259},   {"gamma[2]", 260},   {"gamma[3]", 261},   {"gamma[4]", 262},
        {"delta[0]", 257},   {"delta[1]", 263},   {"delta[2]", 264},   {"delta[3]", 265},   {"delta[4]", 266},
        {"delta[5]", 258},   {"delta[6]", 271},   {"delta[7]", 272},   {"delta[8]", 273},   {"delta[9]", 274},
        {"generate", 256},   {"p_mipmaps", 255},  {"p_alpha[0]", 290}, {"p_alpha[1]", 291}, {"p_alpha[2]", 292},
        {"p_alpha[3]", 293}, {"p_beta[0]", 298},  {"p_beta[1]", 299},  {"p_beta[2]", 300},  {"p_beta[3]", 301},
        {"p_beta[4]", 302},  {"p_gamma[0]", 280}, {"p_gamma[1]", 282}, {"p_gamma[2]", 283}, {"p_gamma[3]", 284},
        {"p_gamma[4]", 285}, {"p_delta[0]", 280}, {"p_delta[1]", 286}, {"p_delta[2]", 287}, {"p_delta[3]", 288},
        {"p_delta[4]", 289}, {"p_delta[5]", 281}, {"p_delta[6]", 294}, {"p_delta[7]", 295}, {"p_delta[8]", 296},
        {"p_delta[9]", 297}, {"p_generate", 256},
    };
    return t;
}

namespace {

struct Expect { const char *name; int counts[4]; };
const Expect kExpect[] = {
#include "lsfg_expect.inc"
};
const int *expected(const std::string &name) {
    for (auto &e : kExpect) if (name == e.name) return e.counts;
    return nullptr;
}

const char *cap_name(uint32_t c) {
    switch (c) {
    case 1: return "Shader";
    case 9: return "Float16";
    case 22: return "Int16";
    case 49: return "StorageImageExtendedFormats";
    case 50: return "ImageQuery";
    case 55: return "StorageImageReadWithoutFormat";
    case 56: return "StorageImageWriteWithoutFormat";
    case 5345: return "VulkanMemoryModel";
    case 5346: return "VulkanMemoryModelDeviceScope";
    default: return nullptr;
    }
}

std::string sha256_hex(const uint8_t *p, size_t n) {
    odin2::Sha256 h;
    h.Update(p, n);
    return h.HexDigest();
}

bool write_all(const std::string &path, const void *p, size_t n) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { close(fd); return false; }
        b += w, n -= size_t(w);
    }
    bool ok = fsync(fd) == 0;
    return close(fd) == 0 && ok;
}

void rm_tree(const std::string &dir) {
    DIR *d = opendir(dir.c_str());
    if (!d) { unlink(dir.c_str()); return; }
    for (dirent *e; (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(p);
        else unlink(p.c_str());
    }
    closedir(d);
    rmdir(dir.c_str());
}

void fsync_dir(const std::string &dir) {
    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) fsync(fd), close(fd);
}

std::string file_of(const std::string &name) {  // ("alpha[0]" -> "alpha_0_.spv")
    std::string f = name;
    for (auto &c : f) if (c == '[' || c == ']') c = '_';
    return f + ".spv";
}

}  // namespace

// The module's descriptor bindings by kind (uniform buffers, samplers, sampled images, storage images), its
// capabilities, and a GLCompute entry point - read from the instructions, bounds-checked.
bool spirv_bindings(const std::vector<uint32_t> &w, int counts[4], std::vector<std::string> &caps, std::string &err) {
    for (int i = 0; i < 4; i++) counts[i] = 0;
    if (w.size() < 5 || w[0] != 0x07230203u) { err = "not SPIR-V"; return false; }
    uint32_t bound = w[3];
    if (bound == 0 || bound > (1u << 22)) { err = "bad id bound"; return false; }
    enum Kind : uint8_t { NONE, SAMPLER, IMAGE_SAMPLED, IMAGE_STORAGE, STRUCT, ARRAY, POINTER, CONST };
    std::vector<uint8_t> kind(bound, NONE);
    std::vector<uint32_t> a(bound, 0), b(bound, 0);  // (pointer: storage class, pointee; array: element, length id)
    std::vector<uint32_t> cval(bound, 0);
    std::vector<uint8_t> has_binding(bound, 0);
    std::vector<std::pair<uint32_t, uint32_t>> vars;  // (variable id, its pointer type)
    bool compute = false;
    std::set<uint32_t> capset;
    for (size_t i = 5; i < w.size();) {
        uint32_t op = w[i] & 0xFFFF, n = w[i] >> 16;
        if (!n || i + n > w.size()) { err = "truncated instruction"; return false; }
        const uint32_t *o = &w[i + 1];
        auto id_ok = [&](uint32_t id) { return id < bound; };
        switch (op) {
        case 17: if (n >= 2) capset.insert(o[0]); break;                          // OpCapability
        case 15: if (n >= 2 && o[0] == 5) compute = true; break;                   // OpEntryPoint GLCompute
        case 26: if (n >= 2 && id_ok(o[0])) kind[o[0]] = SAMPLER; break;           // OpTypeSampler
        case 25:                                                                   // OpTypeImage
            if (n >= 9 && id_ok(o[0])) kind[o[0]] = o[6] == 2 ? IMAGE_STORAGE : IMAGE_SAMPLED;
            break;
        case 30: if (n >= 2 && id_ok(o[0])) kind[o[0]] = STRUCT; break;            // OpTypeStruct
        case 28:                                                                   // OpTypeArray
            if (n >= 4 && id_ok(o[0])) kind[o[0]] = ARRAY, a[o[0]] = o[1], b[o[0]] = o[2];
            break;
        case 32:                                                                   // OpTypePointer
            if (n >= 4 && id_ok(o[0])) kind[o[0]] = POINTER, a[o[0]] = o[1], b[o[0]] = o[2];
            break;
        case 43: if (n >= 4 && id_ok(o[1])) kind[o[1]] = CONST, cval[o[1]] = o[2]; break;  // OpConstant
        case 71: if (n >= 4 && o[1] == 33 && id_ok(o[0])) has_binding[o[0]] = 1; break;    // OpDecorate .. Binding
        case 59: if (n >= 4 && id_ok(o[1])) vars.push_back({o[1], o[0]}); break;           // OpVariable
        default: break;
        }
        i += n;
    }
    if (!compute) { err = "no compute entry point"; return false; }
    for (auto &v : vars) {
        if (!has_binding[v.first]) continue;
        uint32_t t = v.second;
        if (t >= bound || kind[t] != POINTER) { err = "a binding without a pointer type"; return false; }
        uint32_t sc = a[t], p = b[t], mult = 1;
        if (p < bound && kind[p] == ARRAY) {
            uint32_t len = b[p];
            mult = len < bound && kind[len] == CONST ? cval[len] : 0;
            p = a[p];
        }
        if (p >= bound || !mult || mult > 64) { err = "an unsupported binding"; return false; }
        int k = kind[p] == SAMPLER ? 1 : kind[p] == IMAGE_SAMPLED ? 2 : kind[p] == IMAGE_STORAGE ? 3 : kind[p] == STRUCT && sc == 2 ? 0 : -1;
        if (k < 0) { err = "a binding of an unexpected kind"; return false; }
        counts[k] += int(mult);
    }
    caps.clear();
    for (uint32_t c : capset) {
        const char *nm = cap_name(c);
        caps.push_back(nm ? nm : std::to_string(c));
    }
    return true;
}

Result extract(const uint8_t *dll, size_t size, const std::string &root, const Translator &tr, const Validator &val, Report &rep) {
    rep = Report();
    if (size < 64 || size > (512u << 20)) { rep.detail = "not a Lossless.dll (size)"; return NOT_PE; }
    // (pe-parse reads from its own copy of the bytes)
    std::vector<uint8_t> copy(dll, dll + size);
    peparse::parsed_pe *pe = peparse::ParsePEFromPointer(copy.data(), uint32_t(copy.size()));
    if (!pe) { rep.detail = "not a Windows DLL"; return NOT_PE; }
    std::map<uint32_t, std::vector<uint8_t>> res;
    peparse::IterRsrc(pe, [](void *ctx, const peparse::resource &r) -> int {
        auto *m = static_cast<std::map<uint32_t, std::vector<uint8_t>> *>(ctx);
        if (r.type == peparse::RT_RCDATA && r.buf && r.buf->buf && r.buf->bufLen > 0 && r.buf->bufLen < (16u << 20))
            (*m)[r.name] = std::vector<uint8_t>(r.buf->buf, r.buf->buf + r.buf->bufLen);
        return 0;
    }, &res);
    peparse::DestructParsedPE(pe);
    // 1. every shader there
    std::string missing;
    for (auto &s : shader_table())
        if (!res.count(s.id)) missing += (missing.empty() ? "" : ", ") + std::string(s.name);
    if (!missing.empty()) { rep.detail = "missing: " + missing; return MISSING; }
    // 2-4. each translated, valid, with the framegen's bindings
    std::map<uint32_t, std::vector<uint32_t>> spirv;
    std::set<std::string> caps;
    for (auto &s : shader_table()) {
        if (!spirv.count(s.id)) {
            std::vector<uint32_t> out;
            std::string err;
            if (!tr(res[s.id], out, err) || out.empty()) { rep.detail = std::string(s.name) + ": " + err; return TRANSLATE; }
            if (val && !val(out, err)) { rep.detail = std::string(s.name) + ": " + err; return INVALID; }
            spirv[s.id] = std::move(out);
        }
        int counts[4];
        std::vector<std::string> c;
        std::string err;
        if (!spirv_bindings(spirv[s.id], counts, c, err)) { rep.detail = std::string(s.name) + ": " + err; return INVALID; }
        const int *want = expected(s.name);
        if (!want || memcmp(want, counts, sizeof counts)) {
            char d[160];
            snprintf(d, sizeof d, "%s: bindings %d/%d/%d/%d, the framegen gives %d/%d/%d/%d", s.name, counts[0], counts[1], counts[2],
                     counts[3], want ? want[0] : -1, want ? want[1] : -1, want ? want[2] : -1, want ? want[3] : -1);
            rep.detail = d;
            return BINDINGS;
        }
        caps.insert(c.begin(), c.end());
    }
    rep.dll_sha256 = sha256_hex(dll, size);
    rep.capabilities.assign(caps.begin(), caps.end());
    // the set, then the swap
    std::string top = root + "/lsfg", stage = top + "/spirv.importing", live = top + "/spirv", old = top + "/spirv.old";
    mkdir(top.c_str(), 0700);
    rm_tree(stage);
    if (mkdir(stage.c_str(), 0700) && errno != EEXIST) { rep.detail = "can't create " + stage; return WRITE; }
    for (auto &s : shader_table()) {
        auto &v = spirv[s.id];
        if (!write_all(stage + "/" + file_of(s.name), v.data(), v.size() * 4)) { rm_tree(stage); rep.detail = "write failed"; return WRITE; }
    }
    std::string m = "lsfg-shaders 1\nextractor " + std::to_string(LSFG_EXTRACTOR_VERSION) + "\nlsfg-vk-android " LSFG_VK_COMMIT
                    "\ndxbc " LSFG_DXBC_COMMIT "\ndll-sha256 " + rep.dll_sha256 + "\ndll-size " + std::to_string(size) +
                    "\nshaders " + std::to_string(shader_table().size()) + "\ncapabilities";
    for (auto &c : rep.capabilities) m += " " + c;
    m += "\ncomplete\n";
    if (!write_all(stage + "/MANIFEST", m.data(), m.size())) { rm_tree(stage); rep.detail = "write failed"; return WRITE; }
    fsync_dir(stage);
    rm_tree(old);
    struct stat st;
    if (stat(live.c_str(), &st) == 0 && rename(live.c_str(), old.c_str())) { rm_tree(stage); rep.detail = "rename failed"; return WRITE; }
    if (rename(stage.c_str(), live.c_str())) {
        rename(old.c_str(), live.c_str());
        rm_tree(stage);
        rep.detail = "rename failed";
        return WRITE;
    }
    fsync_dir(top);
    rm_tree(old);
    rep.shaders = int(shader_table().size());
    rep.detail = std::to_string(rep.shaders) + " shaders";
    return OK;
}

Result extract_fd(int fd, const std::string &root, const Translator &tr, const Validator &val, Report &rep) {
    // Read sequentially to the end - also from a provider that can't seek or doesn't know the size (a pipe) - into
    // memory, never into a file: the DLL itself isn't kept anywhere.
    const size_t cap = 512u << 20;
    if (fd < 0) { rep.detail = "can't read the file"; return READ; }
    struct stat st;
    std::vector<uint8_t> buf;
    buf.reserve(fstat(fd, &st) == 0 && st.st_size > 0 && size_t(st.st_size) <= cap ? size_t(st.st_size) : 16u << 20);
    uint8_t chunk[1 << 16];
    bool bad = false;
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { bad = true; break; }
        if (r == 0) break;
        if (buf.size() + size_t(r) > cap) { bad = true; break; }
        buf.insert(buf.end(), chunk, chunk + r);
    }
    close(fd);
    if (bad || buf.empty()) { rep.detail = "can't read the file"; return READ; }
    return extract(buf.data(), buf.size(), root, tr, val, rep);
}

void recover(const std::string &root) {
    std::string top = root + "/lsfg", live = top + "/spirv", old = top + "/spirv.old";
    rm_tree(top + "/spirv.importing");
    struct stat st;
    if (stat(live.c_str(), &st) != 0 && stat(old.c_str(), &st) == 0) rename(old.c_str(), live.c_str());  // (died mid-swap)
    else rm_tree(old);
}

bool installed(const std::string &root, std::string *summary, std::vector<std::string> *capabilities) {
    std::string live = root + "/lsfg/spirv";
    FILE *f = fopen((live + "/MANIFEST").c_str(), "re");
    if (!f) return false;
    char line[1024];
    bool complete = false, version = false;
    std::string sha;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (!strcmp(line, "complete")) complete = true;
        else if (!strncmp(line, "extractor ", 10)) version = atoi(line + 10) == LSFG_EXTRACTOR_VERSION;
        else if (!strncmp(line, "dll-sha256 ", 11)) sha = line + 11;
        else if (!strncmp(line, "capabilities", 12) && capabilities) {
            capabilities->clear();
            char *save = nullptr;
            for (char *p = strtok_r(line + 12, " ", &save); p; p = strtok_r(nullptr, " ", &save)) capabilities->push_back(p);
        }
    }
    fclose(f);
    if (!complete || !version) return false;
    for (auto &s : shader_table()) {
        struct stat st;
        if (stat((live + "/" + file_of(s.name)).c_str(), &st) || st.st_size < 20 || st.st_size % 4) return false;
    }
    if (summary) *summary = std::to_string(shader_table().size()) + " shaders, Lossless.dll " + sha.substr(0, 12);
    return true;
}

void remove_all(const std::string &root) { rm_tree(root + "/lsfg"); }

}  // namespace lsfg
