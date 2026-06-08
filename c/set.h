/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * AIS (2026): posting set = sorted, unique array of record ids.
 * The set algebra (intersect = AND, union = OR) is the core of retrieval.
 */

#ifndef _SET_H
#define _SET_H

#include "common.h"

typedef struct set {
	long *ids;   /* sorted, unique */
	long  n;
	long  cap;
} set;

set* set_new(void);
void set_free(set* s);
void set_add(set* s, long id);
void set_remove(set* s, long id);
BOOL set_contains(set* s, long id);
set* set_clone(set* s);
set* set_intersect(set* a, set* b);
set* set_union(set* a, set* b);

#endif
