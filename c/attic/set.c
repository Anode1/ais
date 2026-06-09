/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 * GPL v2 or later.
 */

#include "set.h"

/* binary search: returns index of id, or -(insert_pos)-1 if absent */
static long set_bsearch(set* s, long id){
	long lo = 0, hi = s->n - 1;
	while(lo <= hi){
		long mid = (lo + hi) / 2;
		if(s->ids[mid] == id) return mid;
		if(s->ids[mid] <  id) lo = mid + 1;
		else                  hi = mid - 1;
	}
	return -(lo) - 1;
}

static void set_grow(set* s){
	if(s->n < s->cap) return;
	s->cap = s->cap ? s->cap * 2 : 8;
	s->ids = (long*)realloc(s->ids, sizeof(long) * s->cap);
	if(s->ids == NULL) OUT_OF_MEM();
}

set* set_new(void){
	set* s = (set*)malloc(sizeof(set));
	if(s == NULL) OUT_OF_MEM();
	s->ids = NULL; s->n = 0; s->cap = 0;
	return s;
}

void set_free(set* s){
	if(s == NULL) return;
	free(s->ids);
	free(s);
}

void set_add(set* s, long id){
	long pos = set_bsearch(s, id);
	long i;
	if(pos >= 0) return;          /* already present */
	pos = -(pos) - 1;
	set_grow(s);
	for(i = s->n; i > pos; i--) s->ids[i] = s->ids[i-1];
	s->ids[pos] = id;
	s->n++;
}

void set_remove(set* s, long id){
	long pos = set_bsearch(s, id);
	long i;
	if(pos < 0) return;
	for(i = pos; i < s->n - 1; i++) s->ids[i] = s->ids[i+1];
	s->n--;
}

BOOL set_contains(set* s, long id){
	return set_bsearch(s, id) >= 0 ? TRUE : FALSE;
}

set* set_clone(set* s){
	set* r = set_new();
	if(s->n > 0){
		r->ids = (long*)malloc(sizeof(long) * s->n);
		if(r->ids == NULL) OUT_OF_MEM();
		memcpy(r->ids, s->ids, sizeof(long) * s->n);
		r->n = s->n; r->cap = s->n;
	}
	return r;
}

/* both arrays sorted -> linear merge intersection */
set* set_intersect(set* a, set* b){
	set* r = set_new();
	long i = 0, j = 0;
	while(i < a->n && j < b->n){
		if(a->ids[i] == b->ids[j]){ set_add(r, a->ids[i]); i++; j++; }
		else if(a->ids[i] < b->ids[j]) i++;
		else j++;
	}
	return r;
}

set* set_union(set* a, set* b){
	set* r = set_clone(a);
	long j;
	for(j = 0; j < b->n; j++) set_add(r, b->ids[j]);
	return r;
}
