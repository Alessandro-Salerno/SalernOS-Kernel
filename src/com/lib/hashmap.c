/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/info.h>
#include <errno.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/util.h>

#define FNV1OFFSET 0xcbf29ce484222325ull
#define FNV1PRIME  0x100000001b3ull

static inline uintmax_t get_hash(void *buffer, size_t size) {
    uint8_t *ptr = buffer;
    uint8_t *top = ptr + size;
    uint64_t h   = FNV1OFFSET;

    while (ptr < top) {
        h ^= *ptr++;
        h *= FNV1PRIME;
    }

    return h;
}

static khashmap_entry_t *
get_entry(khashmap_t *hashmap, void *key, size_t key_size, uintmax_t hash) {
    uintmax_t                    off         = hash % hashmap->capacity;
    struct khashmap_entry_tailq *entry_queue = &hashmap->entries[off];

    khashmap_entry_t *entry, *_;
    TAILQ_FOREACH_SAFE(entry, entry_queue, entries, _) {
        if (entry->key_size == key_size && entry->hash == hash &&
            0 == kmemcmp(entry->key, key, key_size)) {
            break;
        }
    }

    return entry;
}

int khashmap_init(khashmap_t *hashmap, size_t size) {
    hashmap->capacity = size;
    hashmap->entries  = (void *)ARCH_PHYS_TO_HHDM(
        com_mm_pmm_alloc_many(sizeof(khashmap_entry_t) * size / 4096 + 1));

    if (NULL == hashmap->entries) {
        return ENOMEM;
    }

    for (size_t i = 0; i < size; i++) {
        TAILQ_INIT(&hashmap->entries[i]);
    }

    return 0;
}

int khashmap_set(khashmap_t *hashmap, void *key, size_t key_size, void *value) {
    uintmax_t         hash  = get_hash(key, key_size);
    khashmap_entry_t *entry = get_entry(hashmap, key, key_size, hash);

    if (NULL != entry) {
        entry->value = value;
        return 0;
    }

    return ENOENT;
}

int khashmap_put(khashmap_t *hashmap, void *key, size_t key_size, void *value) {
    if (0 == khashmap_set(hashmap, key, key_size, value)) {
        return 0;
    }

    uintmax_t         hash  = get_hash(key, key_size);
    uintmax_t         off   = hash % hashmap->capacity;
    khashmap_entry_t *entry = com_mm_slab_alloc(sizeof(khashmap_entry_t));

    if (NULL == entry) {
        return ENOMEM;
    }

    entry->key = com_mm_slab_alloc(key_size);
    if (NULL == entry->key) {
        com_mm_slab_free(entry, sizeof(khashmap_entry_t));
        return ENOMEM;
    }

    kmemcpy(entry->key, key, key_size);
    entry->key_size                          = key_size;
    entry->value                             = value;
    entry->hash                              = hash;
    struct khashmap_entry_tailq *bucket_head = &hashmap->entries[off];
    TAILQ_INSERT_HEAD(bucket_head, entry, entries);
    hashmap->num_entries++;

    return 0;
}

int khashmap_get(void **out, khashmap_t *hashmap, void *key, size_t key_size) {
    uintmax_t         hash  = get_hash(key, key_size);
    khashmap_entry_t *entry = get_entry(hashmap, key, key_size, hash);

    if (NULL == entry) {
        return ENOENT;
    }

    *out = entry->value;
    return 0;
}

int khashmap_remove(khashmap_t *hashmap, void *key, size_t key_size) {
    uintmax_t                    hash        = get_hash(key, key_size);
    uintmax_t                    off         = hash % hashmap->capacity;
    struct khashmap_entry_tailq *bucket_head = &hashmap->entries[off];
    khashmap_entry_t *entry = get_entry(hashmap, key, key_size, hash);

    if (NULL == entry) {
        return ENOENT;
    }

    TAILQ_REMOVE(bucket_head, entry, entries);
    hashmap->num_entries--;
    return 0;
}

int khashmap_default(void      **out,
                     khashmap_t *hashmap,
                     void       *key,
                     size_t      key_size,
                     void       *default_val) {
    void *ret;
    int   get_ret = khashmap_get(&ret, hashmap, key, key_size);
    if (0 == get_ret) {
        *out = ret;
        return 0;
    } else if (ENOENT != get_ret) {
        return get_ret;
    }

    int put_ret = khashmap_put(hashmap, key, key_size, default_val);
    if (0 != put_ret) {
        return put_ret;
    }

    return khashmap_get(out, hashmap, key, key_size);
}

// TODO: implement hashmap destroy
int khashmap_destroy(khashmap_t *hashmap) {
    return 0;
}
