
void RunSearchISL()
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
  readSigHeader(fp_src);
  if (fpsig_sig_width != cfg.sig_width) {
    fprintf(stderr, "Error: source and signature sigfiles differ in width.\n");
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
  int *scoresd = malloc(sizeof(int) * records * cfg.sig_slices);
  int *bitmask = malloc(sizeof(int) * 65536);
  
  for (int i = 0; i < 65536; i++) {
    bitmask[i] = i;
  }
  qsort(bitmask, 65536, sizeof(int), bitcount_compar);
  
  while (!feof(fp_src)) {
    unsigned char sigcache[cfg.sig_record_size];
    fread(sigcache, cfg.sig_record_size, 1, fp_src);
        
    memset(scores, 0, sizeof(scores[0]) * records);
    memset(scoresd, 0, sizeof(scoresd[0]) * records);
 
    int topk_lowest = 0;  
    int topk_2ndlowest = 0;
    int max_dist = atoi(Config("ISL-MAX-DIST"));
    for (int m = 0; m < 65536; m++) {
      int dist = count_bits(bitmask[m]);
      if (dist > max_dist) {
        //fprintf(stderr, "Early exit, exceeded max dist of %d\n", max_dist);
        break;
      }
      
      int sum = 0;
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        int val = mem_read16(sigcache + cfg.sig_offset + 2 * slice) ^ bitmask[m];
        sum += slices[slice].lookup[val].count;
        //printf("Slice %d: %d  (total: %d)\n", slice+1, slices[slice].lookup[val].count, sum);
        for (int n = 0; n < slices[slice].lookup[val].count; n++) {
          int d = slices[slice].lookup[val].list[n];
          scores[d] += 16 - dist;
          
          scoresd[d * cfg.sig_slices + slice] = 1;
        }
      }
    }
    
    int saw_docs = 0;
    result *results = malloc(sizeof(result) * records);
    for (int i = 0; i < records; i++) {
      results[i].docid = i;
      results[i].score = scores[i];
      results[i].dist = cfg.sig_width - scores[i];
    }
    for (int i = 0; i < cfg.sig_slices * records; i++) {
          saw_docs += scoresd[i] ? 1 : 0;
    }
    //printf("saw %d/%d\n", saw_docs, records * cfg.sig_slices);
    
    qsort(results, records, sizeof(result), result_compar);
    
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
      printf("%s %s %d\n", sigcache, docname, results[i].dist);
    }
    
    if (compare_doc != -1) { // Only comparing against 1 document
      break;
    }
  }
  
  fclose(fp_sig);
  fclose(fp_src);
}
