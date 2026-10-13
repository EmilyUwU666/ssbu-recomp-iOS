#include "GuestMemory.h"
#include "Probe.h"

#include <string.h>
#include <sys/mman.h>

/* A small, synthetic address translation prototype; deliberately never MAP_FIXED. */
int ssbu_guest_memory_open(SSBUIOSGuestMemory *memory, uint64_t guest_base, size_t length) {
    if (!memory || memory->host || !length || (uint64_t)length > UINT64_MAX - guest_base)
        return -1;

    void *host = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (host == MAP_FAILED)
        return -1;
    memory->host = (unsigned char *)host;
    memory->guest_base = guest_base;
    memory->length = length;
    return 0;
}

void ssbu_guest_memory_close(SSBUIOSGuestMemory *memory) {
    if (!memory)
        return;
    if (memory->host)
        munmap(memory->host, memory->length);
    memory->host = NULL;
    memory->length = 0;
    memory->guest_base = 0;
}

void *ssbu_guest_memory_translate(const SSBUIOSGuestMemory *memory, uint64_t address, size_t length) {
    if (!memory || !memory->host || address < memory->guest_base || length > memory->length)
        return NULL;
    uint64_t offset = address - memory->guest_base;
    if (offset > (uint64_t)(memory->length - length))
        return NULL;
    return memory->host + (size_t)offset;
}

int ssbu_guest_memory_write(SSBUIOSGuestMemory *memory, uint64_t address, const void *src, size_t length) {
    if (!src && length)
        return -1;
    void *dst = ssbu_guest_memory_translate(memory, address, length);
    if (!dst)
        return -1;
    if (length)
        memcpy(dst, src, length);
    return 0;
}

int ssbu_guest_memory_read(const SSBUIOSGuestMemory *memory, uint64_t address, void *dst, size_t length) {
    if (!dst && length)
        return -1;
    const void *src = ssbu_guest_memory_translate(memory, address, length);
    if (!src)
        return -1;
    if (length)
        memcpy(dst, src, length);
    return 0;
}

int ssbu_guest_memory_selftest(void) {
    const uint64_t base = UINT64_C(0x7100000000);
    const uint64_t value = UINT64_C(0x1122334455667788);
    uint64_t got = 0;
    int failures = 0;
    SSBUIOSGuestMemory mem = {0};

    if (ssbu_guest_memory_open(&mem, base, 4096))
        return 128;

    if (ssbu_guest_memory_write(&mem, base + 128, &value, sizeof value) ||
        ssbu_guest_memory_read(&mem, base + 128, &got, sizeof got) ||
        got != value)
        failures |= 128;

    if (ssbu_guest_memory_translate(&mem, base - 1, 1) != NULL ||
        ssbu_guest_memory_translate(&mem, base + 4093, 8) != NULL ||
        ssbu_guest_memory_translate(&mem, UINT64_MAX, 8) != NULL)
        failures |= 256;

    if ((uintptr_t)mem.host == (uintptr_t)base)
        failures |= 512;

    ssbu_guest_memory_close(&mem);
    if (mem.host || mem.length)
        failures |= 1024;
    return failures;
}
