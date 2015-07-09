#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "uthash.h"

#define BUFFER_SIZE (512 * 1024)

typedef struct {
  char tag[256];
  int uses;
  UT_hash_handle hh;
} category;

category *cats = NULL;

static void add_category(char *docid, char *tagbuf)
{
  //printf("%s - %s\n", docid, tagbuf);
  
  category *C;
  HASH_FIND_STR(cats, tagbuf, C);
  if (!C) {
    C = malloc(sizeof(category));
    strcpy(C->tag, tagbuf);
    C->uses = 0;
    HASH_ADD_STR(cats, tag, C);
  }
  C->uses++;
}

static void write_category(char *docid, char *tagbuf)
{
  //printf("%s - %s\n", docid, tagbuf);
  
  category *C;
  HASH_FIND_STR(cats, tagbuf, C);
  if (C) {
    if (C->uses >= 1000) {
      printf("%s %s\n", docid, tagbuf);
    }
  }
}

static void processfile(char *docid, char *tags, void (*processcat)(char *, char *))
{
  char tagbuf[256];
  char *p = tags;
  char *ob = NULL;
  char *cb = NULL;
  while (*p != '\0') {
    if (isalpha(*p)) {
      if (ob == NULL) {
        ob = p;
      }
    }
    cb = p;
    if ((*p == ',') || (*p == 0x0A)) {
      if (ob) {
        if (p - ob < 255) {
          memcpy(tagbuf, ob, p - ob);
          tagbuf[p - ob] = '\0';
          processcat(docid, tagbuf);
        }
        ob = NULL;
      }
    }

    p++;
  }
  p = (cb+1);
  if (ob) {
    if (p - ob < 255) {
      memcpy(tagbuf, ob, p - ob);
      tagbuf[p - ob] = '\0';
      processcat(docid, tagbuf);
    }
    ob = NULL;
  }
}

static void wsjread(FILE *fp, void (*process_fn)(char *, char *))
{
  char *doc_start;
  char *doc_end;

  char buf[BUFFER_SIZE];
  
  int buflen = fread(buf, 1, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';
  
  for (;;) {
    if ((doc_start = strstr(buf, "<DOC>")) != NULL) {
      if ((doc_end = strstr(buf, "</DOC>")) != NULL) {
        doc_end += 7;
        doclen = doc_end-buf;
        //printf("Document found, %d bytes large\n", doclen);
        
        char *docid_start = strstr(buf, "<DOCNO>");
        char *docid_end = strstr(docid_start+1, "</DOCNO>");
        
        docid_start += 1;
        docid_end -= 1;
        
        docid_start += 7;
        
        int docid_len = docid_end - docid_start;
        char *docid = malloc(docid_len + 1);
        memcpy(docid, docid_start, docid_len);
        docid[docid_len] = '\0';
        
        char *title_start = strstr(buf, "<IN>");
        char *title_end = strstr(title_start+1, "</IN>");
        
        title_start += 1;
        title_end -= 0;
        
        title_start += 4;
        
        int title_len = title_end - title_start;
        char *filename = malloc(title_len + 1);
        memcpy(filename, title_start, title_len);
        filename[title_len] = '\0';
        
        processfile(docid, filename, process_fn);
                
        memmove(buf, doc_end, buflen-doclen);
        buflen -= doclen;
        
        buflen += fread(buf+buflen, 1, BUFFER_SIZE-1-buflen, fp);
        buf[buflen] = '\0';
      }
    } else {
      break;
    }
  }
}

int cat_compar(void *A, void *B)
{
  category *a = A;
  category *b = B;
  return b->uses - a->uses;
}

void print_top()
{
  HASH_SORT(cats, cat_compar);
  
  category *curr, *tmp;

  int n = 0;
  HASH_ITER(hh, cats, curr, tmp) {
    printf("%s %d\n", curr->tag, curr->uses);
    n++;
    if (n >= 100) break;
  }
}


int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "Usage: ./wsj-title-lookup [wsj.xml]\n");
    exit(1);
  }

  FILE *fi_xml = fopen(argv[1], "rb");
  
  fprintf(stderr, "Reading %s", argv[1]);
  wsjread(fi_xml, add_category);
  rewind(fi_xml);
  //print_top();
  wsjread(fi_xml, write_category);
  fprintf(stderr, "...done\n");
  fclose(fi_xml);
  return 0;
}
