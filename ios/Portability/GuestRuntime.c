/* Original static iOS call/import bridge. No Win32 hostrt.c or native loader. */
#include "GuestRuntime.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SSBUIOSGuestRuntime {
    SSBUIOSModuleRegistry registry;
    SSBUIOSModuleSymbols *tables;
    size_t bindings;
    pthread_mutex_t mutex;
};
typedef struct {
    SSBUIOSGuestRuntime *runtime;
    const SSBUIOSGuestModule *active;
    unsigned depth;
} RuntimeBinding;
static pthread_key_t runtime_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static int key_error = -1;

static void release_binding(void *opaque) {
    RuntimeBinding *binding = opaque;
    if (!binding) return;
    pthread_mutex_lock(&binding->runtime->mutex);
    --binding->runtime->bindings;
    pthread_mutex_unlock(&binding->runtime->mutex);
    free(binding);
}
static void create_key(void) {
    key_error = pthread_key_create(&runtime_key, release_binding);
}
static int key_ready(void) {
    return pthread_once(&key_once, create_key) || key_error;
}
static RuntimeBinding *current_binding(void) {
    return key_ready() ? NULL : pthread_getspecific(runtime_key);
}

int ssbu_ios_runtime_create(SSBUIOSGuestRuntime **out,
                            const SSBUIOSModuleRegistry *registry,
                            const SSBUIOSModuleSymbols *tables, size_t count) {
    if (!out || *out || !registry || !tables || count != registry->count)
        return -1;
    SSBUIOSModuleRegistry validated = {0};
    if (ssbu_ios_modules_init_preferred(&validated, registry->modules, count,
                                       registry->preferences, registry->preference_count)) return -1;
    SSBUIOSGuestRuntime *runtime = calloc(1, sizeof *runtime);
    if (!runtime) return -1;
    runtime->tables = calloc(count, sizeof *runtime->tables);
    if (!runtime->tables) { free(runtime); return -1; }
    runtime->registry = validated;
    for (size_t i = 0; i < count; ++i) {
        if (!tables[i].module_name || !*tables[i].module_name ||
            tables[i].symbol_count > 1048576 ||
            (tables[i].symbol_count && !tables[i].symbols)) goto fail;
        size_t owner = count;
        for (size_t j = 0; j < count; ++j)
            if (!strcmp(tables[i].module_name, validated.modules[j].name)) owner = j;
        if (owner == count || runtime->tables[owner].module_name) goto fail;
        const SSBUIOSGuestModule *module = &validated.modules[owner];
        for (size_t j = 0; j < tables[i].symbol_count; ++j) {
            const DynSym *s = &tables[i].symbols[j];
            if (!s->name || (!*s->name && (j || s->defined)) ||
                (s->defined != 0 && s->defined != 1)) goto fail;
            if (tables[i].preemptible &&
                (tables[i].preemptible[j] > 1 ||
                 (tables[i].preemptible[j] && !s->defined))) goto fail;
            if (s->defined && (s->value > UINT64_MAX - module->load_bias ||
                module->load_bias + s->value < module->guest_start ||
                module->load_bias + s->value - module->guest_start >= module->guest_size))
                goto fail;
        }
        runtime->tables[owner] = tables[i];
    }
    if (pthread_mutex_init(&runtime->mutex, NULL)) goto fail;
    *out = runtime;
    return 0;
fail:
    free(runtime->tables); free(runtime); return -1;
}

int ssbu_ios_runtime_bind(SSBUIOSGuestRuntime *runtime) {
    if (!runtime || key_ready() || pthread_getspecific(runtime_key)) return -1;
    RuntimeBinding *binding = calloc(1, sizeof *binding);
    if (!binding) return -1;
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->bindings == SIZE_MAX) {
        pthread_mutex_unlock(&runtime->mutex); free(binding); return -1;
    }
    ++runtime->bindings;
    pthread_mutex_unlock(&runtime->mutex);
    binding->runtime = runtime;
    if (pthread_setspecific(runtime_key, binding)) {
        release_binding(binding); return -1;
    }
    return 0;
}
void ssbu_ios_runtime_unbind(void) {
    RuntimeBinding *binding = current_binding();
    if (binding && binding->depth) return; /* Never free a running call frame. */
    if (binding && !pthread_setspecific(runtime_key, NULL)) release_binding(binding);
}
int ssbu_ios_runtime_close(SSBUIOSGuestRuntime **owner) {
    if (!owner) return -1;
    SSBUIOSGuestRuntime *runtime = *owner;
    if (!runtime) return 0;
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->bindings) { pthread_mutex_unlock(&runtime->mutex); return -1; }
    pthread_mutex_unlock(&runtime->mutex);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime->tables); free(runtime); *owner = NULL;
    return 0;
}

int ssbu_ios_runtime_call(Ctx *context, uint64_t address) {
    RuntimeBinding *binding = current_binding();
    const SSBUIOSGuestModule *module = NULL;
    void (*function)(Ctx *) = NULL;
    if (!binding || !context || !address || binding->depth >= 1024 ||
        ssbu_ios_modules_lookup(&binding->runtime->registry, address, &module, &function))
        return -1;
    const SSBUIOSGuestModule *saved = binding->active;
    binding->active = module;
    ++binding->depth;
    context->fn = address;
    function(context);
    --binding->depth;
    binding->active = saved;
    return 0;
}

int ssbu_ios_runtime_import(Ctx *context, int index) {
    RuntimeBinding *binding = current_binding();
    if (!binding || !binding->active || !context || index < 0 ||
        context->x[30] < 4 || binding->depth >= 1024) return -1;
    size_t owner = (size_t)(binding->active - binding->runtime->registry.modules);
    const SSBUIOSModuleSymbols *table = &binding->runtime->tables[owner];
    if ((size_t)index >= table->symbol_count) return -1;
    const DynSym *symbol = &table->symbols[index];
    if (!*symbol->name) return -1;
    uint64_t address = 0;
    int preferred = 1;
    if (!symbol->defined || (table->preemptible && table->preemptible[index]))
        preferred = ssbu_ios_modules_preferred(&binding->runtime->registry, symbol->name, &address);
    if (preferred == -1) return -1;
    if (preferred == 1) {
        if (symbol->defined) address = binding->active->load_bias + symbol->value;
        else if (ssbu_ios_modules_resolve(&binding->runtime->registry, symbol->name, &address))
            return -1;
    }
    /* A data export or missing AOT entry cannot be invoked as a function. */
    const SSBUIOSGuestModule *target_module = NULL;
    void (*function)(Ctx *) = NULL;
    if (!address || ssbu_ios_modules_lookup(&binding->runtime->registry, address,
                                            &target_module, &function)) return -1;
    const char *saved_name = context->in_import;
    uint64_t saved_from = context->import_from;
    context->in_import = symbol->name;
    context->import_from = context->x[30] - 4;
    int status = ssbu_ios_runtime_call(context, address);
    context->in_import = saved_name;
    context->import_from = saved_from;
    return status;
}

void call_indirect(Ctx *context, uint64_t address) {
    if (ssbu_ios_runtime_call(context, address)) {
        fprintf(stderr, "SSBU unresolved static guest call 0x%llx\n",
                (unsigned long long)address);
        abort();
    }
}
void host_import(Ctx *context, int index) {
    if (ssbu_ios_runtime_import(context, index)) {
        fprintf(stderr, "SSBU unresolved module-local import index %d\n", index);
        abort();
    }
}
