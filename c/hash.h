/**
 * Copyright (C) 2001 Vasili Gavrilov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _HASH_H
#define _HASH_H

#include "common.h"

struct hash;

struct hash* hash_create(long size);

void* hash_put(struct hash *table, char* key, void* data);

void* hash_get(struct hash *table, char* key);

/* Returns pointer to the data which should be deallocated by the caller */
void* hash_delete_entry(struct hash *table, char* key);

void hash_delete(struct hash *table);

/* Callback for each element of the hash */
void hash_call(struct hash *table, void (*func) (void*));

/* Print all keys in stored order - useful for debugging */
void hash_print_all_keys(struct hash* table);

#endif
