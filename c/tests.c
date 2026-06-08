/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * ============================================================================
 *  AIS regression tests -- ALL CURRENT TESTS ARE HERE: ADD MORE!
 * ============================================================================
 *  Style mirrors the KUL project's Tests.java: a flat, inline list of small
 *  feature tests, each with a comment, run as a batch by `make ut`.
 *
 *  Tests are IDEMPOTENT (KUL rule): in-memory tests allocate and free; db
 *  tests use a temp file (UT_DB) and remove() it before and after, so the
 *  suite can be run anywhere, repeatedly, leaving nothing behind.
 *
 *  Covered so far:
 *    set : add (sorted+unique), contains, remove, intersect (AND), union (OR)
 *    db  : put/get AND, get OR, value lookup, delete, persistence (reload),
 *          empty-keys record (memo) survives save/reload,
 *          multi-link records (several values per record) persist + reload
 *    seed: read-only queries against the committed testdata/seed.db fixture
 *
 *  Build & run:   make ut && ./ais        (then `make` to rebuild the real CLI)
 *  Exit code is non-zero if any check fails (CI-friendly).
 * ============================================================================
 */

#ifdef UNIT_TEST

#include "common.h"
#include "set.h"
#include "db.h"

#define UT_DB "ut_tmp.db"
#define SEED_DB "../testdata/seed.db"   /* repo-level fixture; cwd is c/ under make ut */

static int ut_pass = 0;
static int ut_fail = 0;

/* flat assert: one line per check; never aborts, so every test still runs */
#define CHECK(cond, msg) do { \
		if(cond){ ut_pass++; printf("  ok   %s\n", (msg)); } \
		else    { ut_fail++; printf("  FAIL %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
	} while(0)

/* ---- set: add keeps the array sorted and unique ---- */
static void test_set_add_sorted_unique(void){
	set* s = set_new();
	set_add(s, 5); set_add(s, 1); set_add(s, 3); set_add(s, 1); /* 1 is a dup */
	CHECK(s->n == 3, "set_add dedups (n==3)");
	CHECK(s->ids[0]==1 && s->ids[1]==3 && s->ids[2]==5, "set_add stays sorted");
	CHECK(set_contains(s, 3) == TRUE,  "set_contains finds a present id");
	CHECK(set_contains(s, 9) == FALSE, "set_contains rejects an absent id");
	set_free(s);
}

/* ---- set: remove drops an element and is a no-op when absent ---- */
static void test_set_remove(void){
	set* s = set_new();
	set_add(s,1); set_add(s,2); set_add(s,3);
	set_remove(s, 2);
	CHECK(s->n==2 && s->ids[0]==1 && s->ids[1]==3, "set_remove drops middle, keeps order");
	set_remove(s, 42);
	CHECK(s->n==2, "set_remove of an absent id is a no-op");
	set_free(s);
}

/* ---- set: intersect == AND ---- */
static void test_set_intersect(void){
	set* a=set_new(); set* b=set_new(); set* r;
	set_add(a,1); set_add(a,2); set_add(a,3);
	set_add(b,2); set_add(b,3); set_add(b,4);
	r = set_intersect(a,b);
	CHECK(r->n==2 && r->ids[0]==2 && r->ids[1]==3, "intersect {1,2,3} & {2,3,4} == {2,3}");
	set_free(a); set_free(b); set_free(r);
}

/* ---- set: union == OR (sorted, deduped) ---- */
static void test_set_union(void){
	set* a=set_new(); set* b=set_new(); set* r;
	set_add(a,1); set_add(a,3);
	set_add(b,2); set_add(b,3);
	r = set_union(a,b);
	CHECK(r->n==3 && r->ids[0]==1 && r->ids[1]==2 && r->ids[2]==3, "union {1,3} | {2,3} == {1,2,3}");
	set_free(a); set_free(b); set_free(r);
}

/* ---- db: put then get with AND (intersection of keys) ---- */
static void test_db_put_get_and(void){
	db* d; set* r; long id1; char* k1[1]; char* k2[2];
	remove(UT_DB);
	d = db_open(UT_DB);
	id1 = db_put(d, "samba config network", "uri1");
	(void)db_put(d, "nfs network", "uri2");
	k1[0] = "samba";
	r = db_get(d, k1, 1, FALSE);
	CHECK(r->n==1 && r->ids[0]==id1, "get [samba] -> {1}");
	set_free(r);
	k2[0] = "network"; k2[1] = "nfs";
	r = db_get(d, k2, 2, FALSE);   /* AND */
	CHECK(r->n==1 && r->ids[0]==2, "get AND [network nfs] -> {2}");
	set_free(r);
	db_close(d);
	remove(UT_DB);
}

/* ---- db: get with OR (union of keys) ---- */
static void test_db_get_or(void){
	db* d; set* r; char* k[2];
	remove(UT_DB);
	d = db_open(UT_DB);
	(void)db_put(d, "samba", "u1");
	(void)db_put(d, "nfs",   "u2");
	k[0] = "samba"; k[1] = "nfs";
	r = db_get(d, k, 2, TRUE);     /* OR */
	CHECK(r->n==2, "get OR [samba nfs] -> 2 records");
	set_free(r);
	db_close(d);
	remove(UT_DB);
}

/* ---- db: value lookup and delete (and its effect on retrieval) ---- */
static void test_db_value_and_delete(void){
	db* d; const char* v; set* r; long id1; char* k[1];
	remove(UT_DB);
	d = db_open(UT_DB);
	id1 = db_put(d, "a b", "val1");
	(void)db_put(d, "b c", "val2");
	v = db_value(d, id1);
	CHECK(v!=NULL && strcmp(v,"val1")==0, "db_value returns the stored value");
	CHECK(db_delete(d, id1)==TRUE,  "db_delete of an existing id -> TRUE");
	CHECK(db_delete(d, 999)==FALSE, "db_delete of a missing id -> FALSE");
	k[0] = "a";
	r = db_get(d, k, 1, FALSE);
	CHECK(r->n==0, "after delete, key 'a' yields nothing");
	set_free(r);
	db_close(d);
	remove(UT_DB);
}

/* ---- db: persistence -- a saved db reloads with its index and maxid ---- */
static void test_db_persistence(void){
	db* d; set* r; char* k[1];
	remove(UT_DB);
	d = db_open(UT_DB);
	(void)db_put(d, "alpha beta", "v");
	db_save(d);
	db_close(d);
	d = db_open(UT_DB);            /* reopen */
	k[0] = "beta";
	r = db_get(d, k, 1, FALSE);
	CHECK(r->n==1, "reloaded db finds the record by key");
	CHECK(d->maxid==1, "reloaded db restores maxid");
	set_free(r);
	db_close(d);
	remove(UT_DB);
}

/* ---- db: a keyless memo (empty keys) survives save/reload ---- */
static void test_db_empty_keys(void){
	db* d;
	remove(UT_DB);
	d = db_open(UT_DB);
	(void)db_put(d, "", "just a memo");
	db_save(d);
	db_close(d);
	d = db_open(UT_DB);
	CHECK(d->nrecs==1, "empty-keys record persists");
	CHECK(d->recs[0].nvalues==1 && strcmp(d->recs[0].values[0],"just a memo")==0, "empty-keys value intact");
	db_close(d);
	remove(UT_DB);
}

/* ---- db: multi-link record -- one record holds several values (links/URIs) ---- */
static void test_db_multilink(void){
	db* d; long id; char* k[1]; set* r;
	remove(UT_DB);
	d = db_open(UT_DB);
	id = db_put(d, "project docs", "https://a");
	CHECK(db_add(d, id, "https://b")==TRUE, "db_add appends a link to a record");
	CHECK(db_add(d, id, "file:///c")==TRUE, "db_add a second link");
	CHECK(db_add(d, 999, "x")==FALSE,        "db_add to a missing id -> FALSE");
	CHECK(db_nvalues(d, id)==3, "record now carries 3 links");
	CHECK(db_value(d,id) && strcmp(db_value(d,id),"https://a")==0, "db_value returns the first link");
	CHECK(db_value_at(d,id,2) && strcmp(db_value_at(d,id,2),"file:///c")==0, "db_value_at(2) == third link");
	CHECK(db_value_at(d,id,3)==NULL, "db_value_at past the end -> NULL");
	db_save(d);
	db_close(d);
	d = db_open(UT_DB);              /* reload: all links survive, in order */
	k[0] = "docs";
	r = db_get(d, k, 1, FALSE);
	CHECK(r->n==1 && r->ids[0]==id, "reloaded: key still finds the record");
	CHECK(db_nvalues(d, id)==3, "reloaded: all 3 links persisted");
	CHECK(db_value_at(d,id,1) && strcmp(db_value_at(d,id,1),"https://b")==0, "reloaded: link order preserved");
	set_free(r);
	db_close(d);
	remove(UT_DB);
}

/* ---- seed fixture: read-only queries against the committed sample db ---- */
/* Idempotent by construction: opens the fixture read-only (no save/delete),
 * so the committed testdata/seed.db is never modified. Gives the suite real
 * data and shows contributors the store format. Run via `make ut` (cwd = c/). */
static void test_seed_fixture(void){
	db* d; set* r; const char* v; char* k1[1]; char* k2[2];
	d = db_open(SEED_DB);
	if(d->nrecs == 0){
		ut_fail++;
		printf("  FAIL seed not loaded -- run via `make ut` (expects %s)\n", SEED_DB);
		db_close(d);
		return;
	}
	CHECK(d->nrecs == 6, "seed loads 6 records");
	CHECK(d->maxid  == 6, "seed maxid == 6");
	k2[0]="c"; k2[1]="ansi";
	r = db_get(d, k2, 2, FALSE);
	CHECK(r->n==2 && r->ids[0]==3 && r->ids[1]==4, "seed AND [c ansi] -> {3,4}");
	set_free(r);
	k2[0]="linux"; k2[1]="network";
	r = db_get(d, k2, 2, FALSE);
	CHECK(r->n==2 && r->ids[0]==1 && r->ids[1]==2, "seed AND [linux network] -> {1,2}");
	set_free(r);
	k1[0]="samba";
	r = db_get(d, k1, 1, FALSE);
	CHECK(r->n==1 && r->ids[0]==1, "seed [samba] -> {1}");
	set_free(r);
	v = db_value(d, 1);
	CHECK(v && strcmp(v, "/etc/samba/smb.conf")==0, "seed value(1) == /etc/samba/smb.conf");
	k2[0]="samba"; k2[1]="nfs";
	r = db_get(d, k2, 2, TRUE);
	CHECK(r->n==2, "seed OR [samba nfs] -> 2 records");
	set_free(r);
	db_close(d);
}

int main(void){
	printf("AIS regression tests (make ut)\n");
	printf("set:\n");
	test_set_add_sorted_unique();
	test_set_remove();
	test_set_intersect();
	test_set_union();
	printf("db:\n");
	test_db_put_get_and();
	test_db_get_or();
	test_db_value_and_delete();
	test_db_persistence();
	test_db_empty_keys();
	test_db_multilink();
	printf("seed (testdata/seed.db):\n");
	test_seed_fixture();
	printf("----\n%d passed, %d failed\n", ut_pass, ut_fail);
	return ut_fail == 0 ? 0 : 1;
}

#endif /* UNIT_TEST */

/* keep this translation unit non-empty in normal (non-UNIT_TEST) builds so
 * -pedantic does not warn about an empty file. */
typedef int ais_tests_translation_unit_not_empty;
