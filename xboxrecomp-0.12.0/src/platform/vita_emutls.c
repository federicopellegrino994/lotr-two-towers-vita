/* PS Vita: thread-local storage for __thread / _Thread_local.
 *
 * GCC for arm-vita-eabi has no native TLS: every thread-local access calls
 * __emutls_get_address(&object). libgcc's copy keeps its state behind a
 * zero-initialised gthread mutex set up by a pthread_once -- built against
 * newlib's pthread types, not those of the pthread library the Vita links
 * (pthread-embedded) -- so the once never runs and the first access dies in
 * pthread_mutex_unlock (seen on hardware: WinMain -> CreateDirectoryA ->
 * SetLastError). This definition takes libgcc's place at link time (an
 * object file beats an archive member) and keeps libgcc's ABI:
 *
 *   struct __emutls_object { size, align, loc (index once assigned), templ }
 *
 * Each object gets an index the first time any thread touches it; each
 * thread has an array of pointers (pthread key), grown on demand. The guest
 * register file (RECOMP_TLS) goes through here on every spill, so the fast
 * path is one pthread_getspecific, two loads and a compare. */
#if defined(__vita__)
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

typedef unsigned int xr_word;
struct __emutls_object {
    xr_word size;
    xr_word align;
    union { uintptr_t offset; void *ptr; } loc;
    void *templ;
};

typedef struct { uintptr_t n; void *data[]; } xr_tls_array;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t   s_key;
static volatile int    s_key_ok;
static uintptr_t       s_count;

static void xr_tls_destroy(void *p)
{
    xr_tls_array *a = (xr_tls_array *)p;
    uintptr_t i;
    if (!a) return;
    for (i = 0; i < a->n; i++)
        if (a->data[i]) free(((void **)a->data[i])[-1]);
    free(a);
}

static uintptr_t xr_tls_index(struct __emutls_object *obj)
{
    uintptr_t idx = __atomic_load_n(&obj->loc.offset, __ATOMIC_ACQUIRE);
    if (idx) return idx;
    pthread_mutex_lock(&s_lock);
    if (!s_key_ok) {
        pthread_key_create(&s_key, xr_tls_destroy);
        __atomic_store_n(&s_key_ok, 1, __ATOMIC_RELEASE);
    }
    idx = obj->loc.offset;
    if (!idx) {
        idx = ++s_count;
        __atomic_store_n(&obj->loc.offset, idx, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&s_lock);
    return idx;
}

/* One variable's storage for this thread, aligned as asked; the block's
 * start is kept just below the returned pointer for the destructor. */
static void *xr_tls_alloc(const struct __emutls_object *obj)
{
    xr_word align = obj->align < sizeof(void *) ? sizeof(void *) : obj->align;
    char *raw = (char *)malloc(obj->size + align + sizeof(void *));
    char *p;
    if (!raw) abort();
    p = (char *)(((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1));
    ((void **)p)[-1] = raw;
    if (obj->templ) memcpy(p, obj->templ, obj->size);
    else memset(p, 0, obj->size);
    return p;
}

void *__emutls_get_address(struct __emutls_object *obj)
{
    uintptr_t idx = xr_tls_index(obj);
    xr_tls_array *a = (xr_tls_array *)pthread_getspecific(s_key);
    void *p;
    if (!a || idx > a->n) {
        uintptr_t n = a ? a->n : 0, want = idx + 32;
        xr_tls_array *b = (xr_tls_array *)realloc(a, sizeof *a + want * sizeof(void *));
        if (!b) abort();
        memset(&b->data[n], 0, (want - n) * sizeof(void *));
        b->n = want;
        a = b;
        pthread_setspecific(s_key, a);
    }
    p = a->data[idx - 1];
    if (!p) {
        p = xr_tls_alloc(obj);
        a->data[idx - 1] = p;
    }
    return p;
}

void __emutls_register_common(struct __emutls_object *obj, xr_word size,
                              xr_word align, void *templ)
{
    if (obj->size < size) { obj->size = size; obj->templ = NULL; }
    if (obj->align < align) obj->align = align;
    if (templ && size == obj->size) obj->templ = templ;
}
#endif
