/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * AIS (2026): key -> posting set (via the hash), id -> record.
 * A record carries a SET of values (links / URIs / resources): "multi-link",
 * the associative-graph model of the original (key/record -> several resources,
 * as the 2005 shell scripts appended several lines under one key).
 * v0 backend = flat text file, pipe-delimited (id|keys|value1|value2|...|valueN).
 * This struct is the seam where a vtable of backends (sqlite / lmdb / bdb / remote) plugs in.
 */

#ifndef _DB_H
#define _DB_H

#include "common.h"
#include "hash.h"
#include "set.h"

typedef struct rec {
	long   id;
	char*  keys;
	char** values;    /* the record's links (URIs / resources) */
	int    nvalues;
	int    vcap;
} rec;

typedef struct db {
	struct hash* key2set;   /* key -> set* of record ids (the key index) */
	rec*  recs;             /* id -> record (linear store, v0) */
	long  nrecs;
	long  cap;
	long  maxid;
	char* path;
} db;

db*  db_open(const char* path);
void db_close(db* d);
long db_put(db* d, const char* keys, const char* value);   /* new record, one value; returns id */
BOOL db_add(db* d, long id, const char* value);            /* append a link (value) to a record */
set* db_get(db* d, char** keys, int nkeys, BOOL or_mode);  /* AND, or OR */
const char* db_value(db* d, long id);                      /* first link (compatibility) */
int  db_nvalues(db* d, long id);                           /* number of links on a record */
const char* db_value_at(db* d, long id, int i);            /* i-th link, or NULL */
BOOL db_delete(db* d, long id);
void db_dump(db* d, FILE* out);
int  db_save(db* d);

#endif
