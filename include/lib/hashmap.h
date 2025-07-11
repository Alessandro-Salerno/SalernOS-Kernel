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

#pragma once

#include <arch/info.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

TAILQ_HEAD(khashmap_entry_tailq, khashmap_entry);

#define KHASHMAP_DEFAULT_SIZE (20000)

#define KHASHMAP_INIT(hm_ptr) khashmap_init((hm_ptr), KHASHMAP_DEFAULT_SIZE)
#define KHASHMAP_SET(hm_ptr, key, value) \
    khashmap_set((hm_ptr), key, sizeof(*(key)), value)
#define KHASHMAP_PUT(hm_ptr, key, value) \
    khashmap_put((hm_ptr), key, sizeof(*(key)), value)
#define KHASHMAP_GET(out_ptr, hm_ptr, key) \
    khashmap_get((void **)(out_ptr), (hm_ptr), key, sizeof(*(key)))
#define KHASHMAP_REMOVE(hm_ptr, key) \
    khashmap_remove((hm_ptr), key, sizeof(*(key)))
#define KHASHMAP_DEFAULT(out_ptr, hm_ptr, key, d) \
    khashmap_default((void **)(out_ptr), hm_ptr, key, sizeof(*(key)), d)

#define KHASHMAP_FOREACH(map)                                                 \
    for (uintmax_t toffset = 0; toffset < (map)->capacity; ++toffset)         \
        for (khashmap_entry_t *entry = TAILQ_FIRST(&(map)->entries[toffset]); \
             NULL != entry;                                                   \
             entry = TAILQ_NEXT(entry, entries))

typedef struct khashmap_entry {
    TAILQ_ENTRY(khashmap_entry) entries;
    uintmax_t hash;
    size_t    key_size;
    void     *key;
    void     *value;
} khashmap_entry_t;

typedef struct hashamp {
    size_t                       capacity;
    size_t                       num_entries;
    struct khashmap_entry_tailq *entries;
} khashmap_t;

int khashmap_init(khashmap_t *hashmap, size_t size);
int khashmap_set(khashmap_t *hashmap, void *key, size_t key_size, void *value);
int khashmap_put(khashmap_t *hashmap, void *key, size_t key_size, void *value);
int khashmap_get(void **out, khashmap_t *hashmap, void *key, size_t key_size);
int khashmap_default(void      **out,
                     khashmap_t *hashmap,
                     void       *key,
                     size_t      key_size,
                     void       *default_val);
int khashmap_remove(khashmap_t *hashmap, void *key, size_t key_size);
int khashmap_destroy(khashmap_t *hashmap);
