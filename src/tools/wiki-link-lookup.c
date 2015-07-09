#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "Usage: ./wiki-link-lookup [TREC results file]\n");
    exit(1);
  }
  FILE *fi_in = fopen(argv[1], "r");
  
  printf("<html><body><pre>\n");
  for (;;) {
    char orig_doc[128];
    char dummy[16];
    char comp_doc[128];
    char rest_of_line[1024];
    int doc_dist;
    if (fscanf(fi_in, "%s%s%s%[^\n]\n", orig_doc, dummy, comp_doc, rest_of_line) < 4) break;
    
    int orig_doc_id = atoi(orig_doc);
    int comp_doc_id = atoi(comp_doc);
    
    if ((orig_doc_id != 0) && (comp_doc_id != 0)) {
      printf("<p><a href=\"http://en.wikipedia.org/wiki/?curid=%d\">%d</a> %s <a href=\"http://en.wikipedia.org/wiki/?curid=%d\">%d</a>%s</p>", doc_dist, orig_doc_id, dummy, orig_doc_id, comp_doc_id, rest_of_line);
      fflush(stdout);
    }
  }
  printf("</pre></body></html>\n");
  
  fclose(fi_in);
  return 0;
}
