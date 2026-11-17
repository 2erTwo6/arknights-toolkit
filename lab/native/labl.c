// liblab.so —— 运行时 il2cpp 枚举/探测（供 files/ak_cmd 命令通道调用）
#define _GNU_SOURCE
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#define TAG "AKLAB-N"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef struct Il2CppDomain Il2CppDomain;
typedef struct Il2CppAssembly Il2CppAssembly;
typedef struct Il2CppImage Il2CppImage;
typedef struct Il2CppClass Il2CppClass;
typedef struct MethodInfo MethodInfo;
typedef struct Il2CppString Il2CppString;

static unsigned long g_base;
static int cb_phdr(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "libil2cpp.so"))
        g_base = (unsigned long)info->dlpi_addr;
    return 0;
}
static unsigned long il2cpp_base(void) {
    if (!g_base) dl_iterate_phdr(cb_phdr, NULL);
    return g_base;
}

static void *g_handle;

/* 直接从磁盘上的 ELF 解析 .dynsym：绕过 Android linker namespace 导致的
   dlopen(RTLD_NOLOAD)/dlsym(RTLD_DEFAULT) 失败问题。 */
static Elf64_Sym *g_dynsym;
static size_t g_nsym;
static char *g_dynstr;
static int g_elf_tried;

static void load_elf_syms(void) {
    g_elf_tried = 1;
    char path[512] = "";
    FILE *m = fopen("/proc/self/maps", "r");
    if (m) {
        char line[600];
        while (fgets(line, sizeof(line), m)) {
            char *sp = strrchr(line, ' ');
            if (!sp) continue;
            char *p = sp + 1;
            p[strcspn(p, "\n")] = 0;
            if (strstr(p, "libil2cpp.so")) { snprintf(path, sizeof(path), "%s", p); break; }
        }
        fclose(m);
    }
    if (!path[0]) { LOGI("elf: path not found"); return; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { LOGI("elf: open failed %s", path); return; }
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != (ssize_t)sizeof(eh) || memcmp(eh.e_ident, ELFMAG, 4) != 0) {
        close(fd); LOGI("elf: bad header"); return;
    }
    size_t shnum = eh.e_shnum, shentsize = eh.e_shentsize;
    Elf64_Shdr *sh = malloc(shnum * shentsize);
    if (!sh || pread(fd, sh, shnum * shentsize, eh.e_shoff) != (ssize_t)(shnum * shentsize)) {
        free(sh); close(fd); LOGI("elf: read shdr failed"); return;
    }
    int si = -1, st = -1;
    for (size_t i = 0; i < shnum; i++) {
        if (sh[i].sh_type == SHT_DYNSYM) { si = (int)i; st = (int)sh[i].sh_link; }
    }
    if (si < 0) { free(sh); close(fd); LOGI("elf: no dynsym"); return; }
    g_nsym = sh[si].sh_size / sizeof(Elf64_Sym);
    g_dynsym = malloc(sh[si].sh_size);
    g_dynstr = malloc(sh[st].sh_size);
    if (g_dynsym) pread(fd, g_dynsym, sh[si].sh_size, sh[si].sh_offset);
    if (g_dynstr) pread(fd, g_dynstr, sh[st].sh_size, sh[st].sh_offset);
    free(sh); close(fd);
    LOGI("elf: dynsym=%zu path=%s", g_nsym, path);
}

static void *sym(const char *n) {
    void *p = NULL;
    if (!g_handle) g_handle = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
    if (g_handle) p = dlsym(g_handle, n);
    if (!p) p = dlsym(RTLD_DEFAULT, n);
    if (!p) {
        if (!g_elf_tried) load_elf_syms();
        if (g_dynsym && g_dynstr) {
            for (size_t i = 0; i < g_nsym; i++) {
                const char *sn = g_dynstr + g_dynsym[i].st_name;
                if (strcmp(sn, n) == 0 && ELF64_ST_TYPE(g_dynsym[i].st_info) == STT_FUNC) {
                    p = (void *)(il2cpp_base() + g_dynsym[i].st_value);
                    break;
                }
            }
        }
    }
    if (!p) LOGI("missing symbol %s", n);
    return p;
}

/* ---------- output buffer ---------- */
static char *g_out;
static size_t g_cap = 4u << 20, g_len;
static void out(const char *fmt, ...) {
    if (!g_out) { g_out = malloc(g_cap); if (!g_out) return; }
    if (g_len + 512 > g_cap) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(g_out + g_len, g_cap - g_len, fmt, ap);
    va_end(ap);
    if (n > 0 && g_len + (size_t)n < g_cap) g_len += (size_t)n;
}

/* ---------- il2cpp typedefs ---------- */
typedef Il2CppDomain *(*fn_dom_get)(void);
typedef Il2CppDomain *(*fn_attach)(Il2CppDomain *);
typedef const Il2CppAssembly **(*fn_asms)(const Il2CppDomain *, size_t *);
typedef Il2CppImage *(*fn_asm_img)(const Il2CppAssembly *);
typedef size_t (*fn_img_cnt)(const Il2CppImage *);
typedef const Il2CppClass *(*fn_img_cls)(const Il2CppImage *, size_t);
typedef Il2CppClass *(*fn_cls_from_name)(const Il2CppImage *, const char *, const char *);
typedef const char *(*fn_cls_name)(const Il2CppClass *);
typedef const char *(*fn_cls_ns)(const Il2CppClass *);
typedef const MethodInfo *(*fn_cls_methods)(Il2CppClass *, void **);
typedef const char *(*fn_meth_name)(const MethodInfo *);
typedef uint32_t (*fn_meth_pc)(const MethodInfo *);
typedef uint32_t (*fn_meth_flags)(const MethodInfo *, uint32_t *);
typedef void *(*fn_cls_fields)(Il2CppClass *, void **);
typedef const char *(*fn_field_name)(void *);
typedef size_t (*fn_field_off)(void *);
typedef void *(*fn_field_type)(void *);
typedef const char *(*fn_type_name)(void *);
typedef Il2CppString *(*fn_string_new)(const char *);

#define GETSYMS()                                                              \
    fn_dom_get dom_get = (fn_dom_get)sym("il2cpp_domain_get");                 \
    fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");                 \
    fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");               \
    fn_asm_img asm_img = (fn_asm_img)sym("il2cpp_assembly_get_image");         \
    fn_img_cnt img_cnt = (fn_img_cnt)sym("il2cpp_image_get_class_count");      \
    fn_img_cls img_cls = (fn_img_cls)sym("il2cpp_image_get_class");            \
    fn_cls_from_name cfn = (fn_cls_from_name)sym("il2cpp_class_from_name");    \
    fn_cls_name cls_name = (fn_cls_name)sym("il2cpp_class_get_name");          \
    fn_cls_ns cls_ns = (fn_cls_ns)sym("il2cpp_class_get_namespace");           \
    fn_cls_methods cls_methods = (fn_cls_methods)sym("il2cpp_class_get_methods");\
    fn_meth_name meth_name = (fn_meth_name)sym("il2cpp_method_get_name");      \
    fn_meth_pc meth_pc = (fn_meth_pc)sym("il2cpp_method_get_param_count");     \
    if (!dom_get || !asms || !asm_img || !img_cnt || !img_cls || !cls_name ||  \
        !cls_ns || (!cls_methods && 0)) { return "missing il2cpp symbols\n"; } \
    Il2CppDomain *dom = dom_get();                                             \
    if (!dom) return "il2cpp domain not ready\n";                              \
    if (attach) attach(dom);

static Il2CppClass *find_class(const char *ns, const char *name) {
    fn_dom_get dom_get = (fn_dom_get)sym("il2cpp_domain_get");
    fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");
    fn_asm_img asm_img = (fn_asm_img)sym("il2cpp_assembly_get_image");
    fn_cls_from_name cfn = (fn_cls_from_name)sym("il2cpp_class_from_name");
    if (!dom_get || !asms || !asm_img || !cfn) return NULL;
    Il2CppDomain *dom = dom_get();
    if (!dom) return NULL;
    {
        fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");
        if (attach) attach(dom);
    }
    size_t n = 0;
    const Il2CppAssembly **a = asms(dom, &n);
    LOGI("find_class dom=%p asms=%zu ns=%s name=%s", (void*)dom, n, ns, name);
    for (size_t i = 0; a && i < n; i++) {
        Il2CppImage *im = asm_img(a[i]);
        if (!im) { LOGI("  img[%zu] null", i); continue; }
        Il2CppClass *c = cfn(im, ns, name);
        if (c) { LOGI("  found in img[%zu]", i); return c; }
    }
    return NULL;
}

static jstring ret(JNIEnv *env) {
    jstring s = (*env)->NewStringUTF(env, g_out ? g_out : "");
    g_len = 0;
    if (g_out) g_out[0] = 0;
    return s;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_base(JNIEnv *env, jclass c) {
    (void)c;
    out("il2cpp base=0x%lx\n", il2cpp_base());
    return ret(env);
}

static FILE *g_fp;
static void fout(const char *fmt, ...) {
    if (!g_fp) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_fp, fmt, ap);
    va_end(ap);
}

/* 枚举类/方法，直接写文件，避免 JNI 返回超大字符串 */
JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_dump(JNIEnv *env, jclass c, jstring jfilter, jstring jpath, jint withMethods) {
    (void)c;
    const char *filter = jfilter ? (*env)->GetStringUTFChars(env, jfilter, NULL) : "";
    const char *path = jpath ? (*env)->GetStringUTFChars(env, jpath, NULL) : "";
    GETSYMS();
    g_fp = fopen(path, "a");
    if (!g_fp) { if (jfilter) (*env)->ReleaseStringUTFChars(env, jfilter, filter);
                 if (jpath) (*env)->ReleaseStringUTFChars(env, jpath, path);
                 return "open failed\n"; }
    int printed = 0, lines = 0;
    size_t an = 0;
    const Il2CppAssembly **as = asms(dom, &an);
    fout("assemblies=%zu filter='%s' methods=%d\n", an, filter, withMethods);
    for (size_t i = 0; as && i < an && printed < 60; i++) {
        Il2CppImage *im = asm_img(as[i]);
        if (!im) continue;
        size_t cc = img_cnt(im);
        for (size_t j = 0; j < cc && printed < 60; j++) {
            const Il2CppClass *cl = img_cls(im, j);
            if (!cl) continue;
            const char *nm = cls_name(cl);
            const char *ns = cls_ns(cl);
            if (!nm) continue;
            char full[400];
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", nm);
            if (filter[0] && !strstr(full, filter)) continue;
            printed++;
            fout("C %s\n", full);
            if (!withMethods) continue;
            void *iter = NULL;
            const MethodInfo *m;
            int mc = 0;
            while (cls_methods && (m = cls_methods((Il2CppClass *)cl, &iter)) != NULL && mc < 150) {
                const char *mn = meth_name ? meth_name(m) : "?";
                void *ptr = *(void **)m;
                fout("  M %s/%u rva=0x%lx\n", mn ? mn : "?", meth_pc ? meth_pc(m) : 0,
                     (unsigned long)ptr - il2cpp_base());
                mc++; lines++;
            }
        }
    }
    fout("== classes=%d ==\n", printed);
    fclose(g_fp); g_fp = NULL;
    out("dump ok classes=%d methods=%d -> %s\n", printed, lines, path);
    if (jfilter) (*env)->ReleaseStringUTFChars(env, jfilter, filter);
    if (jpath) (*env)->ReleaseStringUTFChars(env, jpath, path);
    return ret(env);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_listMethods(JNIEnv *env, jclass c, jstring jns, jstring jname) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) { out("class not found %s.%s\n", ns, nm); }
    else {
        fn_cls_methods cls_methods = (fn_cls_methods)sym("il2cpp_class_get_methods");
        fn_meth_name meth_name = (fn_meth_name)sym("il2cpp_method_get_name");
        fn_meth_pc meth_pc = (fn_meth_pc)sym("il2cpp_method_get_param_count");
        fn_meth_flags meth_flags = (fn_meth_flags)sym("il2cpp_method_get_flags");
        out("class %s.%s\n", ns, nm);
        void *iter = NULL;
        const MethodInfo *m;
        int mc = 0;
        while (cls_methods && (m = cls_methods(cl, &iter)) != NULL && mc < 400) {
            uint32_t iflags = 0;
            uint32_t fl = meth_flags ? meth_flags(m, &iflags) : 0;
            void *ptr = *(void **)m;
            out("  M %s/%u flags=0x%x rva=0x%lx ptr=0x%lx\n",
                meth_name ? meth_name(m) : "?", meth_pc ? meth_pc(m) : 0, fl,
                (unsigned long)ptr - il2cpp_base(), (unsigned long)ptr);
            mc++;
        }
        out("== methods %d ==\n", mc);
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    return ret(env);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_fields(JNIEnv *env, jclass c, jstring jns, jstring jname) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("class not found %s.%s\n", ns, nm);
    else {
        fn_cls_fields fields = (fn_cls_fields)sym("il2cpp_class_get_fields");
        fn_field_name fname = (fn_field_name)sym("il2cpp_field_get_name");
        fn_field_off foff = (fn_field_off)sym("il2cpp_field_get_offset");
        fn_field_type ftype = (fn_field_type)sym("il2cpp_field_get_type");
        fn_type_name tname = (fn_type_name)sym("il2cpp_type_get_name");
        out("fields of %s.%s\n", ns, nm);
        void *iter = NULL;
        void *f;
        int fc = 0;
        while (fields && (f = fields(cl, &iter)) != NULL && fc < 300) {
            const char *tn = (ftype && tname) ? tname(ftype(f)) : "?";
            out("  F %s off=%zu type=%s\n", fname ? fname(f) : "?", foff ? foff(f) : 0, tn ? tn : "?");
            fc++;
        }
        out("== fields %d ==\n", fc);
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    return ret(env);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_find(JNIEnv *env, jclass c, jstring jns, jstring jname,
                                jstring jmeth, jint pc) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("method not found %s.%s::%s(%d)\n", ns, nm, me, pc);
        else {
            void *ptr = *(void **)m;
            out("method %s.%s::%s(%d) info=0x%lx ptr=0x%lx rva=0x%lx\n", ns, nm, me, pc,
                (unsigned long)m, (unsigned long)ptr, (unsigned long)ptr - il2cpp_base());
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}

/* ---------- memory scan ---------- */
typedef struct { unsigned long start, end; } Region;

static int read_maps(Region **out) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { *out = NULL; return 0; }
    int cap = 8192, n = 0;
    Region *r = malloc(sizeof(Region) * cap);
    char line[512];
    while (fgets(line, sizeof(line), f) && n < cap) {
        unsigned long s, e;
        if (sscanf(line, "%lx-%lx", &s, &e) == 2) { r[n].start = s; r[n].end = e; n++; }
    }
    fclose(f);
    *out = r;
    return n;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_scan(JNIEnv *env, jclass c, jstring jneedle) {
    (void)c;
    const char *needle = jneedle ? (*env)->GetStringUTFChars(env, jneedle, NULL) : "http";
    Region *regs = NULL;
    int n = read_maps(&regs);
    size_t nl = strlen(needle);
    char seen[40][300];
    int nseen = 0;
    for (int i = 0; i < n && nseen < 40; i++) {
        unsigned long addr = regs[i].start;
        size_t chunk = 1 << 20;
        unsigned char *buf = malloc(chunk);
        while (addr < regs[i].end && nseen < 40) {
            size_t want = chunk;
            if (addr + want > regs[i].end) want = regs[i].end - addr;
            memcpy(buf, (void *)addr, want); /* 同进程，直接读；不可读区域会 SIGSEGV，命令须在启动后执行 */
            for (size_t o = 0; o + nl < want; o++) {
                if (memcmp(buf + o, needle, nl) != 0) continue;
                char tmp[300];
                size_t t = 0;
                for (size_t k = o; t < 250 && k < want; k++) {
                    unsigned char ch = buf[k];
                    if (ch < 32 || ch > 126) break;
                    tmp[t++] = (char)ch;
                }
                tmp[t] = 0;
                if (t > 12) {
                    int dup = 0;
                    for (int m = 0; m < nseen; m++) if (!strcmp(seen[m], tmp)) { dup = 1; break; }
                    if (!dup) { snprintf(seen[nseen++], 300, "%s", tmp); out("S %s\n", tmp); }
                }
                o += nl;
            }
            addr += want;
        }
        free(buf);
    }
    free(regs);
    out("== scan('%s') unique=%d ==\n", needle, nseen);
    if (jneedle) (*env)->ReleaseStringUTFChars(env, jneedle, needle);
    return ret(env);
}

/* ---------- method-pointer hook returning a fixed string ---------- */
static char g_hookval[1024];
static fn_string_new g_string_new;
static Il2CppString *hook_repl(void *self) {
    (void)self;
    if (!g_string_new) return NULL;
    return g_string_new(g_hookval);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookGet(JNIEnv *env, jclass c, jstring jns, jstring jname,
                                   jstring jmeth, jint pc, jstring jval) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    const char *val = jval ? (*env)->GetStringUTFChars(env, jval, NULL) : "";
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("method not found\n");
        else {
            g_string_new = (fn_string_new)sym("il2cpp_string_new");
            snprintf(g_hookval, sizeof(g_hookval), "%s", val);
            void *old = *(void **)m;
            *(void **)m = (void *)hook_repl;
            out("hookGet %s.%s::%s(%d) old=0x%lx new=0x%lx val='%s'\n", ns, nm, me, pc,
                (unsigned long)old, (unsigned long)hook_repl, g_hookval);
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    if (jval) (*env)->ReleaseStringUTFChars(env, jval, val);
    return ret(env);
}


/* ---------- 内联 hook + 日志 trampoline ---------- */
static void *g_tramp;
static char g_hookname_buf[128];
static const char *g_hookname;

static int relocate_insns(uint32_t *dst, const uint32_t *src, unsigned long oldpc, unsigned long newpc) {
    for (int i = 0; i < 4; i++) {
        uint32_t v = src[i];
        if ((v & 0x7C000000u) == 0x14000000u) return 0; /* B/BL */
        if ((v & 0x9F000000u) == 0x10000000u) return 0; /* ADR */
        if ((v & 0x3B000000u) == 0x18000000u) return 0; /* LDR literal */
        unsigned long opc = oldpc + (unsigned)i * 4, npc = newpc + (unsigned)i * 4;
        if ((v & 0x9F000000u) == 0x90000000u) { /* ADRP: 重定位页偏移 */
            int64_t immlo = (v >> 29) & 3;
            int64_t immhi = (v >> 5) & 0x7FFFF;
            int64_t imm = (immhi << 2) | immlo;
            if (imm & (1LL << 20)) imm -= (1LL << 21);
            uint64_t tgt = (opc & ~0xFFFULL) + ((uint64_t)imm << 12);
            int64_t nimm = ((int64_t)(tgt - (npc & ~0xFFFULL))) >> 12;
            if (nimm < -(1LL << 20) || nimm >= (1LL << 20)) return 0;
            dst[i] = (v & 0x9F00001Fu) | (((uint32_t)nimm & 3u) << 29)
                     | ((((uint32_t)(nimm >> 2)) & 0x7FFFFu) << 5);
        } else {
            dst[i] = v;
        }
    }
    return 1;
}

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
/* 在目标附近找一块空闲地址（±1GB 内），保证 ADRP 可重定位 */
static void *mmap_near(unsigned long target, size_t sz) {
    typedef struct { unsigned long s, e; } R;
    static R regs[4096];
    int n = 0;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return MAP_FAILED;
    char line[512];
    while (fgets(line, sizeof(line), f) && n < 4096) {
        unsigned long a, b;
        if (sscanf(line, "%lx-%lx", &a, &b) == 2) { regs[n].s = a; regs[n].e = b; n++; }
    }
    fclose(f);
    unsigned long lo = target > 0x40000000UL ? target - 0x40000000UL : 0x10000UL;
    unsigned long hi = target + 0x40000000UL;
    unsigned long step = 0x10000UL;
    for (int i = 0; i + 1 < n; i++) {
        unsigned long gapS = (regs[i].e + step - 1) & ~(step - 1);
        unsigned long gapE = regs[i + 1].s & ~(step - 1);
        if (gapE <= gapS) continue;
        if (gapS < lo) gapS = (lo + step - 1) & ~(step - 1);
        if (gapE > hi) gapE = hi;
        if (gapE <= gapS) continue;
        if (gapE - gapS < sz) continue;
        void *r = mmap((void *)gapS, sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (r != MAP_FAILED) {
            LOGI("mmap_near ok at %p (target=0x%lx)", r, target);
            return r;
        }
    }
    LOGI("mmap_near failed for target=0x%lx", target);
    return MAP_FAILED;
}

static void *make_tramp(unsigned long target) {
    uint32_t ins[4];
    memcpy(ins, (void *)target, 16);
    void *p = mmap_near(target, 4096);
    if (p == MAP_FAILED) return NULL;
    int rok = relocate_insns((uint32_t *)p, ins, target, (unsigned long)p);
    LOGI("make_tramp: p=%p target=0x%lx rok=%d", p, target, rok);
    if (!rok) { munmap(p, 4096); return NULL; }
    uint32_t ldr = 0x58000050u, brx = 0xD61F0200u;
    uint64_t back = target + 16;
    memcpy((char *)p + 16, &ldr, 4);
    memcpy((char *)p + 20, &brx, 4);
    memcpy((char *)p + 24, &back, 8);
    __builtin___clear_cache((char *)p, (char *)p + 32);
    return p;
}


/* 函数足够长才允许打 16 字节补丁：前 4 条指令里若出现 ret，说明函数太短，跳过 */
static int patchable_len(unsigned long target) {
    uint32_t w[4];
    memcpy(w, (void *)target, 16);
    for (int i = 0; i < 4; i++) {
        if (w[i] == 0xD65F03C0u) return 0;                  /* ret */
        if ((w[i] & 0xFFFFFC1Fu) == 0xD65F0000u) return 0;   /* ret xN */
        if (w[i] == 0x00000000u || w[i] == 0xFFFFFFFFu) return 0;
    }
    return 1;
}

static int patch_head(unsigned long target, void *repl) {
    if (!patchable_len(target)) { LOGI("skip patch: function too short at 0x%lx", target); return -2; }
    unsigned long page = target & ~0xFFFUL;
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    unsigned char stub[16];
    uint32_t i0 = 0x58000050, i1 = 0xD61F0200;
    memcpy(stub, &i0, 4); memcpy(stub + 4, &i1, 4); memcpy(stub + 8, &repl, 8);
    memcpy((void *)target, stub, 16);
    __builtin___clear_cache((char *)target, (char *)target + 16);
    mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
    return 0;
}


/* ---- 安全的可读性检查：绝不盲目解引用 ---- */
static int is_readable(uintptr_t addr, size_t len) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long a, b; char perm[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perm) != 3) continue;
        if (perm[0] != 'r') continue;
        if (addr >= a && addr + len <= b) { ok = 1; break; }
    }
    fclose(f);
    return ok;
}

/* 只把“确实可读 + 长度合理 + 内容像字符串”的指针当字符串 */
static int try_str(void *p, char *out, int max, int *outlen) {
    uintptr_t v = (uintptr_t)p;
    if (!v || (v & 7) != 0) return 0;
    if (!is_readable(v, 24)) return 0;
    int len = *(int *)((char *)p + 16);
    if (len <= 0 || len > 3000) return 0;
    if (!is_readable(v + 20, (size_t)len * 2)) return 0;
    const uint16_t *ch = (const uint16_t *)((char *)p + 20);
    int o = 0, printable = 0;
    for (int i = 0; i < len && o < max - 4; i++) {
        uint16_t c = ch[i];
        if (c >= 0x20 && c < 0x7F) { out[o++] = (char)c; printable++; }
        else if (c < 0x80) { out[o++] = (char)c; }
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); printable++; }
        else { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); printable++; }
    }
    out[o] = 0;
    if (printable < len / 2) return 0;
    *outlen = len;
    return o > 0;
}

static int looks_like_string(void *p) { return p && ((uintptr_t)p & 7) == 0; }

static void log_arg(const char *tag, void *a) {
    if (!a) { LOGI("%s null", tag); return; }
    char buf[4096]; int len = 0;
    if (try_str(a, buf, sizeof buf, &len)) { LOGI("%s str(len=%d) = %s", tag, len, buf); return; }
    LOGI("%s = %p", tag, a);
}

static void *g_self_ret;
static void *hooklog_thunk(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) {
    LOGI("CALL %s", g_hookname ? g_hookname : "?");
    log_arg("  a1", b);
    log_arg("  a2", c);
    void *r = ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_tramp)(a, b, c, d, e, f, g, h);
    log_arg("  ret", r);
    return r;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookLog(JNIEnv *env, jclass c, jstring jns, jstring jname,
                                   jstring jmeth, jint pc) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("class not found\n");
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("method not found\n");
        else {
            unsigned long target = (unsigned long)*(void **)m;
            out("hookLog %s.%s::%s(%d) target=0x%lx rva=0x%lx\n", ns, nm, me, pc,
                target, target - il2cpp_base());
            g_tramp = make_tramp(target);
            if (!g_tramp) out("prologue not relocatable\n");
            else {
                snprintf(g_hookname_buf, sizeof g_hookname_buf, "%s", me);
            g_hookname = g_hookname_buf;
                int rc = patch_head(target, (void *)hooklog_thunk);
                out("hook installed rc=%d tramp=%p\n", rc, g_tramp);
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}



JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookRva(JNIEnv *env, jclass c, jstring jhex) {
    (void)c;
    const char *hx = jhex ? (*env)->GetStringUTFChars(env, jhex, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    out("hookRva rva=0x%lx target=0x%lx\n", rva, target);
    if (!il2cpp_base()) { out("il2cpp not mapped\n"); }
    else {
        g_tramp = make_tramp(target);
        if (!g_tramp) out("trampoline failed\n");
        else {
            g_hookname = "rva";
            int rc = patch_head(target, (void *)hooklog_thunk);
            out("hook installed rc=%d tramp=%p\n", rc, g_tramp);
        }
    }
    if (jhex) (*env)->ReleaseStringUTFChars(env, jhex, hx);
    return ret(env);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_trampTest(JNIEnv *env, jclass c, jstring jhex) {
    (void)c;
    const char *hx = jhex ? (*env)->GetStringUTFChars(env, jhex, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    void *t = make_tramp(target);
    out("trampTest rva=0x%lx target=0x%lx tramp=%p ok=%d\n", rva, target, t, t != NULL);
    if (jhex) (*env)->ReleaseStringUTFChars(env, jhex, hx);
    return ret(env);
}


/* ================= 补回被误删的命令 ================= */


/* --- 返回固定字符串 --- */
static char g_ret_val[512];
static Il2CppString *ret_hook(void *self) {
    (void)self;
    if (!g_string_new) g_string_new = (fn_string_new)sym("il2cpp_string_new");
    return g_string_new ? g_string_new(g_ret_val) : NULL;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookRet(JNIEnv *env, jclass c, jstring jrva, jstring jval) {
    (void)c;
    const char *hx = jrva ? (*env)->GetStringUTFChars(env, jrva, NULL) : "0";
    const char *val = jval ? (*env)->GetStringUTFChars(env, jval, NULL) : "";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    if (!il2cpp_base()) out("hookRet: il2cpp not mapped\n");
    else {
        snprintf(g_ret_val, sizeof(g_ret_val), "%s", val);
        int rc = patch_head(target, (void *)ret_hook);
        out("hookRet rva=0x%lx target=0x%lx rc=%d value=%s\n", rva, target, rc, g_ret_val);
    }
    if (jrva) (*env)->ReleaseStringUTFChars(env, jrva, hx);
    if (jval) (*env)->ReleaseStringUTFChars(env, jval, val);
    return ret(env);
}

/* --- 返回固定整数 --- */
static int g_ret_int;
static int ret_int_fn(void) { return g_ret_int; }

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookInt(JNIEnv *env, jclass c, jstring jrva, jint val) {
    (void)c;
    const char *hx = jrva ? (*env)->GetStringUTFChars(env, jrva, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    if (!il2cpp_base()) out("hookInt: il2cpp not mapped\n");
    else {
        g_ret_int = val;
        int rc = patch_head(target, (void *)ret_int_fn);
        out("hookInt rva=0x%lx target=0x%lx val=%d rc=%d\n", rva, target, val, rc);
    }
    if (jrva) (*env)->ReleaseStringUTFChars(env, jrva, hx);
    return ret(env);
}

/* --- 读 NetworkConfig 单例 --- */
JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_netcfg(JNIEnv *env, jclass c) {
    (void)c;
    typedef void *(*fn_getf)(void *, const char *);
    typedef void (*fn_sget)(void *, void *);
    typedef void (*fn_fget)(void *, void *, void *);
    typedef void *(*fn_oc)(void *);
    typedef void *(*fn_fields)(void *, void **);
    typedef const char *(*fn_fname)(void *);
    typedef void *(*fn_parent)(void *);
    fn_getf getf = (fn_getf)sym("il2cpp_class_get_field_from_name");
    fn_sget sget = (fn_sget)sym("il2cpp_field_static_get_value");
    fn_fget fget = (fn_fget)sym("il2cpp_field_get_value");
    fn_oc objcls = (fn_oc)sym("il2cpp_object_get_class");
    fn_fields fields = (fn_fields)sym("il2cpp_class_get_fields");
    fn_fname fname = (fn_fname)sym("il2cpp_field_get_name");
    fn_parent parent = (fn_parent)sym("il2cpp_class_get_parent");
    Il2CppClass *cl = find_class("Torappu.Config", "NetworkConfig");
    if (!cl || !getf || !sget || !fget || !objcls || !fields) { out("netcfg: missing\n"); return ret(env); }
    void *inst = NULL;
    for (void *k = cl; k; k = parent ? parent(k) : NULL) {
        void *sf = getf(k, "s_instance");
        if (sf) { sget(sf, &inst); if (inst) break; }
    }
    if (!inst) { out("netcfg: s_instance null\n"); return ret(env); }
    void *df = getf((void *)objcls(inst), "<data>k__BackingField");
    if (!df) { out("netcfg: no data\n"); return ret(env); }
    void *data = NULL;
    fget(inst, df, &data);
    if (!data) { out("netcfg: data null\n"); return ret(env); }
    void *dcls = objcls(data);
    out("NETCFG data=%p\n", data);
    void *iter = NULL; void *f; int n = 0;
    while ((f = fields((void *)dcls, &iter)) != NULL && n < 80) {
        const char *nm = fname ? fname(f) : "?";
        Il2CppString *sv = NULL;
        fget(data, f, &sv);
        if (sv && (uintptr_t)sv > 0x10000 && ((uintptr_t)sv & 7) == 0) {
            int len = *(int *)((char *)sv + 16);
            if (len >= 0 && len < 512) {
                char buf[1100]; int o = 0;
                const uint16_t *ch = (const uint16_t *)((char *)sv + 20);
                for (int i = 0; i < len && o < 1000; i++) buf[o++] = (ch[i] < 0x80) ? (char)ch[i] : '?';
                buf[o] = 0;
                if (o > 0) { out("  %s = %s\n", nm, buf); n++; continue; }
            }
        }
        out("  %s = <%p>\n", nm, (void *)sv);
        n++;
    }
    out("== netcfg %d ==\n", n);
    return ret(env);
}

/* --- 通用打点 --- */
static void *g_old[4];
static const char *g_tname[4];

static void log_maybe_str(const char *tag, void *p) {
    if (!p) { LOGI("%s=null", tag); return; }
    char buf[4096]; int len = 0;
    if (try_str(p, buf, sizeof buf, &len)) { LOGI("%s(len=%d)=%s", tag, len, buf); return; }
    LOGI("%s=%p", tag, p);
}

#define DEFWRAP(I) \
static void *wrap##I(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) { \
    LOGI("TAP%d %s", I, g_tname[I] ? g_tname[I] : "?"); \
    log_maybe_str("  a0", a); log_maybe_str("  a1", b); \
    void *r = ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_old[I])(a, b, c, d, e, f, g, h); \
    LOGI("TAP%d ret=%p", I, r); \
    return r; \
}
DEFWRAP(0)
DEFWRAP(1)
DEFWRAP(2)
DEFWRAP(3)
static void *g_wraps[4] = { (void *)wrap0, (void *)wrap1, (void *)wrap2, (void *)wrap3 };

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_tap(JNIEnv *env, jclass c, jint idx, jstring jns, jstring jname,
                               jstring jmeth, jint pc) {
    (void)c;
    if (idx < 0 || idx > 3) { out("tap idx 0..3\n"); return ret(env); }
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("tap: class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("tap: method not found\n");
        else {
            g_old[idx] = *(void **)m;
            g_tname[idx] = me;
            *(void **)m = g_wraps[idx];
            out("tap%d %s::%s ok old=%p\n", idx, nm, me, g_old[idx]);
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}



/* ---- 改写 Networker._ParseServiceUrl 返回的 URL 主机 ---- */
static void *g_parse_tramp;
static int g_parse_on;
#ifndef AK_SERVER_URL
#define AK_SERVER_URL "http://127.0.0.1:10029"
#endif
#define PRIV AK_SERVER_URL

static int s2u8(Il2CppString *s, char *out, int max) {
    if (!s) return 0;
    char *p = (char *)s;
    int len = *(int *)(p + 16);
    if (len < 0 || len > 4096) return 0;
    const uint16_t *ch = (const uint16_t *)(p + 20);
    int o = 0;
    for (int i = 0; i < len && o < max - 4; i++) {
        uint16_t c = ch[i];
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return o;
}

static const char *HOSTS[] = { "ak-gs-gf.hypergryph.com", "as.hypergryph.com", "ak-gs.hypergryph.com" };

static Il2CppString *my_parse(void *self, void *a1, void *a2) {
    Il2CppString *(*orig)(void *, void *, void *) = (Il2CppString *(*)(void *, void *, void *))g_parse_tramp;
    Il2CppString *r = orig(self, a1, a2);
    if (!g_parse_on || !r || !g_string_new) return r;
    char buf[2048];
    if (!s2u8(r, buf, sizeof buf)) return r;
    for (int i = 0; i < 3; i++) {
        char *hit = strstr(buf, HOSTS[i]);
        if (!hit) continue;
        char *scheme = hit;
        if (scheme - 8 >= buf && !strncmp(scheme - 8, "https://", 8)) scheme -= 8;
        else if (scheme - 7 >= buf && !strncmp(scheme - 7, "http://", 7)) scheme -= 7;
        else continue;
        char out[2304];
        int pre = (int)(scheme - buf);
        snprintf(out, sizeof out, "%.*s%s%s", pre, buf, PRIV, hit + strlen(HOSTS[i]));
        LOGI("URLREWRITE %s -> %s", buf, out);
        Il2CppString *ns = g_string_new(out);
        return ns ? ns : r;
    }
    return r;
}

static void *parse_thunk(void *a, void *b, void *c) {
    return (void *)my_parse(a, b, c);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookParse(JNIEnv *env, jclass c, jstring jrva, jint on) {
    (void)c;
    const char *hx = jrva ? (*env)->GetStringUTFChars(env, jrva, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    g_parse_on = on;
    if (!il2cpp_base()) out("hookParse: not mapped\n");
    else if (!g_parse_tramp) {
        g_string_new = (fn_string_new)sym("il2cpp_string_new");
        g_parse_tramp = make_tramp(target);
        if (!g_parse_tramp) out("hookParse: trampoline failed\n");
        else {
            int rc = patch_head(target, (void *)parse_thunk);
            out("hookParse rva=0x%lx tramp=%p rc=%d on=%d\n", rva, g_parse_tramp, rc, on);
        }
    } else out("hookParse already installed, on=%d\n", on);
    if (jrva) (*env)->ReleaseStringUTFChars(env, jrva, hx);
    return ret(env);
}


/* 8 参数日志直通（先记参数，再原样调用） */
static void *g_tramp8;
static const char *g_name8;
static void *log8_thunk(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) {
    LOGI("H8 %s a0=%p a1=%p", g_name8 ? g_name8 : "?", a, b);
    log_maybe_str("  a0s", a);
    log_maybe_str("  a1s", b);
    void *r = ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_tramp8)(a, b, c, d, e, f, g, h);
    log_maybe_str("  rets", r);
    return r;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookLog8(JNIEnv *env, jclass c, jstring jrva) {
    (void)c;
    const char *hx = jrva ? (*env)->GetStringUTFChars(env, jrva, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    if (!il2cpp_base()) out("hookLog8: not mapped\n");
    else {
        g_tramp8 = make_tramp(target);
        if (!g_tramp8) out("hookLog8: tramp failed\n");
        else {
            g_name8 = "h8";
            int rc = patch_head(target, (void *)log8_thunk);
            out("hookLog8 rva=0x%lx tramp=%p rc=%d\n", rva, g_tramp8, rc);
        }
    }
    if (jrva) (*env)->ReleaseStringUTFChars(env, jrva, hx);
    return ret(env);
}


/* ---- 解析完配置后，同步改写 gateway 字段 ---- */
static void *g_setresp_tramp;
static int g_sr_on = 1;

static int set_str_field(void *obj, void *field, const char *val) {
    typedef void (*fn_set)(void *, void *, void *);
    fn_set fset = (fn_set)sym("il2cpp_field_set_value");
    if (!fset || !g_string_new) return 0;
    Il2CppString *ns = g_string_new(val);
    fset(obj, field, &ns);
    return 1;
}

static void patch_cfg_from(void *self) {
    typedef void *(*fn_oc)(void *);
    typedef void *(*fn_gf)(void *, const char *);
    typedef void (*fn_fg)(void *, void *, void *);
    typedef const char *(*fn_ftn)(void *);
    typedef void *(*fn_ft)(void *);
    fn_oc objcls = (fn_oc)sym("il2cpp_object_get_class");
    fn_gf getf = (fn_gf)sym("il2cpp_class_get_field_from_name");
    fn_fg fget = (fn_fg)sym("il2cpp_field_get_value");
    fn_ft ftype = (fn_ft)sym("il2cpp_field_get_type");
    fn_ftn tname = (fn_ftn)sym("il2cpp_type_get_name");
    if (!objcls || !getf || !fget || !self) return;
    void *df = getf((void *)objcls(self), "<data>k__BackingField");
    if (!df) return;
    void *data = NULL;
    fget(self, df, &data);
    if (!data) return;
    void *dcls = objcls(data);
    static const struct { const char *n; const char *v; } tab[] = {
        {"gameServerUrl", PRIV},
        {"sdkServerUrl", PRIV},
        {"u8ServerUrl", PRIV "/u8"},
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        void *f = getf((void *)dcls, tab[i].n);
        if (!f) continue;
        if (ftype && tname) {
            const char *tn = tname(ftype(f));
            if (!tn || strcmp(tn, "System.String") != 0) continue;
        }
        if (set_str_field(data, f, tab[i].v)) n++;
    }
    LOGI("SETRESP patched %d fields", n);
}

static void *g_pending_self;
static volatile int g_patched_done;
static pthread_t g_patch_thread;
static int g_patch_started;

static void *deferred_patch(void *arg) {
    (void)arg;
    for (int i = 0; i < 40; i++) {
        usleep(100000);
        if (!g_pending_self) continue;
        if (g_patched_done) continue;
        g_patched_done = 1;
        void *self = g_pending_self;
        patch_cfg_from(self);
        break;
    }
    return NULL;
}

static void *setresp_thunk(void *self, void *json) {
    void (*orig)(void *, void *) = (void (*)(void *, void *))g_setresp_tramp;
    orig(self, json);
    if (g_sr_on) {
        g_pending_self = self;
        if (!g_patch_started) {
            g_patch_started = 1;
            pthread_create(&g_patch_thread, NULL, deferred_patch, NULL);
            pthread_detach(g_patch_thread);
        }
    }
    return NULL;
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookSetResp(JNIEnv *env, jclass c, jstring jrva, jint on) {
    (void)c;
    const char *hx = jrva ? (*env)->GetStringUTFChars(env, jrva, NULL) : "0";
    unsigned long rva = strtoul(hx, NULL, 0);
    unsigned long target = il2cpp_base() + rva;
    g_sr_on = on;
    if (!il2cpp_base()) out("hookSetResp: not mapped\n");
    else if (!g_setresp_tramp) {
        g_string_new = (fn_string_new)sym("il2cpp_string_new");
        g_setresp_tramp = make_tramp(target);
        if (!g_setresp_tramp) out("hookSetResp: tramp failed\n");
        else {
            int rc = patch_head(target, (void *)setresp_thunk);
            out("hookSetResp rva=0x%lx tramp=%p rc=%d\n", rva, g_setresp_tramp, rc);
        }
    } else out("hookSetResp already, on=%d\n", on);
    if (jrva) (*env)->ReleaseStringUTFChars(env, jrva, hx);
    return ret(env);
}


/* ---- 改写 SendPost 等 C# 网络调用的 URL 参数 ---- */
static const char *URL_HOSTS[] = {
    "as.hypergryph.com", "ak-gs-gf.hypergryph.com", "ak-gs.hypergryph.com",
    "ak-conf.hypergryph.com", "user.hypergryph.com", "launcher.hypergryph.com",
    "game-config.hypergryph.com", "ak.hypergryph.com", "ak-webview.hypergryph.com"
};
static void *g_url_tramp;
static int g_url_on = 1;

static Il2CppString *rewrite_url(Il2CppString *s) {
    if (!s || !g_string_new) return s;
    char buf[2048];
    int len = 0;
    if (!try_str(s, buf, sizeof buf, &len)) return s;
    char *p = buf;
    if (!strncmp(p, "https://", 8)) p += 8;
    else if (!strncmp(p, "http://", 7)) p += 7;
    else return s;    /* 相对路径，不动 */
    for (unsigned i = 0; i < sizeof(URL_HOSTS)/sizeof(URL_HOSTS[0]); i++) {
        size_t hl = strlen(URL_HOSTS[i]);
        if (strncmp(p, URL_HOSTS[i], hl) != 0) continue;
        char out[2304];
        snprintf(out, sizeof out, "%s%s", PRIV, p + hl);
        LOGI("CSHARP REWRITE %s -> %s", buf, out);
        Il2CppString *ns2 = g_string_new(out);
        return ns2 ? ns2 : s;
    }
    return s;
}

static void *url_thunk(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) {
    if (g_url_on) b = (void *)rewrite_url((Il2CppString *)b);
    return ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_url_tramp)(a, b, c, d, e, f, g, h);
}

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookUrl(JNIEnv *env, jclass c, jstring jns, jstring jname,
                                   jstring jmeth, jint pc, jint on) {
    (void)c;
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    g_url_on = on;
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("hookUrl: class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("hookUrl: method not found\n");
        else {
            unsigned long target = (unsigned long)*(void **)m;
            g_string_new = (fn_string_new)sym("il2cpp_string_new");
            void *t = make_tramp(target);
            if (!t) out("hookUrl: tramp failed\n");
            else {
                g_url_tramp = t;
                int rc = patch_head(target, (void *)url_thunk);
                out("hookUrl %s::%s rva=0x%lx tramp=%p rc=%d on=%d\n", nm, me,
                    target - il2cpp_base(), t, rc, on);
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}


/* ---- 4 个代码补丁槽位，可同时追踪多个方法 ---- */
static void *g_cs_tramp[4];
static char g_cs_name[4][80];

#define DEFCS(I) \
static void *cs##I(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) { \
    LOGI("CS%d %s", I, g_cs_name[I]); \
    log_arg("  b", b); \
    void *r = ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_cs_tramp[I])(a, b, c, d, e, f, g, h); \
    log_arg("  r", r); \
    return r; \
}
DEFCS(0)
DEFCS(1)
DEFCS(2)
DEFCS(3)
static void *g_cs_thunks[4] = { (void *)cs0, (void *)cs1, (void *)cs2, (void *)cs3 };

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookSlot(JNIEnv *env, jclass c, jint idx, jstring jns, jstring jname,
                                    jstring jmeth, jint pc) {
    (void)c;
    if (idx < 0 || idx > 3) { out("slot 0..3\n"); return ret(env); }
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("hookSlot: class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("hookSlot: method not found %s\n", me);
        else {
            unsigned long target = (unsigned long)*(void **)m;
            void *t = make_tramp(target);
            if (!t) out("hookSlot: tramp failed %s\n", me);
            else {
                g_cs_tramp[idx] = t;
                snprintf(g_cs_name[idx], sizeof g_cs_name[idx], "%s", me);
                int rc = patch_head(target, g_cs_thunks[idx]);
                out("slot%d %s::%s rva=0x%lx rc=%d\n", idx, nm, me, target - il2cpp_base(), rc);
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}


/* ---- 多槽位 URL 改写（每个槽位独立跳板） ---- */
static void *g_url_tramp_s[4];
static int g_url_on_s[4];

#define DEFURL(I) \
static void *urlthunk##I(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) { \
    if (g_url_on_s[I]) b = (void *)rewrite_url((Il2CppString *)b); \
    return ((void *(*)(void *, void *, void *, void *, void *, void *, void *, void *))g_url_tramp_s[I])(a, b, c, d, e, f, g, h); \
}
DEFURL(0)
DEFURL(1)
DEFURL(2)
DEFURL(3)
static void *g_url_thunks[4] = { (void *)urlthunk0, (void *)urlthunk1, (void *)urlthunk2, (void *)urlthunk3 };

JNIEXPORT jstring JNICALL
Java_com_akpatch_LabNative_hookUrlSlot(JNIEnv *env, jclass c, jint idx, jstring jns, jstring jname,
                                       jstring jmeth, jint pc, jint on) {
    (void)c;
    if (idx < 0 || idx > 3) { out("urlslot 0..3\n"); return ret(env); }
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    g_url_on_s[idx] = on;
    g_string_new = (fn_string_new)sym("il2cpp_string_new");
    Il2CppClass *cl = find_class(ns, nm);
    if (!cl) out("hookUrlSlot: class not found %s.%s\n", ns, nm);
    else {
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        const MethodInfo *m = gm ? gm(cl, me, pc) : NULL;
        if (!m) out("hookUrlSlot: method not found %s\n", me);
        else {
            unsigned long target = (unsigned long)*(void **)m;
            void *t = make_tramp(target);
            if (!t) out("hookUrlSlot: tramp failed %s\n", me);
            else {
                g_url_tramp_s[idx] = t;
                int rc = patch_head(target, g_url_thunks[idx]);
                out("urlslot%d %s::%s rva=0x%lx tramp=%p rc=%d\n", idx, nm, me,
                    target - il2cpp_base(), t, rc);
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    return ret(env);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm; (void)reserved;
    g_out = malloc(g_cap);
    if (g_out) g_out[0] = 0;
    LOGI("liblab loaded base=0x%lx", il2cpp_base());
    return JNI_VERSION_1_6;
}
