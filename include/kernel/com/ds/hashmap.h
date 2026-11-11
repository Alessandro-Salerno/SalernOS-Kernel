#pragma once

#include <arch/info.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

TAILQ_HEAD(HashmapEntryTailQ, HashmapEntry);

#define HASHMAP_DEFAULT_SIZE 16384

#define HASHMAP_INIT(hm_ptr) hashmap_init((hm_ptr), HASHMAP_DEFAULT_SIZE)
#define HASHMAP_SET(hm_ptr, key, value) \
    hashmap_set((hm_ptr), key, sizeof(*(key)), value)
#define HASHMAP_PUT(hm_ptr, key, value) \
    hashmap_put((hm_ptr), key, sizeof(*(key)), value)
#define HASHMAP_GET(out_ptr, hm_ptr, key) \
    hashmap_get((hm_ptr), key, sizeof(*(key)), (void **)(out_ptr))
#define HASHMAP_REMOVE(hm_ptr, key) \
    hashmap_remove((hm_ptr), key, sizeof(*(key)))
#define HASHMAP_DEFAULT(out_ptr, hm_ptr, key, d) \
    hashmap_default(hm_ptr, key, sizeof(*(key)), d, (void **)(out_ptr))

#define HASHMAP_FOREACH(map)                                              \
    for (u64 toffset = 0; toffset < (map)->capacity; ++toffset)           \
        for (HashmapEntry *entry = TAILQ_FIRST(&(map)->entries[toffset]); \
             entry != NULL; entry = TAILQ_NEXT(entry, entries))

typedef struct HashmapEntry {
    TAILQ_ENTRY(HashmapEntry) entries;
    u64 hash;
    usize key_size;
    void *key;
    void *value;
} HashmapEntry;

typedef struct Hashmap {
    usize capacity;
    usize num_entries;
    struct HashmapEntryTailQ *entries;
} Hashmap;

// NOTE: size is rounded up to a power of 2, buckets are whole pages
int hashmap_init(Hashmap *hashmap, usize size);
int hashmap_set(Hashmap *hashmap, void *key, usize key_size, void *value);
int hashmap_put(Hashmap *hashmap, void *key, usize key_size, void *value);
int hashmap_get(Hashmap *hashmap, void *key, usize key_size, void **out_ptr);
int hashmap_default(Hashmap *hashmap, void *key, usize key_size,
                    void *default_val, void **out_ptr);
int hashmap_remove(Hashmap *hashmap, void *key, usize key_size);
int hashmap_destroy(Hashmap *hashmap);
