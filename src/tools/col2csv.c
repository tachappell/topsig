#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct list {
  char *t;
  struct list *n;
};

int main(int argc, char **argv)
{
  int i;
  struct list **lists = malloc(sizeof(struct list *) * (argc-1));
  for (i = 1; i < argc; i++) {
    FILE *fp = fopen(argv[i], "r");
    char buf[256];
    lists[i-1] = NULL;
    struct list *T = NULL;
    while (fscanf(fp, "%s", buf) == 1) {
      struct list *L = malloc(sizeof(struct list));
      L->t = malloc(strlen(buf) + 1);
      strcpy(L->t, buf);
      L->n = NULL;
      if (T) {
        T->n = L;
      } else {
        lists[i-1] = L;
      }
      T = L;
    }
    fclose(fp);
  }
  
  for (;;) {
    int eof = 0;
    for (i = 0; i < argc-1; i++) {
      if (lists[i] == NULL) {
        eof = 1;
        break;
      }
      printf("%s", lists[i]->t);
      lists[i] = lists[i]->n;
      if ((i+1) < (argc-1)) printf(",");
    }
    printf("\n");
    if (eof) break;
  }
  
  return 0;
}
