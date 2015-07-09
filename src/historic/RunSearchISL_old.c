
void RunSearchISL_old()
{
  FILE *fp_sig = fopen(Config("SIGNATURE-PATH"), "rb");
  FILE *fp_src = fp_sig;
  
  if (Config("SOURCE-SIGNATURE-PATH")) {
    fp_src = fopen(Config("SOURCE-SIGNATURE-PATH"), "rb");
  } else {
    fp_src = fopen(Config("SIGNATURE-PATH"), "rb");
  }
  
  int topk = atoi(Config("SEARCH-DOC-TOPK")) + 1;
  
  readSigHeader(fp_sig);
  int fpsig_sig_width = cfg.sig_width;
  if (fp_sig != fp_src) 
    readSigHeader(fp_src);
  if (fpsig_sig_width != cfg.sig_width) {
    fprintf(stderr, "Error: source and signature sigfiles differ in width.");
    exit(1);
  }
  
  int records;
  islSlice *slices = readISL(Config("ISL-PATH"), &records);
  fprintf(stderr, "Total records: %d\n", records);
  
  int compare_doc = -1;
  if (Config("SEARCH-DOC")) {
    compare_doc = atoi(Config("SEARCH-DOC"));
    fseek(fp_src, cfg.headersize + cfg.sig_record_size * compare_doc, SEEK_SET);
  }
  int *scores = malloc(sizeof(int) * records);
  int *scoresd = malloc(sizeof(int) * records);
  int *bitmask = malloc(sizeof(int) * 65536);
  
  for (int i = 0; i < 65536; i++) {
    bitmask[i] = i;
  }
  qsort(bitmask, 65536, sizeof(int), bitcount_compar);
  
  while (!feof(fp_src)) {
    unsigned char sigcache[cfg.sig_record_size];
    fread(sigcache, cfg.sig_record_size, 1, fp_src);
    
    result results[topk];
    for (int i = 0; i < topk; i++) {
      results[i].score = -1;
      results[i].docid = -1;
    }
    
    //printf("Searching for documents similar to %s\n", sigcache);
    
    memset(scores, 0, sizeof(scores[0]) * records);
    memset(scoresd, 0, sizeof(scoresd[0]) * records);
    
    int dirty_n;
    int dirty[500000];
 
    int topk_lowest = 0;  
    int topk_2ndlowest = 0;
    int max_dist = atoi(Config("ISL-MAX-DIST"));
    for (int m = 0; m < 65536; m++) {
      int dist = count_bits(bitmask[m]);
      if (dist > max_dist) {
        //fprintf(stderr, "Early exit, exceeded max dist of %d\n", max_dist);
        break;
      }
      //printf("\nM: %5d. Dist: %2d\n------------------------\n", m, dist);
      if (results[topk_lowest].score + (16-dist)*cfg.sig_slices <= results[topk_2ndlowest].score) {
        fprintf(stderr, "Early exit @ dist %d\n", dist);
        break;
      }
      
      dirty_n = 0;
      int sum = 0;
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        int val = mem_read16(sigcache + cfg.sig_offset + 2 * slice) ^ bitmask[m];
        sum+=slices[slice].lookup[val].count;
        //printf("Slice %d: %d  (total: %d)\n", slice+1, slices[slice].lookup[val].count, sum);
        for (int n = 0; n < slices[slice].lookup[val].count; n++) {
          int d = slices[slice].lookup[val].list[n];
          scores[d] += 16-dist;
          if (scoresd[d] == 0) scoresd[d] = dist + 1;
          dirty[dirty_n++] = d;
        }
      }
      
      qsort(dirty, dirty_n, sizeof(int), int_compar);
      
      int last_d = -1;
      for (int i = 0; i < dirty_n; i++) {
        int d = dirty[i];
        if (d == last_d) continue;
        
        if (scores[d] > results[topk_lowest].score) {
          int dup = -1;

          for (int j = 0; j < topk; j++) {
            if (d == results[j].docid) {
              dup = j;
              break;
            }
          }

          if (dup != -1) {
            results[dup].score = scores[d];
          } else {
            results[topk_lowest].score = scores[d];
            results[topk_lowest].docid = d;
          }
            
          for (int j = 0; j < topk; j++) {
            if (results[j].score < results[topk_lowest].score) {
              topk_lowest = j;
            }
          }
          if (topk_2ndlowest == topk_lowest) {
            topk_2ndlowest = topk_lowest == 0 ? 1 : 0;
          }
          for (int j = 0; j < topk; j++) {
            if (results[j].score < results[topk_2ndlowest].score) {
              if (j == topk_lowest) continue;
              topk_2ndlowest = j;
            }
          }

        }
      }
      
    }
    
    int sig_bytes = cfg.sig_width / 8;
    unsigned char curmask[sig_bytes];
    memset(curmask, 0xFF, sig_bytes);
    
    rescoreResults(fp_sig, results, topk, sigcache + cfg.sig_offset, curmask);
    
    qsort(results, topk, sizeof(result), result_compar);
    
    for (int i = 0; i < topk-1; i++) {
      char docname[cfg.maxnamelen + 1];
      fseek(fp_sig, cfg.headersize + cfg.sig_record_size * results[i].docid, SEEK_SET);
      fread(docname, 1, cfg.maxnamelen + 1, fp_sig);

      //printf("%02d. (%05d) %s  Dist: %d (first seen at %d)\n", i+1, results[i].docid, docname, results[i].dist, scoresd[results[i].docid] - 1);
      //printf("%s Q0 %s 1 1 topsig\n", sigcache, docname);
      //printf("%s DIST %03d XIST Q0 %s 1 1 topsig\n", sigcache, results[i].dist, docname);
      printf("%s\n", docname);
    }
    
    if (compare_doc != -1) { // Only comparing against 1 document
      break;
    }
  }

  /*
  int maxscore = -1;
  scores[compare_doc] = 0;
  for (int i = 0; i < records; i++) {
    if (scores[i] > maxscore) maxscore = scores[i];
  }
  for (int i = 0; i < records; i++) {
    if (scores[i] == maxscore) {
      char docname[cfg.maxnamelen + 1];
      printf("Document %d is at max score %d\n", i, maxscore);
      fseek(fp, cfg.headersize + cfg.sig_record_size * i, SEEK_SET);
      fread(docname, 1, cfg.maxnamelen + 1, fp);
      printf("Document %d id: %s\n\n", i, docname);
      
    }
  }
  */
  
  
  // Histogram display
  /*
  int hist[17];
  for (int i = 0; i < records; i++) {
    hist[scoresd[i]-1]++;
  }
  for (int i = 0; i < 17; i++) {
    printf("%2d: %d\n", i, hist[i]);
  }
  */
  
  fclose(fp_sig);
  fclose(fp_src);
}
