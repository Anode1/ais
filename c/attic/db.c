/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 * GPL v2 or later.
 *
 * AIS (2026): key -> posting set (the key index); id -> record.
 * A record carries a SET of values (links / URIs / resources) -- "multi-link",
 * the associative-graph model of the original (one key/record -> several resources).
 * v0 backend = flat text file: id|keys|value1|value2|...|valueN
 * Field delimiter is the pipe '|' (robust for hand-editing: visually distinct, and a
 * conformant URI encodes a literal pipe as %7C, so values do not contain a raw '|').
 * Keys within the keys field are space-separated.
 */

#include "common.h"
#include "hash.h"
#include "db.h"

#define DB_LINE_MAX 65536

/* forward declarations */
static void index_keys(db* d, long id, const char* keys);
static int  db_load(db* d);
static void set_free_void(void* p);

static void set_free_void(void* p){ set_free((set*)p); }

db* db_open(const char* path){
	db* d = (db*)malloc(sizeof(db));
	if(d == NULL) OUT_OF_MEM();
	d->key2set = hash_create(4099);
	d->recs = NULL; d->nrecs = 0; d->cap = 0; d->maxid = 0;
	d->path = strdup(path);
	db_load(d);
	return d;
}

static void rec_grow(db* d){
	if(d->nrecs < d->cap) return;
	d->cap = d->cap ? d->cap * 2 : 16;
	d->recs = (rec*)realloc(d->recs, sizeof(rec) * d->cap);
	if(d->recs == NULL) OUT_OF_MEM();
}

/* append one value (link) to a record's value list */
static void rec_push_value(rec* r, const char* value){
	if(r->nvalues >= r->vcap){
		r->vcap = r->vcap ? r->vcap * 2 : 4;
		r->values = (char**)realloc(r->values, sizeof(char*) * r->vcap);
		if(r->values == NULL) OUT_OF_MEM();
	}
	r->values[r->nvalues++] = strdup(value ? value : "");
}

static void rec_free_values(rec* r){
	int k;
	for(k = 0; k < r->nvalues; k++) free(r->values[k]);
	free(r->values);
	r->values = NULL; r->nvalues = 0; r->vcap = 0;
}

/* create a new (value-less) record and return it; the caller pushes values.
 * Valid until the next rec_new (which may realloc d->recs); rec_push_value does not. */
static rec* rec_new(db* d, long id, const char* keys){
	rec* r;
	rec_grow(d);
	r = &d->recs[d->nrecs];
	r->id      = id;
	r->keys    = strdup(keys ? keys : "");
	r->values  = NULL;
	r->nvalues = 0;
	r->vcap    = 0;
	d->nrecs++;
	if(id > d->maxid) d->maxid = id;
	return r;
}

static rec* rec_find(db* d, long id){
	long i;
	for(i = 0; i < d->nrecs; i++)
		if(d->recs[i].id == id) return &d->recs[i];
	return NULL;
}

/* tokenize keys, add id to each key's posting set */
static void index_keys(db* d, long id, const char* keys){
	char* dup = strdup(keys ? keys : "");
	char* tok;
	for(tok = strtok(dup, " \t\r\n"); tok != NULL; tok = strtok(NULL, " \t\r\n")){
		set* s = (set*)hash_get(d->key2set, tok);
		if(s == NULL){ s = set_new(); hash_put(d->key2set, tok, s); }
		set_add(s, id);
	}
	free(dup);
}

long db_put(db* d, const char* keys, const char* value){
	long id = ++d->maxid;
	rec* r  = rec_new(d, id, keys);
	rec_push_value(r, value);
	index_keys(d, id, keys);
	return id;
}

BOOL db_add(db* d, long id, const char* value){
	rec* r = rec_find(d, id);
	if(r == NULL) return FALSE;
	rec_push_value(r, value);
	return TRUE;
}

set* db_get(db* d, char** keys, int nkeys, BOOL or_mode){
	set* result = NULL;
	int i;
	for(i = 0; i < nkeys; i++){
		set* s = (set*)hash_get(d->key2set, keys[i]);
		if(or_mode){
			if(s == NULL) continue;
			if(result == NULL) result = set_clone(s);
			else { set* t = set_union(result, s); set_free(result); result = t; }
		} else {
			if(s == NULL){ if(result) set_free(result); return set_new(); }
			if(result == NULL) result = set_clone(s);
			else { set* t = set_intersect(result, s); set_free(result); result = t; }
		}
	}
	if(result == NULL) result = set_new();
	return result;
}

const char* db_value(db* d, long id){
	rec* r = rec_find(d, id);
	return (r && r->nvalues) ? r->values[0] : NULL;
}

int db_nvalues(db* d, long id){
	rec* r = rec_find(d, id);
	return r ? r->nvalues : 0;
}

const char* db_value_at(db* d, long id, int i){
	rec* r = rec_find(d, id);
	if(r == NULL || i < 0 || i >= r->nvalues) return NULL;
	return r->values[i];
}

BOOL db_delete(db* d, long id){
	long i;
	for(i = 0; i < d->nrecs; i++){
		if(d->recs[i].id == id){
			char* dup = strdup(d->recs[i].keys);
			char* tok;
			for(tok = strtok(dup, " \t\r\n"); tok != NULL; tok = strtok(NULL, " \t\r\n")){
				set* s = (set*)hash_get(d->key2set, tok);
				if(s) set_remove(s, id);
			}
			free(dup);
			free(d->recs[i].keys);
			rec_free_values(&d->recs[i]);
			memmove(&d->recs[i], &d->recs[i+1], sizeof(rec) * (d->nrecs - i - 1));
			d->nrecs--;
			return TRUE;
		}
	}
	return FALSE;
}

/* one record per line: id|keys|value1|value2|... */
static void rec_print(const rec* r, FILE* out){
	int k;
	fprintf(out, "%ld|%s", r->id, r->keys);
	for(k = 0; k < r->nvalues; k++) fprintf(out, "|%s", r->values[k]);
	fprintf(out, "\n");
}

void db_dump(db* d, FILE* out){
	long i;
	for(i = 0; i < d->nrecs; i++) rec_print(&d->recs[i], out);
}

int db_save(db* d){
	FILE* fp;
	long i;
	errno = 0;
	fp = fopen(d->path, "w");
	if(fp == NULL){ fprintf(stderr, "ais: can't write %s: %s\n", d->path, strerror(errno)); return -1; }
	for(i = 0; i < d->nrecs; i++) rec_print(&d->recs[i], fp);
	fclose(fp);
	return 0;
}

/* flat file: id|keys|value1|value2|... ; split on '|' manually so empty keys survive */
static int db_load(db* d){
	FILE* fp = fopen(d->path, "r");
	char* line;
	if(fp == NULL) return 0;   /* no file yet -> empty db */
	line = (char*)malloc(DB_LINE_MAX);
	if(line == NULL) OUT_OF_MEM();
	while(fgets(line, DB_LINE_MAX, fp) != NULL){
		char* t1; char* keys; char* t2; char* p; long id; rec* r;
		rtrim(line, '\n'); rtrim(line, '\r');
		t1 = strchr(line, '|');
		if(t1 == NULL) continue;
		*t1 = '\0';
		id   = atol(line);
		keys = t1 + 1;
		t2   = strchr(keys, '|');
		if(t2 != NULL){ *t2 = '\0'; p = t2 + 1; } else { p = NULL; }
		r = rec_new(d, id, keys);
		if(p != NULL){
			char* sep;
			while((sep = strchr(p, '|')) != NULL){ *sep = '\0'; rec_push_value(r, p); p = sep + 1; }
			rec_push_value(r, p);   /* final field (may be "") */
		}
		index_keys(d, id, keys);
	}
	free(line);
	fclose(fp);
	return 0;
}

void db_close(db* d){
	long i;
	if(d == NULL) return;
	hash_call(d->key2set, set_free_void);
	hash_delete(d->key2set);
	for(i = 0; i < d->nrecs; i++){ free(d->recs[i].keys); rec_free_values(&d->recs[i]); }
	free(d->recs);
	free(d->path);
	free(d);
}
