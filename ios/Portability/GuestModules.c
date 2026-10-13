#include "GuestModules.h"

#include <string.h>

static int in_module(const SSBUIOSGuestModule *m, uint64_t offset) {
    if (offset > UINT64_MAX - m->load_bias) return 0;
    uint64_t address = m->load_bias + offset;
    return address >= m->guest_start &&
           address - m->guest_start < m->guest_size;
}

static const SSBUIOSGuestExport *find_export(const SSBUIOSGuestModule *m,
                                            const char *name) {
    size_t low = 0, high = m->export_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp = strcmp(m->exports[mid].name, name);
        if (!cmp) return &m->exports[mid];
        if (cmp < 0) low = mid + 1; else high = mid;
    }
    return NULL;
}

static const SSBUIOSProviderPreference *find_preference(
    const SSBUIOSProviderPreference *preferences, size_t count, const char *name) {
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp = strcmp(preferences[mid].name, name);
        if (!cmp) return &preferences[mid];
        if (cmp < 0) low = mid + 1; else high = mid;
    }
    return NULL;
}

int ssbu_ios_modules_init_preferred(SSBUIOSModuleRegistry *out,
                                    const SSBUIOSGuestModule *modules, size_t count,
                                    const SSBUIOSProviderPreference *preferences,
                                    size_t preference_count) {
    if (!out || !modules || !count || count > 256 || preference_count > 1048576 ||
        (preference_count && !preferences)) return -1;
    for (size_t i = 0; i < count; ++i) {
        const SSBUIOSGuestModule *m = &modules[i];
        if (!m->name || !*m->name || !m->guest_size ||
            m->guest_size > UINT64_MAX - m->guest_start ||
            (m->function_count && !m->functions) ||
            (m->export_count && !m->exports)) return -1;
        for (size_t j = 0; j < m->function_count; ++j)
            if (!m->functions[j].fn || !in_module(m, m->functions[j].addr) ||
                (j && m->functions[j-1].addr >= m->functions[j].addr))
                return -1;
        for (size_t j = 0; j < m->export_count; ++j)
            if (!m->exports[j].name || !*m->exports[j].name ||
                !in_module(m, m->exports[j].offset) ||
                (j && strcmp(m->exports[j-1].name, m->exports[j].name) >= 0))
                return -1;
    }
    for (size_t i = 0; i < preference_count; ++i) {
        const SSBUIOSProviderPreference *p = &preferences[i];
        if (!p->name || !*p->name || !p->provider || !*p->provider ||
            (i && strcmp(preferences[i-1].name, p->name) >= 0)) return -1;
        int found = 0;
        for (size_t j = 0; j < count; ++j)
            if (!strcmp(modules[j].name, p->provider) && find_export(&modules[j], p->name))
                found = 1;
        if (!found) return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        const SSBUIOSGuestModule *a = &modules[i];
        for (size_t j = i + 1; j < count; ++j) {
            const SSBUIOSGuestModule *b = &modules[j];
            if (!strcmp(a->name, b->name) ||
                (a->guest_start < b->guest_start + b->guest_size &&
                 b->guest_start < a->guest_start + a->guest_size)) return -1;
            for (size_t k = 0; k < a->export_count; ++k)
                if (find_export(b, a->exports[k].name) &&
                    !find_preference(preferences, preference_count, a->exports[k].name)) return -1;
        }
    }
    *out = (SSBUIOSModuleRegistry){modules, count, preferences, preference_count};
    return 0;
}

int ssbu_ios_modules_init(SSBUIOSModuleRegistry *out,
                          const SSBUIOSGuestModule *modules, size_t count) {
    return ssbu_ios_modules_init_preferred(out, modules, count, NULL, 0);
}

int ssbu_ios_modules_preferred(void *opaque, const char *name, uint64_t *out) {
    const SSBUIOSModuleRegistry *r = opaque;
    if (!r || !r->modules || !r->count || !name || !*name || !out ||
        (r->preference_count && !r->preferences)) return -1;
    const SSBUIOSProviderPreference *p = find_preference(r->preferences, r->preference_count, name);
    if (!p) return 1;
    for (size_t i = 0; i < r->count; ++i) {
        if (strcmp(r->modules[i].name, p->provider)) continue;
        const SSBUIOSGuestExport *e = find_export(&r->modules[i], name);
        if (!e) return -1;
        *out = r->modules[i].load_bias + e->offset; return 0;
    }
    return -1;
}

int ssbu_ios_modules_resolve(void *opaque, const char *name, uint64_t *out) {
    const SSBUIOSModuleRegistry *r = opaque;
    if (!r || !r->modules || !r->count || !name || !*name || !out) return -1;
    int preferred = ssbu_ios_modules_preferred(opaque, name, out);
    if (preferred != 1) return preferred;
    for (size_t i = 0; i < r->count; ++i) {
        const SSBUIOSGuestExport *e = find_export(&r->modules[i], name);
        if (e) { *out = r->modules[i].load_bias + e->offset; return 0; }
    }
    return 1;
}

int ssbu_ios_modules_dependency(void *opaque, const char *name) {
    const SSBUIOSModuleRegistry *r = opaque;
    if (!r || !r->modules || !r->count || !name || !*name) return -1;
    for (size_t i = 0; i < r->count; ++i)
        if (!strcmp(r->modules[i].name, name)) return 0;
    return 1;
}

int ssbu_ios_modules_lookup(const SSBUIOSModuleRegistry *r, uint64_t address,
                            const SSBUIOSGuestModule **owner,
                            void (**function)(Ctx *)) {
    if (!r || !r->modules || !r->count || !owner || !function) return -1;
    for (size_t i = 0; i < r->count; ++i) {
        const SSBUIOSGuestModule *m = &r->modules[i];
        if (address < m->guest_start ||
            address - m->guest_start >= m->guest_size || address < m->load_bias)
            continue;
        uint64_t offset = address - m->load_bias;
        size_t low = 0, high = m->function_count;
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            const FnEntry *e = &m->functions[mid];
            if (e->addr == offset) { *owner = m; *function = e->fn; return 0; }
            if (e->addr < offset) low = mid + 1; else high = mid;
        }
        return -1;
    }
    return -1;
}

int ssbu_ios_modules_dispatch(const SSBUIOSModuleRegistry *r, Ctx *c,
                              uint64_t address) {
    const SSBUIOSGuestModule *owner = NULL;
    void (*function)(Ctx *) = NULL;
    if (!c || ssbu_ios_modules_lookup(r, address, &owner, &function)) return -1;
    c->fn = address;
    function(c);
    return 0;
}
