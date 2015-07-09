#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include "uthash.h"

typedef struct {
  int source_num;
  char docname[64];
  int begin;
  int length;
  int src_begin;
  int src_length;
  char col[4];
  //UT_hash_handle hh;
} plagsrc;

#define MAX_SOURCES 4096
plagsrc *sources[MAX_SOURCES];
int sources_n = 0;

char *fid = NULL;
char *fid_p = NULL;

char *read_field(const char *field)
{
  char search[1000];
  sprintf(search, "%s=\"", field);
  char *s = strstr(fid_p, search);
  if (!s) return NULL;
  s += strlen(search);
  char *e = strchr(s, '"');
  int slen = e - s;
  fid_p = e;
  char *ret = malloc(slen + 1);
  memcpy(ret, s, slen);
  ret[slen] = 0;
  return ret;
}

int readutf8char(FILE *fp)
{
  int c = fgetc(fp);
  
  if (c == EOF) return EOF;
  if (c >= 192) {
    int seqlen = 2;
    seqlen += c >= 224 ? 1 : 0;
    seqlen += c >= 240 ? 1 : 0;
    seqlen += c >= 248 ? 1 : 0;
    seqlen += c >= 252 ? 1 : 0;
    
    c = '?';
    for (int i = 1; i < seqlen; i++) {
      fgetc(fp);
    }
  }
  return c;
}

char *readFile_sub(const char *prefix, const char *suffix, int begin, int end)
{
  char fname[1024];
  sprintf(fname, "%s%s", prefix, suffix);
  FILE *fi = fopen(fname, "rb");
  fseek(fi, 0, SEEK_END);
  int fis = ftell(fi);
  char *fid = malloc(fis + 1);
  fseek(fi, 0, SEEK_SET);
  //fread(fid, 1, fis, fi);
  
  int fis2 = 0;
  int i = 0;
  
  for (;;) {
    int c = fgetc(fi);
    if (c == EOF) break;

    if (c == '\0') {
      fprintf(stderr, "NUL DETECTED IN %s\n", fname);
      exit(1);
    }
    
    if ((i >= begin) && (i < end)) {
      fid[fis2++] = c;
    }
    
    i++;
  }
  fid[fis2] = '\0';
  fclose(fi);
  return fid;
}


char *readFile(const char *prefix, const char *suffix)
{
  return readFile_sub(prefix, suffix, 0, INT_MAX);
}


int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "Usage: {xml file} {output dir}\n");
    exit(1);
  }
  
  fid = readFile("", argv[1]);

  fid_p = fid;
  
  char *reference_field = read_field("reference");
  
  char *refdoc = readFile("p:/pan10/", reference_field);
  int refdoc_len = strlen(refdoc);
  
  plagsrc **refdoc_src = malloc(refdoc_len * sizeof(plagsrc *));
  
  for (int i = 0; i < refdoc_len; i++) {
    refdoc_src[i] = NULL;
  }
  
  for (;;) {
   
    char *Fthis_offset = read_field("this_offset");
    if (Fthis_offset == NULL) break;
    char *Fthis_length = read_field("this_length");
    char *Fsource_reference = read_field("source_reference");
    char *Fsource_offset = read_field("source_offset");
    char *Fsource_length = read_field("source_length");
        
    plagsrc *P = malloc(sizeof(plagsrc));
    P->begin = atoi(Fthis_offset);
    P->length = atoi(Fthis_length);
    P->src_begin = atoi(Fsource_offset);
    P->src_length = atoi(Fsource_length);
    strcpy(P->docname, Fsource_reference);
    P->source_num = sources_n;
    
    const char *hex = "0123456789abcdef";
    P->col[0] = hex[((rand()) % 10)+5];
    P->col[1] = hex[((rand()) % 10)+5];
    P->col[2] = hex[((rand()) % 10)+5];
    P->col[3] = '\0';
        
    free(Fthis_offset);
    free(Fthis_length);
    free(Fsource_reference);
    free(Fsource_offset);
    free(Fsource_length);
        
    for (int i = P->begin; i < P->begin + P->length; i++) {
      refdoc_src[i] = P;
    }
        
    if (sources_n >= MAX_SOURCES) {
      fprintf(stderr, "TOO MANY SOURCES\n");
      exit(1);
    }
    sources[sources_n++] = P;
    
    char *fd_before = readFile_sub("p:/pan10/", P->docname, 0, P->src_begin);
    char *fd_mid = readFile_sub("p:/pan10/", P->docname, P->src_begin, P->src_begin + P->src_length);
    char *fd_after = readFile_sub("p:/pan10/", P->docname, P->src_begin + P->src_length, INT_MAX);
    char fdname[1024];
        
    sprintf(fdname, "%s/%d.html", argv[2], P->source_num);
    FILE *fp = fopen(fdname, "w");
    fprintf(fp, "<html>");
    fprintf(fp, "<head><title>%s</title></head>", P->docname);
    fprintf(fp, "<body>");
    fprintf(fp, "<pre>");
    //fprintf(stderr, "writing %d %d %d\n", strlen(fd), P->src_begin, P->src_length);
    fprintf(fp, "<span>%s</span>", fd_before);
    fprintf(fp, "<a id=\"entry\">");
    fprintf(fp, "<span style=\"background-color:#%s;\">%s</span>", P->col, fd_mid);     
    fprintf(fp, "<span>%s</span>", fd_after);
    fprintf(fp, "</pre>");
    fprintf(fp, "</body>");
    fprintf(fp, "</html>");
    fclose(fp);
    
    free(fd_before);
    free(fd_mid);
    free(fd_after);
  }
  
  
  char fdname[1024];
      
  sprintf(fdname, "%s/index.html", argv[2]);
  FILE *fp = fopen(fdname, "w");
  plagsrc *curplg = NULL;
  fprintf(fp, "<html>");
  fprintf(fp, "<body>");
  fprintf(fp, "<pre>");
  fprintf(fp, "<span>");
  for (int i = 0; i < refdoc_len; i++) {
    if (curplg != refdoc_src[i]) {
      if (curplg != NULL) fprintf(fp, "</a>");
      fprintf(fp, "</span>");
      
      curplg = refdoc_src[i];
      if (curplg) {
        fprintf(fp, "<span style=\"background-color:#%s;\">", curplg->col);
        fprintf(fp, "<a href=\"%d.html#entry\">", curplg->source_num);
      } else {
        fprintf(fp, "<span>");
      }
    }
    fputc(refdoc[i], fp);
  }
  if (curplg != NULL) fprintf(fp, "</a>");
  fprintf(fp, "</span>");
  fprintf(fp, "</pre>");
  fprintf(fp, "</body>");
  fprintf(fp, "</html>");
  fclose(fp);
    
  return 0;
}
