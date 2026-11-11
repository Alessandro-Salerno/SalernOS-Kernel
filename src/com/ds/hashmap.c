#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>

#define FNV1OFFSET 0xcbf29ce484222325ULL
#define FNV1PRIME  0x100000001b3ULL

static inline u64 get_hash(void *buffer, usize size) {
    u8 *ptr = buffer;
    u8 *top = ptr + size;
    u64 h = FNV1OFFSET;

    while (ptr < top) {
        h ^= *ptr++;
        h *= FNV1PRIME;
    }

    return h;
}

static HashmapEntry *get_entry(Hashmap *hashmap, void *key, usize key_size,
                               u64 hash) {
    u64 off = hash & (hashmap->capacity - 1);
    struct HashmapEntryTailQ *entry_queue = &hashmap->entries[off];

    HashmapEntry *entry, *_;
    TAILQ_FOREACH_SAFE(entry, entry_queue, entries, _) {
        if (entry->key_size == key_size && entry->hash == hash &&
            memcmp(entry->key, key, key_size) == 0) {
            break;
        }
    }

    return entry;
}

static usize bucket_pages(usize capacity) {
    usize size = capacity * sizeof(struct HashmapEntryTailQ);
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

static void entry_free(HashmapEntry *entry) {
    slab_free(entry->key, entry->key_size);
    slab_free(entry, sizeof(HashmapEntry));
}

int hashmap_init(Hashmap *hashmap, usize size) {
    usize capacity = 1;
    while (capacity < size) capacity *= 2;

    void *phys = pmm_allocn(bucket_pages(capacity));
    if (phys == NULL) return ENOMEM;

    hashmap->capacity = capacity;
    hashmap->num_entries = 0;
    hashmap->entries = (void *)PHYS_TO_HHDM(phys);
    for (usize i = 0; i < capacity; i++) TAILQ_INIT(&hashmap->entries[i]);

    return 0;
}

int hashmap_set(Hashmap *hashmap, void *key, usize key_size, void *value) {
    u64 hash = get_hash(key, key_size);
    HashmapEntry *entry = get_entry(hashmap, key, key_size, hash);

    if (entry != NULL) {
        entry->value = value;
        return 0;
    }

    return ENOENT;
}

int hashmap_put(Hashmap *hashmap, void *key, usize key_size, void *value) {
    if (hashmap_set(hashmap, key, key_size, value) == 0) return 0;

    u64 hash = get_hash(key, key_size);
    u64 off = hash & (hashmap->capacity - 1);
    HashmapEntry *entry = slab_alloc(sizeof(HashmapEntry));

    if (entry == NULL) return ENOMEM;

    entry->key = slab_alloc(key_size);
    if (entry->key == NULL) {
        slab_free(entry, sizeof(HashmapEntry));
        return ENOMEM;
    }

    memcpy(entry->key, key, key_size);
    entry->key_size = key_size;
    entry->value = value;
    entry->hash = hash;
    struct HashmapEntryTailQ *bucket_head = &hashmap->entries[off];
    TAILQ_INSERT_HEAD(bucket_head, entry, entries);
    hashmap->num_entries++;

    return 0;
}

int hashmap_get(Hashmap *hashmap, void *key, usize key_size, void **out_ptr) {
    u64 hash = get_hash(key, key_size);
    HashmapEntry *entry = get_entry(hashmap, key, key_size, hash);

    if (entry == NULL) return ENOENT;

    *out_ptr = entry->value;
    return 0;
}

int hashmap_remove(Hashmap *hashmap, void *key, usize key_size) {
    u64 hash = get_hash(key, key_size);
    u64 off = hash & (hashmap->capacity - 1);
    struct HashmapEntryTailQ *bucket_head = &hashmap->entries[off];
    HashmapEntry *entry = get_entry(hashmap, key, key_size, hash);

    if (entry == NULL) return ENOENT;

    TAILQ_REMOVE(bucket_head, entry, entries);
    hashmap->num_entries--;
    entry_free(entry);
    return 0;
}

int hashmap_default(Hashmap *hashmap, void *key, usize key_size,
                    void *default_val, void **out_ptr) {
    void *ret;
    int get_ret = hashmap_get(hashmap, key, key_size, &ret);
    if (get_ret == 0) {
        *out_ptr = ret;
        return 0;
    } else if (get_ret != ENOENT) {
        return get_ret;
    }

    int put_ret = hashmap_put(hashmap, key, key_size, default_val);
    if (put_ret != 0) return put_ret;

    return hashmap_get(hashmap, key, key_size, out_ptr);
}

int hashmap_destroy(Hashmap *hashmap) {
    for (usize i = 0; i < hashmap->capacity; i++) {
        HashmapEntry *entry, *next;
        TAILQ_FOREACH_SAFE(entry, &hashmap->entries[i], entries, next) {
            entry_free(entry);
        }
    }

    pmm_freen((void *)HHDM_TO_PHYS(hashmap->entries),
              bucket_pages(hashmap->capacity));
    hashmap->entries = NULL;
    hashmap->capacity = 0;
    hashmap->num_entries = 0;
    return 0;
}
