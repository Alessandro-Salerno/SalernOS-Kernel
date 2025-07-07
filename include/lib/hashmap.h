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

TAILQ_HEAD(hashmap_entry_tailq, hashmap_entry);

#define HASHMAP_DEFAULT_SIZE \
    (ARCH_PAGE_SIZE / sizeof(struct hashmap_entry_tailq))

#define HASHMAP_INIT(hm_ptr) hashmap_init((hm_ptr), HASHMAP_DEFAULT_SIZE)
#define HASHMAP_SET(hm_ptr, key, value) \
    hashmap_set((hm_ptr), key, sizeof(*(key)), value)
#define HASHMAP_PUT(hm_ptr, key, value) \
    hashmap_put((hm_ptr), key, sizeof(*(key)), value)
#define HASHMAP_GET(out_ptr, hm_ptr, key) \
    hashmap_get((void **)(out_ptr), (hm_ptr), key, sizeof(*(key)))
#define HASHMAP_REMOVE(hm_ptr, key) \
    hashmap_remove((hm_ptr), key, sizeof(*(key)))
#define HASHMAP_DEFAULT(out_ptr, hm_ptr, key, d) \
    hashmap_default((void **)(out_ptr), hm_ptr, key, sizeof(*(key)), d)

typedef struct hashmap_entry {
    TAILQ_ENTRY(hashmap_entry) entries;
    uintmax_t hash;
    size_t    key_size;
    void     *key;
    void     *value;
} hashmap_entry_t;

typedef struct hashamp {
    size_t                      capacity;
    size_t                      num_entries;
    struct hashmap_entry_tailq *entries;
} hashmap_t;

int hashmap_init(hashmap_t *hashmap, size_t size);
int hashmap_set(hashmap_t *hashmap, void *key, size_t key_size, void *value);
int hashmap_put(hashmap_t *hashmap, void *key, size_t key_size, void *value);
int hashmap_get(void **out, hashmap_t *hashmap, void *key, size_t key_size);
int hashmap_default(void     **out,
                    hashmap_t *hashmap,
                    void      *key,
                    size_t     key_size,
                    void      *default_val);
int hashmap_remove(hashmap_t *hashmap, void *key, size_t key_size);
int hashmap_destroy(hashmap_t *hashmap);
