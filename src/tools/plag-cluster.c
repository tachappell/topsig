#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "uthash.h"

#define SIG_SIZE (256/8)
#define MAX_CLUSTER_SIZE (131072)
#define DOC_NAME_LEN 79

typedef struct {
  char docname[DOC_NAME_LEN+1];
  unsigned char sig[SIG_SIZE];
  UT_hash_handle hh;
} signature;

int main(int argc, char **argv)
{  
  fprintf(stderr, "Reading signatures....");
  FILE *fp_docids;
  FILE *fp_sigs;
  fp_docids = fopen("p:/pan10-ktree.docids", "r");
  fp_sigs = fopen("p:/pan10-ktree.sig", "rb");
  
  signature *signature_hash = NULL;
  for (;;) {
    char docname[DOC_NAME_LEN+1];
    if (fscanf(fp_docids, "%s\n", docname) < 1) break;
    signature *S = malloc(sizeof(signature));
    strcpy(S->docname, docname);
    fread(S->sig, 1, SIG_SIZE, fp_sigs);
    HASH_ADD_STR(signature_hash, docname, S);
  }
  fclose(fp_docids);
  fclose(fp_sigs);
  fprintf(stderr, "done.\n");

  int last_cluster = -1;
  int max_cluster_size = 32768;
  signature **current_cluster = malloc(sizeof(signature *) * max_cluster_size);
  int current_cluster_n = 0;
  FILE *fp = fopen(argv[1], "r");
  for (;;) {
/*
    char docname[256];
    char dummy_ch;
    int param[8];
    int cluster;
    
    if (fscanf(fp, "%[^_]", docname) == 0) break;
    for (int i = 0; i < 8; i++) {
      fscanf(fp, "%c%d", &dummy_ch, param+i);
    }
    fscanf(fp, "%d\n", &cluster);
    
    char docname[DOC_NAME_LEN+1];
    sprintf(docname, "%s_%d_%d_%d_%d_%d_%d_%d_%d", docname, param[0], param[1], param[2], param[3], param[4], param[5], param[6], param[7]);
    
    
    //printf("%s %d %d %d %d %d %d %d %d\n", docname, param[0], param[1], param[2], param[3], param[4], param[5], param[6], param[7]);
    
    doc *D = malloc(sizeof(doc));
    strcpy(D->docname, docname);
    D->cluster = cluster;
*/
    char docname[DOC_NAME_LEN+1];
    int cluster;
    if (fscanf(fp, "%s %d\n", docname, &cluster) < 2) break;
    if (cluster != last_cluster) {
      int pairs = 0;
      if (current_cluster_n) {
        for (int i = 0; i < current_cluster_n; i++) {
          for (int j = i + 1; j < current_cluster_n; j++) {
            signature *I = current_cluster[i];
            signature *J = current_cluster[j];
            //int i_source = strncmp(I->docname, "source", 6)==0 ? 1 : 0;
            //int j_source = strncmp(J->docname, "source", 6)==0 ? 1 : 0;
            int i_source = I->docname[1] == 'o' ? 1 : 0;
            int j_source = J->docname[1] == 'o' ? 1 : 0;
            //fprintf(stderr, "%s %s %d %d\n", I->docname, J->docname, i_source, j_source);
            
            if ((i_source != j_source)) {
              pairs++;
            }
          }
        }
      }
      printf("Pairs in cluster %d: %d\n", last_cluster, pairs);
      current_cluster_n = 0;
    }
    last_cluster = cluster;
    signature *S;
    HASH_FIND_STR(signature_hash, docname, S);
    if (S) {
      if (current_cluster_n == max_cluster_size) {
        max_cluster_size *= 2;
        current_cluster = realloc(current_cluster, sizeof(signature *) * max_cluster_size);
        fprintf(stderr, "max_cluster_size (%d) insufficient. Expanding to %d\n", max_cluster_size/2, max_cluster_size);
        //exit(1);
      }
      current_cluster[current_cluster_n++] = S;
    } else {
      fprintf(stderr, "Couldn't find %s\n", docname);
      exit(1);
    }
    
  }
  
  fclose(fp);
  
  fprintf(stderr, "Pairing done\n");
  
  
  return 0;
  /*
  

  for (int sus_id = 1; sus_id <= 15925; sus_id++) {
    int sus_dir = (sus_id - 1) / 500 + 1;
    char susp_filename[512];
    sprintf(susp_filename, "D:/sekai/plag/pan-plagiarism-corpus-2010/suspicious-documents/part%d/suspicious-document%05d.xml", sus_dir, sus_id);
    printf("%s\n", susp_filename);
    
    FILE *fi = fopen(susp_filename, "r");
    fseek(fi, 0, SEEK_END);
    int fis = ftell(fi);
    char *fid = malloc(fis + 1);
    fseek(fi, 0, SEEK_SET);
    fread(fid, 1, fis, fi);
    fid[fis] = '\0';
    fclose(fi);
    char *s = fid;
    for (;;) {
      const char *search = "source_reference=\"";
      s = strstr(s, search);
      if (!s) break;
      s += strlen(search);
      char *e = strchr(s, '.');
      int fname_len = e - s;
      char fname[256];
      memcpy(fname, s, fname_len);
      fname[fname_len] = '\0';
      //fprintf(stderr, "[%s]\n", fname);
      plagiarised *P;
      HASH_FIND_STR(plagiarised_documents, fname, P);
      if (!P) {
        P = malloc(sizeof(plagiarised));
        strcpy(P->fname, fname);
        HASH_ADD_STR(plagiarised_documents, fname, P);
      }
    }
  }
  */
  return 0;
}
