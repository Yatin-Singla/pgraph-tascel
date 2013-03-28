/**
 * @file elib.c
 *
 * @author andy.cj.wu@gmail.com
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2010 Washington State University. All rights reserved.
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elib.h"


static char *name = NULL;    /* program name */
static size_t space = 0;     /* keep track of allocated mem */

char *estrdup(const char *s){
    char *t = NULL;
    int need;

    need = strlen(s) + 1;
    t = malloc(need);
    if(t == NULL)
        eprintf("estrdup(\"%.20s\") failed:", s);

    space += need;

    strcpy(t, s);
    return t;
}


void error(const char *msg){
    fprintf(stderr, "LOG - ERROR: %s\n", msg);
}


void eprintf(const char *fmt, ...){

    va_list args;
    fflush(stdout);

    if(getProgName() != NULL)
        fprintf(stderr, "%s: ", getProgName());

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if(fmt[0] != '\0' && fmt[strlen(fmt)-1] == ':')
        fprintf(stderr, " %s", strerror(errno));
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}


void *emalloc(size_t n){
    void *p = NULL;
    
    p = malloc(n);
    if(p == NULL)
        eprintf("malloc of %u bytes failed:", n);

    space += n;
    return p;
}


void *ecalloc(size_t nmemb, size_t size){
    void *p = NULL;

    p = calloc(nmemb, size);

    if(p == NULL)
        eprintf("calloc of (%u*%u) bytes failed:", nmemb, size);

    space += (nmemb*size);
    return p;
}


void *efopen(const char *fileName, const char *mode){
    FILE *fp = NULL;

    fp = fopen(fileName, mode);
    if(fp == NULL)
        eprintf("open file \"%.20s\" in mode \"%.20s\" failed:", fileName, mode);

    return fp;
}


void setProgName(const char *str){
    name = estrdup(str);
}


const char *getProgName(void){
    return name;
}

void printSpace(void){
    printf("space=%d\n", (int)space);
}


/* TODO: write efree() function to keep tracked of mem space and also null the freed pointer */
void efree(void **p){
    free(*p);
    /* p assigned to be NULL ? */
    *p = NULL; 
}
