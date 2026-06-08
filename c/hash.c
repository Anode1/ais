/**
 * Copyright (C) 2001 Vasili Gavrilov <vgavrilov AAAATTTTT users.sourceforge.net>
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

#include <string.h>
#include <stdlib.h>
#include "hash.h"

typedef struct hash_bucket{
    char* key;
    void* data;
    struct hash_bucket* next;
} hash_bucket;

struct hash{
    long size;                   /* table size */
    long n;                      /* current number of entries */
    long used;                   /* number of used elements */
    hash_bucket** table;
    char name[256];              /* hashname */
};


/* berkeley DB algorithm (V.G.: I've got 0 collisions populating English words (dict)
and 70 colisions - with Bernstein algorithm, so I'm using the 1st one) */
static unsigned long calc_hash(char* str){

    unsigned long hash = 0;
    int c;

    while ((c = *str++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}


struct hash* hash_create(long size){

	struct hash* table;
    hash_bucket** bucket;
    long i;

    table = (struct hash*)malloc(sizeof(struct hash));
    if(table == NULL){
    	OUT_OF_MEM();
    }

    if (size <= 0){
        free(table);
        return NULL;
    }

    table->size = size;
    table->table = (hash_bucket**)malloc(sizeof(hash_bucket*) * size);
    if(table->table == NULL)
    	OUT_OF_MEM();

    bucket = table->table;

    if (bucket == NULL) {
        free(table);
        return NULL;
    }

    for (i = 0; i < size; i++)
        bucket[i] = NULL;

    table->n = 0;
    table->used = 0;

    return table;
}

void hash_delete(struct hash* table){

    long i;

    if (table == NULL)
        return;

    for (i = 0; i < table->size; i++) {
        hash_bucket* bucket;

        for (bucket = (table->table)[i]; bucket != NULL;) {
            hash_bucket* prev = bucket;

            free(bucket->key);
            bucket = bucket->next;
            free(prev);
        }
    }

    free(table->table);
    free(table);
}

void* hash_put(struct hash* table, char* key, void* data){

    unsigned long val = calc_hash(key) % table->size;
    hash_bucket* bucket;

    if ((table->table)[val] == NULL) {
        bucket = (hash_bucket*)malloc(sizeof(hash_bucket));
        if (bucket == NULL)
        	OUT_OF_MEM();

        bucket->key = strdup(key);
        bucket->next = NULL;
        bucket->data = data;

        (table->table)[val] = bucket;
        table->n++;
        table->used++;

        return bucket->data;
    }

    for (bucket = (table->table)[val]; bucket != NULL; bucket = bucket->next)
        if (strcmp(key, bucket->key) == 0) {
            void* old_data = bucket->data;

            bucket->data = data;

            return old_data;
        }

    bucket = (hash_bucket*) malloc(sizeof(hash_bucket));
    if (bucket == NULL)
    	OUT_OF_MEM();

    bucket->key = strdup(key);
    bucket->data = data;
    bucket->next = (table->table)[val];

    (table->table)[val] = bucket;
    table->n++;

    return data;
}

void* hash_get(struct hash* table, char* key){

    unsigned long val = calc_hash(key) % table->size;
    hash_bucket* bucket;

	/* printf("getting attempt %s<\n", key); */

    if ((table->table)[val] == NULL)
        return NULL;

    for (bucket = (table->table)[val]; bucket != NULL; bucket = bucket->next)
        if (strcmp(key, bucket->key) == 0)
            return bucket->data;

    return NULL;
}

void* hash_delete_entry(struct hash* table, char* key){

    unsigned long val = calc_hash(key) % table->size;
    hash_bucket* prev;
    hash_bucket* bucket;
    void* data;

    if ((table->table)[val] == NULL)
        return NULL;

    for (prev = NULL, bucket = (table->table)[val]; bucket != NULL; prev = bucket, bucket = bucket->next) {
        if (strcmp(key, bucket->key) == 0) {
            data = bucket->data;
            if (prev != NULL)
                prev->next = bucket->next;
            else {
                (table->table)[val] = bucket->next;
                table->used--;
            }
            free(bucket->key);
            free(bucket);
            table->n--;

            return data;
        }
    }

    return NULL;
}


void hash_call(struct hash* table, void (*func) (void*)){

    long i;
    hash_bucket* bucket;

    for (i = 0; i < table->size; i++)
    	if((table->table)[i])
    		for (bucket = (table->table)[i]; bucket != NULL; bucket = bucket->next)
    			func(bucket->data);
}


/* Print all keys in stored order - useful for debugging */
void hash_print_all_keys(struct hash* table){

    long i;
    hash_bucket* bucket;

    for (i = 0; i < table->size; i++){
    	if((table->table)[i]){
    		for (bucket = (table->table)[i]; bucket != NULL; bucket = bucket->next){
    			printf("%s\n", (char*)bucket->key);
    		}
    	}
    }
}


#ifdef HASH_UNIT_TEST

#include <stdio.h>
#include <limits.h>

#define BUFSIZE 1024

static void print_string(void* data){
	printf("%s ", (char*)data);
}

/* Print number of collisions in buckets */
static void hash_print_backets(struct hash* table){

    long i;
    hash_bucket* bucket;
    long collisions = 0;

    for (i = 0; i < table->size; i++){
    	if((table->table)[i]){
    		long count = 0;
    		for (bucket = (table->table)[i]; bucket != NULL; bucket = bucket->next){
    			if(count>0){
    				collisions++;
    				/*printf(" %d: \n", count);*/
    			}
    			count++;
    			/* printf("%s ", (char*)bucket->key); */
    		}
    		printf(" %ld: \n", count);
    	}
    	else{
    		printf("<null>\n");
    	}
    }
    printf("# collisions = %ld\n", collisions);
}


main(){
        char key[1024];
        FILE *fp;
        char *filename = "dict";
        long collision = 0;
    	struct hash* h;
        long i;
        /* int size = 65536; */
        long size = (2<<12)-1; /* 8191 */

        h = hash_create(size);

        printf("%d", sizeof(h));

        if(freopen(filename, "r", stdin) != stdin){
        	fprintf(stderr,"Can't open %s\n", filename);
            return -1;
        }

        while(fgets(key,sizeof(key),stdin)){

        	key[strlen(key)-1] = '\0';
        	hash_put(h, key, "");
        }

        hash_print_backets(h);


        printf("  stats:\n");
        printf("    %d entries, %d table elements, %d filled elements\n", h->n, h->size, h->used);
        printf("    %f entries per hash value in use\n", (double) h->n / h->used);

        hash_call(h, free);
        hash_delete(h);

        return 0;
}


#endif


