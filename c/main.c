/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * AIS (2026): POSIX argv front end. verb + keys; value as arg or on stdin.
 * Designed to be exec'd from a web layer over stdin/stdout (the old pattern).
 *
 * main() is wrapped in #ifndef UNIT_TEST so `make ut` can substitute the
 * regression-test main() from tests.c. The debug/trace globals stay outside
 * the guard so the test build links them.
 */

#include "common.h"
#include "db.h"
#include <getopt.h>

#define KEYS_BUF_MAX 65536

int debug = 0;
int trace = 0;

#ifndef UNIT_TEST

static void help(void);

int main(int argc, char** argv){
	int   c;
	char* path = "ais.db";
	BOOL  or_mode = FALSE;
	char* verb;
	db*   d;

	while((c = getopt(argc, argv, "f:odh")) != -1){
		switch(c){
			case 'f': path = optarg;  break;
			case 'o': or_mode = TRUE; break;
			case 'd': debug = 1;      break;
			case 'h': help(); return 0;
			default : help(); return -1;
		}
	}

	if(optind >= argc){ help(); return -1; }
	verb = argv[optind++];

	d = db_open(path);

	if(strcmp(verb, "put") == 0){
		char  keysbuf[KEYS_BUF_MAX];
		char* value;
		long  id;
		int    i;
		size_t used = 0;
		keysbuf[0] = '\0';
		if(optind < argc && strcmp(argv[optind], "-") != 0){
			value = argv[optind++];
			for(i = 0; optind < argc; optind++){
				const char* k    = argv[optind];
				size_t      klen = strlen(k);
				size_t      need = klen + (i ? 1 : 0);  /* +1 for separating space */
				if(used + need >= sizeof(keysbuf)){      /* keep room for the '\0' */
					fprintf(stderr, "ais: key list exceeds %d bytes\n", KEYS_BUF_MAX - 1);
					db_close(d);
					return -1;
				}
				if(i++) keysbuf[used++] = ' ';
				memcpy(keysbuf + used, k, klen);
				used += klen;
				keysbuf[used] = '\0';
			}
			id = db_put(d, keysbuf, value);
		} else {
			/* stdin, the old pipe idiom: first token = value (URI), rest = keys */
			static char line[KEYS_BUF_MAX];
			char* sp;
			if(fgets(line, sizeof(line), stdin) == NULL){
				fprintf(stderr, "ais: nothing on stdin\n");
				db_close(d);
				return -1;
			}
			rtrim(line, '\n');
			rtrim(line, '\r');
			sp = strchr(line, ' ');
			if(sp){ *sp = '\0'; value = line; id = db_put(d, sp + 1, value); }
			else  { value = line;             id = db_put(d, "", value); }
		}
		db_save(d);
		printf("%ld\n", id);
	}
	else if(strcmp(verb, "add") == 0){
		/* attach another link (value) to an existing record: add <id> <value> */
		if(optind + 1 < argc){
			long id = atol(argv[optind]);
			if(db_add(d, id, argv[optind + 1])){ db_save(d); printf("%ld\n", id); }
			else fprintf(stderr, "ais: no record id %ld\n", id);
		} else {
			fprintf(stderr, "ais: add needs <id> <value>\n");
		}
	}
	else if(strcmp(verb, "get") == 0){
		set* r = db_get(d, &argv[optind], argc - optind, or_mode);
		long i;
		for(i = 0; i < r->n; i++){
			long id = r->ids[i];
			int  nv = db_nvalues(d, id), k;
			if(nv == 0) printf("%ld|\n", id);               /* keyed record, no links yet */
			for(k = 0; k < nv; k++)
				printf("%ld|%s\n", id, db_value_at(d, id, k));   /* one line per link */
		}
		set_free(r);
	}
	else if(strcmp(verb, "del") == 0){
		if(optind < argc){
			long id = atol(argv[optind]);
			if(db_delete(d, id)) db_save(d);
			else fprintf(stderr, "ais: no record id %ld\n", id);
		} else {
			fprintf(stderr, "ais: del needs an id\n");
		}
	}
	else if(strcmp(verb, "dump") == 0){
		db_dump(d, stdout);
	}
	else {
		fprintf(stderr, "ais: unknown command '%s'\n", verb);
		help();
		db_close(d);
		return -1;
	}

	db_close(d);
	return 0;
}

static void help(void){
	fprintf(stdout,
"\nAIS - Associative Indexing Service (re-engineering, ANSI C / C99)\n\n"
"SYNOPSIS\n"
"    ais [-f FILE] [-o] [-d] <command> [args]\n\n"
"COMMANDS\n"
"    put <value> <key1> <key2> ...   store value (URI or text) under keys\n"
"    put -                           read 'value key1 key2 ...' from stdin\n"
"    add <id> <value>                attach another link (value) to record <id>\n"
"    get <key1> <key2> ...           AND of keys (default); -o = OR (union)\n"
"    del <id>                        delete record by id\n"
"    dump                            print all records (id|keys|value...)\n\n"
"OPTIONS\n"
"    -f FILE   index file (default: ais.db)\n"
"    -o        OR / union retrieval for 'get' (default is AND / intersection)\n"
"    -d        debug\n"
"    -h        this help\n\n");
}

#endif /* UNIT_TEST */
