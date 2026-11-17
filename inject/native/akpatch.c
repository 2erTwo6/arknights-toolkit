#define _GNU_SOURCE
#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>

#define TAG "AKPATCH-N"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

typedef struct { unsigned long start, end; int w; } Region;

static int collect_strings(const char *needle, int wide, char out[][256], int maxout);
static int read_maps(Region **out) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    int cap = 4096, n = 0;
    Region *r = malloc(sizeof(Region) * cap);
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long s, e; char perms[8]; char rest[400];
        rest[0] = 0;
        int got = sscanf(line, "%lx-%lx %7s %*s %*s %*s %399[^\n]", &s, &e, perms, rest);
        if (got < 3) continue;
        if (perms[0] != 'r') continue;
        if (e - s > (512UL << 20)) continue;
        if (strstr(rest, ".apk") || strstr(rest, "/system/") || strstr(rest, "/apex/")) continue;
        if (n == cap) { cap *= 2; r = realloc(r, sizeof(Region) * cap); }
        r[n].start = s; r[n].end = e; r[n].w = (perms[1] == 'w');
        n++;
    }
    fclose(f);
    *out = r;
    return n;
}

static int patch_all(const unsigned char *pat, size_t plen, const unsigned char *rep, size_t rlen,
                     int do_patch, int wide) {
    Region *regs = NULL;
    int n = read_maps(&regs);
    if (!n) return 0;
    int hits = 0;
    pid_t pid = getpid();
    size_t chunk = 1 << 20;
    unsigned char *buf = malloc(chunk);
    const size_t step = wide ? 2 : 1;
    for (int i = 0; i < n; i++) {
        unsigned long addr = regs[i].start;
        while (addr < regs[i].end) {
            size_t want = chunk;
            if (addr + want > regs[i].end) want = regs[i].end - addr;
            struct iovec local = { buf, want };
            struct iovec remote = { (void *)addr, want };
            ssize_t got = process_vm_readv(pid, &local, 1, &remote, 1, 0);
            if (got <= 0) { addr += want; continue; }
            for (ssize_t o = 0; (size_t)o + plen <= (size_t)got; o += step) {
                if (memcmp(buf + o, pat, plen) != 0) continue;
                unsigned long at = addr + o;
                LOGI("hit %s @0x%lx (region 0x%lx-0x%lx w=%d)", wide ? "u16" : "u8", at,
                     regs[i].start, regs[i].end, regs[i].w);
                if (do_patch) {
                    struct iovec l2 = { (void *)rep, rlen };
                    struct iovec r2 = { (void *)at, rlen };
                    ssize_t wr = process_vm_writev(pid, &l2, 1, &r2, 1, 0);
                    if (wr == (ssize_t)rlen) { hits++; LOGI("patched @0x%lx", at); }
                    else LOGI("write failed @0x%lx: %zd", at, wr);
                } else hits++;
                o += plen - step;
            }
            if ((size_t)got < want) addr += want; else addr += (want > 4096 ? want - 4096 : want);
        }
    }
    free(buf); free(regs);
    return hits;
}

static jbyteArray to_utf16le(JNIEnv *env, const char *utf8, int *outlen) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    // count chars (assume ascii for our use case)
    n = strlen(utf8);
    jbyteArray arr = (*env)->NewByteArray(env, n * 2);
    jbyte *b = (*env)->GetByteArrayElements(env, arr, NULL);
    for (int i = 0; i < n; i++) { b[i*2] = (jbyte)p[i]; b[i*2+1] = 0; }
    (*env)->ReleaseByteArrayElements(env, arr, b, 0);
    *outlen = n * 2;
    return arr;
}

JNIEXPORT jint JNICALL
Java_com_akpatch_Native_patchUrl(JNIEnv *env, jclass clz, jstring jold, jstring jnew, jboolean wide, jboolean doPatch) {
    const char *old = (*env)->GetStringUTFChars(env, jold, NULL);
    const char *nw  = (*env)->GetStringUTFChars(env, jnew, NULL);
    int hits = 0;
    if (wide) {
        int ol = 0, nl = 0;
        jbyteArray oa = to_utf16le(env, old, &ol);
        jbyteArray na = to_utf16le(env, nw, &nl);
        jbyte *ob = (*env)->GetByteArrayElements(env, oa, NULL);
        jbyte *nb = (*env)->GetByteArrayElements(env, na, NULL);
        if (ol == nl) hits = patch_all((unsigned char*)ob, ol, (unsigned char*)nb, nl, doPatch, 1);
        (*env)->ReleaseByteArrayElements(env, oa, ob, JNI_ABORT);
        (*env)->ReleaseByteArrayElements(env, na, nb, JNI_ABORT);
        (*env)->DeleteLocalRef(env, oa); (*env)->DeleteLocalRef(env, na);
    } else {
        size_t ol = strlen(old), nl = strlen(nw);
        if (ol == nl) hits = patch_all((unsigned char*)old, ol, (unsigned char*)nw, nl, doPatch, 0);
    }
    LOGI("patchUrl wide=%d patch=%d hits=%d", wide, doPatch, hits);
    (*env)->ReleaseStringUTFChars(env, jold, old);
    (*env)->ReleaseStringUTFChars(env, jnew, nw);
    return hits;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOGI("libakpatch loaded");
    return JNI_VERSION_1_6;
}

/* ---------------- il2cpp introspection ---------------- */
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdint.h>

typedef struct Il2CppDomain Il2CppDomain;
typedef struct Il2CppAssembly Il2CppAssembly;
typedef struct Il2CppImage Il2CppImage;
typedef struct Il2CppClass Il2CppClass;
typedef struct MethodInfo MethodInfo;
typedef struct Il2CppString Il2CppString;

static void *sym(const char *n) {
    static void *h = NULL;
    if (!h) {
        h = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!h) return NULL;   /* never load it ourselves */
    }
    return h ? dlsym(h, n) : NULL;
}

#define GETSYM(type, var, name) type var = (type)sym(name); if (!var) { LOGI("missing sym %s", name); return -1; }

static unsigned long g_il2cpp_base = 0;
static int cb_phdr(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "libil2cpp.so")) {
        g_il2cpp_base = (unsigned long)info->dlpi_addr;
    }
    return 0;
}
static unsigned long il2cpp_base(void) {
    if (!g_il2cpp_base) { g_il2cpp_base = 0; dl_iterate_phdr(cb_phdr, NULL); }
    return g_il2cpp_base;
}

JNIEXPORT jint JNICALL
Java_com_akpatch_Native_listTypes(JNIEnv *env, jclass clz, jstring jfilter) {
    const char *filter = jfilter ? (*env)->GetStringUTFChars(env, jfilter, NULL) : "";
    typedef const Il2CppAssembly **(*fn_get_asm)(const Il2CppDomain *, size_t *);
    typedef Il2CppDomain *(*fn_domain_get)(void);
    typedef Il2CppImage *(*fn_asm_img)(const Il2CppAssembly *);
    typedef size_t (*fn_img_cnt)(const Il2CppImage *);
    typedef const Il2CppClass *(*fn_img_cls)(const Il2CppImage *, size_t);
    typedef const char *(*fn_cls_name)(const Il2CppClass *);
    typedef const char *(*fn_cls_ns)(const Il2CppClass *);
    typedef const MethodInfo *(*fn_cls_meth)(Il2CppClass *, void **);
    typedef const char *(*fn_meth_name)(const MethodInfo *);
    typedef uint32_t (*fn_meth_pc)(const MethodInfo *);
    typedef Il2CppDomain *(*fn_thread_attach)(Il2CppDomain *);

    if (!sym("il2cpp_domain_get")) return -3;   /* il2cpp not loaded yet */
    GETSYM(fn_domain_get, domain_get, "il2cpp_domain_get");
    GETSYM(fn_thread_attach, thread_attach, "il2cpp_thread_attach");
    GETSYM(fn_get_asm, get_asm, "il2cpp_domain_get_assemblies");
    GETSYM(fn_asm_img, asm_img, "il2cpp_assembly_get_image");
    GETSYM(fn_img_cnt, img_cnt, "il2cpp_image_get_class_count");
    GETSYM(fn_img_cls, img_cls, "il2cpp_image_get_class");
    GETSYM(fn_cls_name, cls_name, "il2cpp_class_get_name");
    GETSYM(fn_cls_ns, cls_ns, "il2cpp_class_get_namespace");
    GETSYM(fn_cls_meth, cls_meth, "il2cpp_class_get_methods");
    GETSYM(fn_meth_name, meth_name, "il2cpp_method_get_name");
    GETSYM(fn_meth_pc, meth_pc, "il2cpp_method_get_param_count");

    Il2CppDomain *dom = domain_get();
    if (!dom) return -2;   /* runtime not ready */
    thread_attach(dom);
    LOGI("il2cpp_base=0x%lx", il2cpp_base());
    size_t n = 0;
    const Il2CppAssembly **asms = get_asm(dom, &n);
    LOGI("assemblies=%zu filter=%s", n, filter);
    int printed = 0;
    for (size_t i = 0; i < n && printed < 300; i++) {
        Il2CppImage *img = asm_img(asms[i]);
        if (!img) continue;
        size_t cc = img_cnt(img);
        for (size_t j = 0; j < cc && printed < 300; j++) {
            const Il2CppClass *c = img_cls(img, j);
            if (!c) continue;
            const char *nm = cls_name(c);
            const char *ns = cls_ns(c);
            if (!nm) continue;
            char full[300];
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", nm);
            if (filter[0] && !strstr(full, filter)) continue;
            printed++;
            LOGI("CLASS %s", full);
            void *iter = NULL;
            const MethodInfo *m;
            int mc = 0;
            while ((m = cls_meth((Il2CppClass *)c, &iter)) != NULL && mc < 60) {
                const char *mn = meth_name(m);
                void *ptr = *(void **)m;
                LOGI("   M %s/%u -> %p", mn ? mn : "?", meth_pc(m), ptr);
                mc++;
            }
        }
    }
    LOGI("dump done printed=%d", printed);
    if (jfilter) (*env)->ReleaseStringUTFChars(env, jfilter, filter);
    return printed;
}

JNIEXPORT jint JNICALL
Java_com_akpatch_Native_classInfo(JNIEnv *env, jclass clz, jstring jns, jstring jname) {
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    typedef Il2CppDomain *(*fn_dom)(void);
    typedef Il2CppDomain *(*fn_attach)(Il2CppDomain *);
    typedef const Il2CppAssembly **(*fn_asms)(const Il2CppDomain *, size_t *);
    typedef Il2CppImage *(*fn_img)(const Il2CppAssembly *);
    typedef Il2CppClass *(*fn_cfn)(const Il2CppImage *, const char *, const char *);
    typedef Il2CppClass *(*fn_parent)(Il2CppClass *);
    typedef const char *(*fn_cname)(const Il2CppClass *);
    typedef const char *(*fn_cns)(const Il2CppClass *);
    typedef void *(*fn_fields)(Il2CppClass *, void **);
    typedef const char *(*fn_fname)(void *);
    fn_dom dom_get = (fn_dom)sym("il2cpp_domain_get");
    fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");
    fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");
    fn_img img_of = (fn_img)sym("il2cpp_assembly_get_image");
    fn_cfn cfn = (fn_cfn)sym("il2cpp_class_from_name");
    fn_parent parent = (fn_parent)sym("il2cpp_class_get_parent");
    fn_cname cname = (fn_cname)sym("il2cpp_class_get_name");
    fn_cns cns = (fn_cns)sym("il2cpp_class_get_namespace");
    fn_fields fields = (fn_fields)sym("il2cpp_class_get_fields");
    fn_fname fname = (fn_fname)sym("il2cpp_field_get_name");
    Il2CppDomain *dom = dom_get ? dom_get() : NULL;
    if (!dom || !asms || !cfn) return -2;
    if (attach) attach(dom);
    size_t n = 0; const Il2CppAssembly **a = asms(dom, &n);
    Il2CppClass *c = NULL;
    for (size_t k = 0; k < n && !c; k++) {
        Il2CppImage *im = img_of ? img_of(a[k]) : NULL;
        if (im) c = cfn(im, ns, nm);
    }
    if (!c) { LOGI("classInfo: not found"); return 0; }
    for (int depth = 0; c && depth < 6; depth++) {
        LOGI("CHAIN %s.%s", cns ? cns(c) : "?", cname ? cname(c) : "?");
        void *iter = NULL; void *f;
        int fc = 0;
        while (fields && (f = fields((Il2CppClass *)c, &iter)) != NULL && fc < 80) {
            LOGI("   CF %s", fname ? fname(f) : "?");
            fc++;
        }
        c = parent ? parent((Il2CppClass *)c) : NULL;
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_akpatch_Native_listFields(JNIEnv *env, jclass clz, jstring jns, jstring jname) {
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    typedef Il2CppDomain *(*fn_dom)(void);
    typedef Il2CppDomain *(*fn_attach)(Il2CppDomain *);
    typedef const Il2CppAssembly **(*fn_asms)(const Il2CppDomain *, size_t *);
    typedef Il2CppImage *(*fn_img)(const Il2CppAssembly *);
    typedef Il2CppClass *(*fn_cfn)(const Il2CppImage *, const char *, const char *);
    typedef void *(*fn_fields)(Il2CppClass *, void **);
    typedef const char *(*fn_fname)(void *);
    typedef size_t (*fn_foffset)(void *);
    typedef uint32_t (*fn_ftype)(void *);
    fn_dom dom_get = (fn_dom)sym("il2cpp_domain_get");
    fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");
    fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");
    fn_img img_of = (fn_img)sym("il2cpp_assembly_get_image");
    fn_cfn cfn = (fn_cfn)sym("il2cpp_class_from_name");
    fn_fields fields = (fn_fields)sym("il2cpp_class_get_fields");
    fn_fname fname = (fn_fname)sym("il2cpp_field_get_name");
    fn_foffset foff = (fn_foffset)sym("il2cpp_field_get_offset");
    fn_ftype ftype = (fn_ftype)sym("il2cpp_field_get_type");
    if (!dom_get || !asms || !cfn || !fields || !fname) { LOGI("sym missing"); return -1; }
    Il2CppDomain *dom = dom_get();
    if (!dom || !img_of || !foff) return -2;
    if (attach) attach(dom);
    size_t n = 0; const Il2CppAssembly **a = asms(dom, &n);
    for (size_t k = 0; k < n; k++) {
        Il2CppImage *im = img_of(a[k]);
        if (!im) continue;
        Il2CppClass *c = cfn(im, ns, nm);
        if (!c) continue;
        LOGI("FIELDS of %s.%s", ns, nm);
        void *iter = NULL; void *f;
        while ((f = fields(c, &iter)) != NULL) {
            LOGI("   F %s off=%zu type=%p", fname(f), foff(f), (void*)(ftype ? ftype(f) : 0x0));
        }
        (*env)->ReleaseStringUTFChars(env, jns, ns);
        (*env)->ReleaseStringUTFChars(env, jname, nm);
        return 0;
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    LOGI("class not found %s.%s", ns, nm);
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_akpatch_Native_findMethod(JNIEnv *env, jclass clz, jstring jns, jstring jname, jstring jmeth, jint pc) {
    const char *ns = (*env)->GetStringUTFChars(env, jns, NULL);
    const char *nm = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *me = (*env)->GetStringUTFChars(env, jmeth, NULL);
    typedef Il2CppDomain *(*fn_domain_get)(void);
    typedef Il2CppDomain *(*fn_thread_attach)(Il2CppDomain *);
    typedef Il2CppImage *(*fn_asm_img)(const Il2CppAssembly *);
    typedef Il2CppClass *(*fn_cls_from_name)(const Il2CppImage *, const char *, const char *);
    typedef const MethodInfo *(*fn_get_meth)(Il2CppClass *, const char *, int);
    typedef Il2CppImage *(*fn_dom_asm_open)(const Il2CppDomain *, const char *);
    fn_domain_get domain_get = (fn_domain_get)sym("il2cpp_domain_get");
    fn_thread_attach thread_attach = (fn_thread_attach)sym("il2cpp_thread_attach");
    fn_cls_from_name cls_from_name = (fn_cls_from_name)sym("il2cpp_class_from_name");
    fn_get_meth get_meth = (fn_get_meth)sym("il2cpp_class_get_method_from_name");
    if (!domain_get || !cls_from_name || !get_meth) return 0;
    Il2CppDomain *dom = domain_get();
    if (!dom) return 0;
    if (thread_attach) thread_attach(dom);
    /* search every image for the class */
    typedef const Il2CppAssembly **(*fn_get_asm)(const Il2CppDomain *, size_t *);
    typedef size_t (*fn_img_cnt)(const Il2CppImage *);
    typedef const Il2CppClass *(*fn_img_cls)(const Il2CppImage *, size_t);
    typedef const char *(*fn_cls_name)(const Il2CppClass *);
    typedef const char *(*fn_cls_ns)(const Il2CppClass *);
    fn_get_asm get_asm = (fn_get_asm)sym("il2cpp_domain_get_assemblies");
    fn_asm_img asm_img = (fn_asm_img)sym("il2cpp_assembly_get_image");
    fn_img_cnt img_cnt = (fn_img_cnt)sym("il2cpp_image_get_class_count");
    fn_img_cls img_cls = (fn_img_cls)sym("il2cpp_image_get_class");
    fn_cls_name cls_name = (fn_cls_name)sym("il2cpp_class_get_name");
    fn_cls_ns cls_ns = (fn_cls_ns)sym("il2cpp_class_get_namespace");
    size_t an = 0;
    const Il2CppAssembly **asms = get_asm ? get_asm(dom, &an) : NULL;
    for (size_t i = 0; asms && i < an; i++) {
        Il2CppImage *img = asm_img ? asm_img(asms[i]) : NULL;
        if (!img) continue;
        Il2CppClass *c = cls_from_name(img, ns, nm);
        if (!c) {
            size_t cc = img_cnt ? img_cnt(img) : 0;
            for (size_t j = 0; j < cc; j++) {
                const Il2CppClass *cc2 = img_cls ? img_cls(img, j) : NULL;
                if (!cc2) continue;
                const char *cname = cls_name ? cls_name(cc2) : NULL;
                const char *cns = cls_ns ? cls_ns(cc2) : NULL;
                if (cname && strcmp(cname, nm) == 0 && (!ns[0] || (cns && strcmp(cns, ns) == 0))) { c = (Il2CppClass *)cc2; break; }
            }
        }
        if (!c) continue;
        const MethodInfo *m = get_meth(c, me, pc);
        if (!m) continue;
        void *ptr = *(void **)m;
        LOGI("findMethod %s.%s::%s(%d) info=%p ptr=%p", ns, nm, me, pc, (void*)m, ptr);
        (*env)->ReleaseStringUTFChars(env, jns, ns);
        (*env)->ReleaseStringUTFChars(env, jname, nm);
        (*env)->ReleaseStringUTFChars(env, jmeth, me);
        return (jlong)(intptr_t)m;
    }
    (*env)->ReleaseStringUTFChars(env, jns, ns);
    (*env)->ReleaseStringUTFChars(env, jname, nm);
    (*env)->ReleaseStringUTFChars(env, jmeth, me);
    LOGI("findMethod not found %s.%s::%s", ns, nm, me);
    return 0;
}

/* ---------------- guarded il2cpp probe + hook ---------------- */
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>

typedef Il2CppString *(*fn_string_new)(const char *);
static fn_string_new g_string_new = NULL;

static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_guarded = 0;
static struct sigaction g_old_segv, g_old_bus;

static void guard_handler(int sig, siginfo_t *si, void *ctx) {
    if (g_guarded) {
        g_guarded = 0;
        siglongjmp(g_jmp, 1);
    }
    /* not one of our guarded probes: restore the previous handler so debuggerd
       produces a proper tombstone, then re-deliver the signal */
    struct sigaction *old = (sig == SIGSEGV) ? &g_old_segv : &g_old_bus;
    LOGI("UNGUARDED FAULT sig=%d addr=%p", sig, si ? si->si_addr : 0);
    sigaction(sig, old, NULL);
    syscall(SYS_tgkill, getpid(), gettid(), sig);
}

static void install_guards(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = guard_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);
}

typedef struct MethodInfo MethodInfo;

static void *g_orig_build = NULL; /* original BuildNetworkConfigUrl */
static void *g_orig_remote = NULL;
static void *g_orig_update = NULL;

static Il2CppString *my_build_network_config_url(void *self) {
    if (g_string_new) return g_string_new("http://127.0.0.1:10029/config/prod/official/network_config");
    return NULL;
}

static int do_hook(void *methodInfo, void *replacement, void **origOut) {
    if (!methodInfo) return -1;
    void *old = *(void **)methodInfo;
    if (origOut) *origOut = old;
    *(void **)methodInfo = replacement;
    LOGI("hooked %p (was %p) -> %p", methodInfo, old, replacement);
    return 0;
}

static int install_inline_hook(unsigned long target, void *repl) {
    unsigned long page = target & ~0xFFFUL;
    size_t len = 0x2000;
    if (mprotect((void *)page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGI("mprotect failed for %lx", page);
        return -1;
    }
    unsigned char stub[16];
    memset(stub, 0, sizeof(stub));
    uint32_t i0 = 0x58000050; /* ldr x16, #8 */
    uint32_t i1 = 0xD61F0200; /* br x16 */
    memcpy(stub + 0, &i0, 4);
    memcpy(stub + 4, &i1, 4);
    memcpy(stub + 8, &repl, 8);
    memcpy((void *)target, stub, 16);
    __builtin___clear_cache((char *)target, (char *)target + 16);
    mprotect((void *)page, len, PROT_READ | PROT_EXEC);
    LOGI("inline hook installed at 0x%lx -> %p", target, repl);
    return 0;
}

#define RVA_BUILD_NETWORK_CONFIG_URL 0x9931F7CUL
#define RVA_SET_RESPONSE 0x993B100UL
#define RVA_GET_GAME_SERVER_URL  0x993A938UL
#define RVA_GET_SDK_SERVER_URL   0x993A9C4UL
#define RVA_GET_U8_SERVER_URL    0x993AA50UL
#define RVA_GET_HOT_UPDATE_URL   0x993AADCUL
#define RVA_GET_HT_UDT_VER_URL   0x993AB68UL
#define RVA_GET_ANNOUNCE_URL     0x993ABF4UL

#define SRV "http://127.0.0.1:10029"

/* ---- introspect / patch NetworkConfig data object ---- */
typedef struct { void *klass; void *monitor; } Il2CppObjHdr;

static int str_to_utf8(Il2CppString *s, char *out, int max) {
    if (!s) return 0;
    unsigned char *p = (unsigned char *)s;
    int len = *(int *)(p + 16);
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

static int g_dumped = 0;

static void dump_data_fields(void *self) {
    if (g_dumped || !self) return;
    g_dumped = 1;
    typedef void *(*fn_get_field)(void *, const char *);
    typedef void *(*fn_obj_class)(void *);
    typedef void *(*fn_fields)(void *, void **);
    typedef const char *(*fn_fname)(void *);
    typedef size_t (*fn_foff)(void *);
    typedef void (*fn_fget)(void *, void *, void *);
    fn_get_field get_field = (fn_get_field)sym("il2cpp_class_get_field_from_name");
    fn_obj_class obj_class = (fn_obj_class)sym("il2cpp_object_get_class");
    fn_fields fields = (fn_fields)sym("il2cpp_class_get_fields");
    fn_fname fname = (fn_fname)sym("il2cpp_field_get_name");
    fn_foff foff = (fn_foff)sym("il2cpp_field_get_offset");
    fn_fget fget = (fn_fget)sym("il2cpp_field_get_value");
    if (!get_field || !obj_class || !fields || !fname) return;
    void *klass = obj_class(self);
    void *f = get_field(klass, "<data>k__BackingField");
    if (!f) { LOGI("no data field"); return; }
    void *data = NULL;
    if (fget) { fget(self, f, &data); }
    if (!data) { foff ? LOGI("data=null (off=%zu)", foff(f)) : 0; return; }
    void *dclass = obj_class(data);
    LOGI("DATA FIELDS:");
    void *iter = NULL; void *ff;
    int n = 0;
    while ((ff = fields((void *)dclass, &iter)) != NULL && n < 60) {
        const char *nm = fname(ff);
        char val[512]; val[0] = 0;
        if (fget && nm && ff) {
            Il2CppString *sv = NULL;
            /* only attempt for likely-string fields */
            fget(data, ff, &sv);
            str_to_utf8(sv, val, sizeof(val));
        }
        LOGI("  DF %s = %s", nm ? nm : "?", val);
        n++;
    }
}

/* ---------- patch NetworkConfig data fields ---------- */
static void *find_class(const char *ns, const char *name) {
    typedef Il2CppDomain *(*fn_dom)(void);
    typedef const Il2CppAssembly **(*fn_asms)(const Il2CppDomain *, size_t *);
    typedef Il2CppImage *(*fn_img)(const Il2CppAssembly *);
    typedef Il2CppClass *(*fn_cfn)(const Il2CppImage *, const char *, const char *);
    fn_dom dom_get = (fn_dom)sym("il2cpp_domain_get");
    fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");
    fn_img img_of = (fn_img)sym("il2cpp_assembly_get_image");
    fn_cfn cfn = (fn_cfn)sym("il2cpp_class_from_name");
    if (!dom_get || !asms || !img_of || !cfn) return NULL;
    Il2CppDomain *dom = dom_get();
    if (!dom) return NULL;
    size_t n = 0; const Il2CppAssembly **a = asms(dom, &n);
    for (size_t k = 0; k < n; k++) {
        Il2CppImage *im = img_of(a[k]);
        if (!im) continue;
        Il2CppClass *c = cfn(im, ns, name);
        if (c) return c;
    }
    return NULL;
}

static int set_str_field(void *obj, void *field, const char *val) {
    typedef void (*fn_set)(void *, void *, void *);
    fn_set fset = (fn_set)sym("il2cpp_field_set_value");
    if (!fset || !g_string_new) return 0;
    Il2CppString *s = g_string_new(val);
    fset(obj, field, &s);
    return 1;
}

static int g_patched_once = 0;

static void patch_config_data_from(void *self) {
    if (g_patched_once || !self || !g_string_new) return;
    typedef void *(*fn_oc)(void *);
    typedef void *(*fn_gf)(void *, const char *);
    typedef void (*fn_fg)(void *, void *, void *);
    typedef void *(*fn_fl)(void *, void **);
    typedef const char *(*fn_fn)(void *);
    typedef void *(*fn_ft)(void *);
    typedef const char *(*fn_tn)(void *);
    fn_oc objcls = (fn_oc)sym("il2cpp_object_get_class");
    fn_gf getf = (fn_gf)sym("il2cpp_class_get_field_from_name");
    fn_fg fget = (fn_fg)sym("il2cpp_field_get_value");
    fn_fl fields = (fn_fl)sym("il2cpp_class_get_fields");
    fn_fn fname = (fn_fn)sym("il2cpp_field_get_name");
    fn_ft ftype = (fn_ft)sym("il2cpp_field_get_type");
    fn_tn tname = (fn_tn)sym("il2cpp_type_get_name");
    if (!objcls || !getf || !fget || !fields || !fname) return;
    void *df = getf((void *)objcls(self), "<data>k__BackingField");
    if (!df) return;
    void *data = NULL;
    fget(self, df, &data);
    if (!data) return;
    void *dcls = objcls(data);
    static const struct { const char *name; const char *val; } tab[] = {
        {"gameServerUrl", SRV},
        {"sdkServerUrl", SRV},
        {"u8ServerUrl", SRV "/u8"},
        /* only the gate-related endpoints; hot update / version URLs stay official
           (patching those while the update flow runs has proven unstable) */

    };
    int patched = 0;
    for (size_t i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        void *ff = getf((void *)dcls, tab[i].name);
        if (!ff) continue;
        if (ftype && tname) {
            const char *tn = tname(ftype(ff));
            if (!tn || strcmp(tn, "System.String") != 0) continue;
        }
        set_str_field(data, ff, tab[i].val);
        patched++;
    }
    g_patched_once = 1;
    LOGI("patched %d data fields (from instance)", patched);
}

static void patch_config_data(void) {
    typedef void *(*fn_parent)(void *);
    typedef void *(*fn_getf)(void *, const char *);
    typedef void (*fn_sget)(void *, void *);
    typedef void (*fn_fget)(void *, void *, void *);
    typedef void *(*fn_objcls)(void *);
    typedef void *(*fn_fields)(void *, void **);
    typedef const char *(*fn_fname)(void *);
    typedef void (*fn_attach)(Il2CppDomain *);
    fn_parent parent = (fn_parent)sym("il2cpp_class_get_parent");
    fn_getf getf = (fn_getf)sym("il2cpp_class_get_field_from_name");
    fn_sget sget = (fn_sget)sym("il2cpp_field_static_get_value");
    fn_fget fget = (fn_fget)sym("il2cpp_field_get_value");
    fn_objcls objcls = (fn_objcls)sym("il2cpp_object_get_class");
    fn_fields fields = (fn_fields)sym("il2cpp_class_get_fields");
    fn_fname fname = (fn_fname)sym("il2cpp_field_get_name");
    if (!parent || !getf || !sget || !fget || !objcls || !fields || !fname) return;
    void *c = find_class("Torappu.Config", "NetworkConfig");
    if (!c) return;
    void *inst = NULL;
    for (void *k = c; k; k = parent(k)) {
        void *sf = getf(k, "s_instance");
        if (sf) { sget(sf, &inst); if (inst) break; }
    }
    if (!inst) return;
    void *df = getf((void*)objcls(inst), "<data>k__BackingField");
    if (!df) return;
    void *data = NULL;
    fget(inst, df, &data);
    if (!data) return;
    void *dcls = objcls(data);
    void *iter = NULL; void *f;
    static int logged = 0;
    if (!logged) {
        logged = 1;
        LOGI("DATA CLASS FIELDS:");
        void *it2 = NULL; void *f2;
        while ((f2 = fields((void *)dcls, &it2)) != NULL) LOGI("   DF %s", fname(f2));
    }
    typedef void *(*fn_fget_type)(void *);
    typedef const char *(*fn_type_name)(void *);
    fn_fget_type fget_type = (fn_fget_type)sym("il2cpp_field_get_type");
    fn_type_name type_name = (fn_type_name)sym("il2cpp_type_get_name");
    static const struct { const char *name; const char *val; } tab[] = {
        {"gameServerUrl", SRV},
        {"sdkServerUrl", SRV},
        {"u8ServerUrl", SRV "/u8"},
        /* only the gate-related endpoints; hot update / version URLs stay official
           (patching those while the update flow runs has proven unstable) */

    };
    int patched = 0;
    for (size_t i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        void *ff = getf((void *)dcls, tab[i].name);
        if (!ff) continue;
        if (fget_type && type_name) {
            const char *tn = type_name(fget_type(ff));
            if (!tn || strcmp(tn, "System.String") != 0) {
                if (!logged) LOGI("skip %s (type %s)", tab[i].name, tn ? tn : "?");
                continue;
            }
        }
        set_str_field(data, ff, tab[i].val);
        patched++;
    }
    if (patched) { g_patched_once = 1; LOGI("patched %d data fields (one-shot)", patched); }
}

static void *config_patch_thread(void *arg) {
    (void)arg;
    install_guards();
    sleep(7);
    int attached = 0;
    for (int i = 0; i < 8000; i++) {
        unsigned long base = il2cpp_base();
        if (base && sym("il2cpp_domain_get")) {
            if (!g_string_new) g_string_new = (fn_string_new)sym("il2cpp_string_new");
            if (g_string_new) {
                typedef Il2CppDomain *(*fn_dom)(void);
                typedef Il2CppDomain *(*fn_attach)(Il2CppDomain *);
                fn_dom dom_get = (fn_dom)sym("il2cpp_domain_get");
                fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");
                Il2CppDomain *dom = dom_get ? dom_get() : NULL;
                if (dom) {
                    g_guarded = 1;
                    if (sigsetjmp(g_jmp, 1) == 0) {
                        if (!attached && attach) { attach(dom); attached = 1; }
                        patch_config_data();
                    }
                    g_guarded = 0;
                }
            }
        }
        usleep(150000);
    }
    return NULL;
}

/* ---- periodic URL logger: reveals which endpoints the C# side actually uses ---- */
static char g_seen[300][160];
static int g_seen_n = 0;

static int seen_before(const char *s) {
    for (int i = 0; i < g_seen_n; i++) if (!strcmp(g_seen[i], s)) return 1;
    return 0;
}

static void url_log_once(void) {
    char hits[60][256];
    int n = collect_strings("http://", 0, hits, 20);
    n += collect_strings("http://", 1, hits + n, 20 - n);
    n += collect_strings("https://", 0, hits + n, 20 - n);
    n += collect_strings("https://", 1, hits + n, 20 - n);
    for (int i = 0; i < n; i++) {
        char *s = hits[i];
        if (!strstr(s, "hypergryph") && !strstr(s, "10029")) continue;
        if (strlen(s) > 150) s[150] = 0;
        if (seen_before(s)) continue;
        if (g_seen_n < 300) { strncpy(g_seen[g_seen_n], s, 159); g_seen[g_seen_n][159] = 0; g_seen_n++; }
        LOGI("URLNEW %s", s);
    }
}

static void *url_log_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < 600; i++) {
        usleep(2000000);
        url_log_once();
    }
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_akpatch_Native_startUrlLog(JNIEnv *env, jclass clz) {
    pthread_t t;
    pthread_create(&t, NULL, url_log_thread, NULL);
    pthread_detach(t);
}

JNIEXPORT void JNICALL
Java_com_akpatch_Native_startConfigPatch(JNIEnv *env, jclass clz) {
    pthread_t t;
    pthread_create(&t, NULL, config_patch_thread, NULL);
    pthread_detach(t);
}

static Il2CppString *s_game(void *self)  { /* no inline patch */ return g_string_new ? g_string_new(SRV) : NULL; }
static Il2CppString *s_sdk(void *self)   { /* no inline patch */ return g_string_new ? g_string_new(SRV) : NULL; }
static Il2CppString *s_u8(void *self)    { (void)self; return g_string_new ? g_string_new(SRV "/u8") : NULL; }
static Il2CppString *s_hot(void *self)   { /* no inline patch */ return g_string_new ? g_string_new(SRV "/assetbundle/official") : NULL; }
static Il2CppString *s_ver(void *self)   { /* no inline patch */ return g_string_new ? g_string_new(SRV "/config/prod/official/Android/version") : NULL; }
static Il2CppString *s_ann(void *self)   { /* no inline patch */ return g_string_new ? g_string_new(SRV "/config/prod/announce_meta/Android/announcement.meta.json") : NULL; }
#define RVA_BUILD_REMOTE_CONFIG_URL  0x9931DF8UL
#define RVA_BUILD_GAME_UPDATE_URL    0x9932100UL

/* build a trampoline for a function whose first 16 bytes contain no PC-relative insns */
static void *make_trampoline(unsigned long target) {
    uint32_t ins[4];
    memcpy(ins, (void *)target, 16);
    for (int i = 0; i < 4; i++) {
        uint32_t v = ins[i];
        if ((v & 0x7C000000u) == 0x14000000u) return NULL; /* B/BL */
        if ((v & 0x9F000000u) == 0x10000000u) return NULL; /* ADR */
        if ((v & 0x9F000000u) == 0x90000000u) return NULL; /* ADRP */
        if ((v & 0x3B000000u) == 0x18000000u) return NULL; /* LDR literal */
    }
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    memcpy(p, ins, 16);
    uint32_t ldr = 0x58000050u, brx = 0xD61F0200u;
    memcpy((char *)p + 16, &ldr, 4);
    memcpy((char *)p + 20, &brx, 4);
    void *back = (void *)(target + 16);
    memcpy((char *)p + 24, &back, 8);
    __builtin___clear_cache((char *)p, (char *)p + 32);
    return p;
}

typedef void (*fn_setresp)(void *self, Il2CppString *json);
static fn_setresp g_orig_setresp = NULL;

static void my_set_response(void *self, Il2CppString *json) {
    char buf[256];
    str_to_utf8(json, buf, sizeof(buf));
    LOGI("SetResponse: %s", buf);
    if (g_orig_setresp) g_orig_setresp(self, json);
    patch_config_data_from(self);   /* overwrite endpoints right after parsing */
}

static void *rva_hook_thread(void *arg) {
    (void)arg;
    LOGI("rva hook thread start");
    unsigned long base = 0;
    for (int i = 0; i < 6000; i++) {
        base = il2cpp_base();
        if (base) break;
        usleep(5000);
    }
    if (!base) { LOGI("no il2cpp base"); return NULL; }
    LOGI("il2cpp base=0x%lx", base);
    g_string_new = (fn_string_new)sym("il2cpp_string_new");
    if (!g_string_new) LOGI("no il2cpp_string_new");
    /* config fetch goes to the official server so its signed payload passes validation;
       only the resulting endpoints are redirected through the getters below */
    if (getenv("AKPATCH_CONFIG_URL") != NULL) {
        install_inline_hook(base + RVA_BUILD_NETWORK_CONFIG_URL, (void *)my_build_network_config_url);
    } else {
        LOGI("skipping network-config-url hook (keep official signed config)");
    }
    install_inline_hook(base + RVA_GET_GAME_SERVER_URL, (void *)s_game);
    install_inline_hook(base + RVA_GET_SDK_SERVER_URL, (void *)s_sdk);
    install_inline_hook(base + RVA_GET_U8_SERVER_URL, (void *)s_u8);
    install_inline_hook(base + RVA_GET_HOT_UPDATE_URL, (void *)s_hot);
    install_inline_hook(base + RVA_GET_HT_UDT_VER_URL, (void *)s_ver);
    install_inline_hook(base + RVA_GET_ANNOUNCE_URL, (void *)s_ann);
    void *tramp = make_trampoline(base + RVA_SET_RESPONSE);
    if (tramp) {
        g_orig_setresp = (fn_setresp)tramp;
        install_inline_hook(base + RVA_SET_RESPONSE, (void *)my_set_response);
        LOGI("SetResponse hook installed (trampoline %p)", tramp);
    } else {
        LOGI("SetResponse prologue not hookable");
    }
    LOGI("all config hooks installed");
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_akpatch_Native_startRvaHook(JNIEnv *env, jclass clz) {
    pthread_t t;
    pthread_create(&t, NULL, rva_hook_thread, NULL);
    pthread_detach(t);
}

static void *hook_thread(void *arg) {
    (void)arg;
    install_guards();
    LOGI("hook thread start");
    for (int i = 0; i < 3000; i++) {
        if (!sym("il2cpp_domain_get")) { usleep(20000); continue; }
        g_guarded = 1;
        if (sigsetjmp(g_jmp, 1) != 0) {
            /* crashed during probe: runtime not ready */
            usleep(20000);
            continue;
        }
        if (!g_string_new) g_string_new = (fn_string_new)sym("il2cpp_string_new");
        if (!g_string_new) { g_guarded = 0; usleep(20000); continue; }
        typedef Il2CppDomain *(*fn_dom)(void);
        typedef Il2CppDomain *(*fn_attach)(Il2CppDomain *);
        fn_dom dom_get = (fn_dom)sym("il2cpp_domain_get");
        fn_attach attach = (fn_attach)sym("il2cpp_thread_attach");
        Il2CppDomain *dom = dom_get();
        if (!dom) { g_guarded = 0; usleep(20000); continue; }
        if (attach) attach(dom);
        /* find NetworkOptions class + methods */
        typedef const Il2CppAssembly **(*fn_asms)(const Il2CppDomain *, size_t *);
        typedef Il2CppImage *(*fn_img)(const Il2CppAssembly *);
        typedef Il2CppClass *(*fn_cfn)(const Il2CppImage *, const char *, const char *);
        typedef const MethodInfo *(*fn_gm)(Il2CppClass *, const char *, int);
        fn_asms asms = (fn_asms)sym("il2cpp_domain_get_assemblies");
        fn_img img_of = (fn_img)sym("il2cpp_assembly_get_image");
        fn_cfn cfn = (fn_cfn)sym("il2cpp_class_from_name");
        fn_gm gm = (fn_gm)sym("il2cpp_class_get_method_from_name");
        if (!asms || !img_of || !cfn || !gm) { g_guarded = 0; usleep(20000); continue; }
        size_t n = 0;
        const Il2CppAssembly **a = asms(dom, &n);
        if (!a || n == 0) { g_guarded = 0; usleep(50000); continue; }
        Il2CppClass *c = NULL;
        for (size_t k = 0; k < n && !c; k++) {
            Il2CppImage *im = img_of(a[k]);
            if (!im) continue;
            c = cfn(im, "Torappu.Network", "NetworkOptions");
            if (!c) c = cfn(im, "", "NetworkOptions");
        }
        g_guarded = 0;
        if (!c) { usleep(50000); continue; }
        const MethodInfo *m1 = gm(c, "BuildNetworkConfigUrl", 0);
        const MethodInfo *m2 = gm(c, "BuildRemoteConfigUrl", 0);
        const MethodInfo *m3 = gm(c, "BuildGameUpdateV2ApiUrl", 0);
        LOGI("methods: net=%p remote=%p upd=%p", (void*)m1, (void*)m2, (void*)m3);
        if (m1 && !g_orig_build) {
            do_hook((void*)m1, (void*)my_build_network_config_url, &g_orig_build);
            LOGI("hook installed at attempt %d", i);
            return NULL;
        }
        usleep(50000);
    }
    LOGI("hook thread gave up");
    return NULL;
}

static int collect_strings(const char *needle, int wide, char out[][256], int maxout) {
    Region *regs = NULL; int n = read_maps(&regs);
    if (!n) return 0;
    int cnt = 0;
    pid_t pid = getpid();
    size_t chunk = 1 << 20;
    unsigned char *buf = malloc(chunk);
    size_t nl = strlen(needle);
    for (int i = 0; i < n && cnt < maxout; i++) {
        unsigned long addr = regs[i].start;
        while (addr < regs[i].end && cnt < maxout) {
            size_t want = chunk;
            if (addr + want > regs[i].end) want = regs[i].end - addr;
            struct iovec local = { buf, want }, remote = { (void *)addr, want };
            ssize_t got = process_vm_readv(pid, &local, 1, &remote, 1, 0);
            if (got <= 0) { addr += want; continue; }
            for (ssize_t o = 0; o + (ssize_t)(wide ? nl*2 : nl) < got; o++) {
                if (wide) {
                    int ok = 1;
                    for (size_t k = 0; k < nl; k++) if (buf[o + k*2] != (unsigned char)needle[k] || buf[o + k*2 + 1] != 0) { ok = 0; break; }
                    if (!ok) continue;
                    char tmp[256]; int t = 0;
                    for (ssize_t k = o; t < 250 && k + 1 < (ssize_t)got; k += 2) {
                        unsigned char c = buf[k];
                        if (buf[k+1] != 0 || c < 32 || c > 126) break;
                        tmp[t++] = (char)c;
                    }
                    tmp[t] = 0;
                    if (t > 8) {
                        int dup = 0;
                        for (int m = 0; m < cnt; m++) if (!strcmp(out[m], tmp)) { dup = 1; break; }
                        if (!dup) { strncpy(out[cnt], tmp, 255); out[cnt][255] = 0; LOGI("URL %s", out[cnt]); cnt++; }
                    }
                    o += (ssize_t)nl*2;
                } else {
                    if (memcmp(buf + o, needle, nl) != 0) continue;
                    char tmp[256]; int t = 0;
                    for (ssize_t k = o; t < 250 && k < (ssize_t)got; k++) {
                        unsigned char c = buf[k];
                        if (c < 32 || c > 126) break;
                        tmp[t++] = (char)c;
                    }
                    tmp[t] = 0;
                    if (t > 8) {
                        int dup = 0;
                        for (int m = 0; m < cnt; m++) if (!strcmp(out[m], tmp)) { dup = 1; break; }
                        if (!dup) { strncpy(out[cnt], tmp, 255); out[cnt][255] = 0; LOGI("URL %s", out[cnt]); cnt++; }
                    }
                    o += (ssize_t)nl;
                }
            }
            if ((size_t)got < want) addr += want; else addr += (want > 4096 ? want - 4096 : want);
        }
    }
    free(buf); free(regs);
    return cnt;
}

static char g_urls[80][256];

/* scan for URLs containing a given substring; returns number of unique hits */
JNIEXPORT jint JNICALL
Java_com_akpatch_Native_scanUrls(JNIEnv *env, jclass clz, jstring jneedle) {
    const char *needle = jneedle ? (*env)->GetStringUTFChars(env, jneedle, NULL) : "http";
    int a = collect_strings(needle, 0, g_urls, 80);
    int b = collect_strings(needle, 1, g_urls + a, 80 - a);
    if (jneedle) (*env)->ReleaseStringUTFChars(env, jneedle, needle);
    LOGI("scanUrls(%s) unique=%d", needle, a + b);
    return a + b;
}

JNIEXPORT void JNICALL
Java_com_akpatch_Native_startHook(JNIEnv *env, jclass clz) {
    pthread_t t;
    pthread_create(&t, NULL, hook_thread, NULL);
    pthread_detach(t);
}
