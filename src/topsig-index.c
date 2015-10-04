#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "topsig-index.h"
#include "topsig-file.h"
#include "topsig-filerw.h"
#include "topsig-config.h"
#include "topsig-process.h"
#include "topsig-thread.h"
#include "topsig-signature.h"
#include "topsig-stats.h"
#include "topsig-global.h"
#include "topsig-document.h"
#include "topsig-progress.h"
#include "uthash.h"

#define BUFFER_SIZE (512 * 1024)
#define MAX_WARC_HEADER_LINE 65536
#define MAX_FIELDNAME 256

static char currentArchivePath[2048];

typedef struct {
  char from[256];
  char to[256];
  UT_hash_handle hh;
} docid_mapping;

typedef struct {
  char WARC_Type[256];
  char WARC_TREC_ID[256]; // TREC ID is only populated if WARC_Type is response
  int Content_Length;
} WarcHeader;

docid_mapping *docid_mapping_list = NULL;
docid_mapping *docid_mapping_hash = NULL;

static char *DocumentID(char *path, char *data)
{
  char *docId = NULL;
  const char *docIdFormat = GetOptionalConfig("DOCID-FORMAT", "path");
  
  if (strcmp_lc(docIdFormat, "path")==0) {
    docId = malloc(strlen(path)+1);
    strcpy(docId, path);
  } else if (strcmp_lc(docIdFormat, "basename.ext")==0) {
    char *p = strrchr(path, '/');
    if (p == NULL)
      p = path;
    else
      p = p + 1;
    docId = malloc(strlen(p)+1);
    strcpy(docId, p);
  } else if (strcmp_lc(docIdFormat, "basename")==0) {
    char *p = strrchr(path, '/');
    if (p == NULL)
      p = path;
    else
      p = p + 1;
    docId = malloc(strlen(p)+1);
    strcpy(docId, p);
    p = strrchr(docId, '.');
    if (p) *p = '\0';
  } else if (strcmp_lc(docIdFormat, "xmlfield")==0) {
    char *docid_field = Config("XML-DOCID-FIELD");
    if (!docid_field) {
      fprintf(stderr, "DOCID-FORMAT=xmlfield but XML-DOCID-FIELD unspecified\n");
      exit(1);
    }

    char xml_open[256], xml_close[256];
    sprintf(xml_open, "<%s>", docid_field);
    sprintf(xml_close, "</%s>", docid_field);

    char *start = strstr(data, xml_open);
    char *end = strstr(data, xml_close);
    if (!start || !end) {
      // XML field not found. Use path
      docId = malloc(strlen(path)+1);
      strcpy(docId, path);
    } else {
      start += strlen(xml_open);
      docId = malloc(end - start + 1);
      memcpy(docId, start, end - start);
      docId[end - start] = '\0';
    }
  } else {
    fprintf(stderr, "DOCID-FORMAT invalid.\n");
    exit(1);
  }

  if (docid_mapping_hash) {
    docid_mapping *lookup;
    HASH_FIND_STR(docid_mapping_hash, docId, lookup);
    if (lookup) {
      docId = realloc(docId, strlen(lookup->to)+1);
      strcpy(docId, lookup->to);
    }
  }

  return docId;
}

// Get the next file pointed to
static char *getnextfile(char *path)
{
  // In the configuration, both files and directories can be specified.
  static int cfg_pos = 0;
  static DIR *curr_dir = NULL;
  static char *curr_dir_path = NULL;

  // Loop until we return something
  for (;;) {

    if (curr_dir) {
      struct dirent *dir_ent = readdir(curr_dir);
      if (dir_ent) {
        if (strcmp(dir_ent->d_name, ".")==0) continue;
        if (strcmp(dir_ent->d_name, "..")==0) continue;
        sprintf(path, "%s%s%s", curr_dir_path, GetDirSeparator(), dir_ent->d_name);
        return path;
      }
      closedir(curr_dir);
      curr_dir = NULL;
    }
    cfg_pos++;

    char cfg_opt[128];
    if (cfg_pos == 1) {
      sprintf(cfg_opt, "TARGET-PATH");
    } else {
      sprintf(cfg_opt, "TARGET-PATH-%d", cfg_pos);
    }
    char *fpath = Config(cfg_opt);

    if (fpath == NULL) return NULL;
    if (fpath[0] != '\0') {
      if (IsDirectory(fpath)) { // Directory
        curr_dir_path = fpath;
        curr_dir = opendir(fpath);
      } else {
        strcpy(path, fpath);
        return path;
      }
    }
  }

  return NULL;
}

static void indexfile(Document *doc)
{
  static int thread_mode = -1;
  static SignatureCache *signaturecache = NULL;
  if (thread_mode == -1) {
    if (GetIntegerConfig("THREADS", 1) > 1) {
      thread_mode = 1;
    } else {
      thread_mode = 0;
    }
  }

  if (thread_mode == 0) { // Single-threaded
    if (signaturecache == NULL) {
      signaturecache = NewSignatureCache(1, 1);
    }
    ProcessFile(signaturecache, doc);
  } else {
    ProcessFile_Threaded(doc);
  }
}

static void AR_file(FileHandle *fp, void (*processFile)(Document *))
{
  int filesize = 0;
  int buffersize = 1024;
  Document *doc = NewDocument(NULL, NULL);

  char *filedat = NULL;

  for (;;) {
    filedat = realloc(filedat, buffersize);
    int rbuf = ReadFile(filedat+filesize, buffersize-filesize, fp);
    filesize += rbuf;
    if (rbuf == 0) break;

    buffersize *= 2;
  }

  filedat[filesize] = '\0';
  doc->data = filedat;
  doc->dataLength = filesize;
  doc->docId = DocumentID(currentArchivePath, filedat);

  processFile(doc);
}

static int warcReadHeader_line(FileHandle *fp, char *buf, const size_t buflen) {
  size_t bufpos = 0; // where to insert next char
  for (;;) {
    char c = -1;
    int bytes_read = ReadFile(&c, 1, fp);
    if (bytes_read == 0) {
      return 1; // reached EOF
    }
    if (c == '\n') {
      break;
    } else if (c == '\0' || c == '\r') {
      c = ' ';
    }
    buf[bufpos++] = c;
    if (bufpos == buflen - 1) { // last position is for string null terminator
      break;
    }
  }
  buf[bufpos] = '\0';
  trim(buf);
  return 0; // not EOF
}

static int warcReadHeader(FileHandle *fp, WarcHeader *header) {
  // initialize
  strcpy(header->WARC_Type, "");
  strcpy(header->WARC_TREC_ID, "");
  header->Content_Length = -1;

  for (;;) {
    // read a line
    char buf[MAX_WARC_HEADER_LINE] = "";
    if (warcReadHeader_line(fp, buf, MAX_WARC_HEADER_LINE)) {
      // reached EOF
      return 1;
    }

    // warc header ends with a blank line
    if (strcmp(buf, "") == 0) {
      break;
    }

    // parse fieldname
    char fieldname[MAX_FIELDNAME] = "";
    char *end_fieldname = strchr(buf, ':');
    if (end_fieldname == 0) {
      continue; // not a field
    }
    char *src = buf, *dst = fieldname, *dstend = fieldname + MAX_FIELDNAME;
    while (src != end_fieldname && dst != dstend) {
      *dst++ = *src++;
    }
    *dst = '\0';

    // parse field
    char *field = end_fieldname + 1; // skip the : separator
    if (strcmp_lc(fieldname, "WARC-Type") == 0) {
      sscanf(field, "%s", header->WARC_Type);
    } else if (strcmp_lc(fieldname, "Content-Length") == 0) {
      sscanf(field, "%d", &header->Content_Length);
    } else if (strcmp_lc(fieldname, "WARC-TREC-ID") == 0) {
      sscanf(field, "%s", header->WARC_TREC_ID);
    }
  }

  // check everything was read
  if (strcmp(header->WARC_Type, "") == 0) {
    fprintf(stderr, "WARC format error - can not read WARC-Type:\n");
    exit(1);
  }
  if (strcmp_lc(header->WARC_Type, "response") == 0 && strcmp(header->WARC_TREC_ID, "") == 0) {
    fprintf(stderr, "WARC format error - can not read WARC-TREC-ID:\n");
    exit(1);
  }
  if (header->Content_Length < 0) {
    fprintf(stderr, "WARC format error - can not read Content-Length:\n");
    exit(1);
  }

  // not EOF
  return 0;
}

static void warcReadContent(FileHandle *fp, char *data, const int Content_Length) {
  // fill data byte reading Content_Length bytes
  int bytes_read = 0;
  while (bytes_read < Content_Length) {
    int current_bytes_read = ReadFile(data + bytes_read, Content_Length - bytes_read, fp);
    if (current_bytes_read == 0) {
      fprintf(stderr, "WARC format error - EOF reached while reading content section\n");
      exit(1);
    }
    bytes_read += current_bytes_read;
  }

  // some content contains null characters
  for (int i = 0; i < Content_Length; i++) {
    if (data[i] == '\0') {
      data[i] = '_';
    }
  }
  data[Content_Length] = '\0';
}

static void AR_warc(FileHandle *fp, void (*processfile)(Document *)) {
  for (;;) {
    // read header
    WarcHeader header;
    if (warcReadHeader(fp, &header)) {
      // reached EOF
      return;
    }

    // read content
    Document *newDoc = NewDocument(header.WARC_TREC_ID, NULL);
    newDoc->data = malloc(header.Content_Length + 1);
    warcReadContent(fp, newDoc->data, header.Content_Length);

    // warc has two trailing empty lines
    char buf[MAX_WARC_HEADER_LINE] = "";
    for (int i = 0; i < 2; i++ ) {
      warcReadHeader_line(fp, buf, MAX_WARC_HEADER_LINE);
      if (strcmp(buf, "") != 0) {
        fprintf(stderr, "WARC format error - can not read 2 empty lines after content\n");
        exit(1);
      }
    }

    // process the document
    if (strcmp_lc(header.WARC_Type, "response") == 0) {
      processfile(newDoc);
    } else {
      FreeDocument(newDoc);
    }
  }
}

static void AR_tar(FileHandle *fp, void (*processfile)(Document *))
{
  for (;;) {
    char buffer[512];
    int rlen = ReadFile(buffer, 512, fp);
    if (rlen < 512) break;

    int file_size;
    sscanf(buffer+124, "%o", &file_size);

    char *filedat = malloc(file_size + 1);
    for (int file_offset = 0; file_offset < file_size; file_offset += 512) {
      char buffer[512];
      ReadFile(buffer, 512, fp);
      int blocklen = file_size - file_offset;
      if (blocklen > 512) blocklen = 512;

      memcpy(filedat + file_offset, buffer, blocklen);
    }
    filedat[file_size] = '\0';
    char *filename = DocumentID(buffer, filedat);
    Document *newDoc = NewDocument(NULL, NULL);
    newDoc->data = filedat;
    newDoc->dataLength = file_size;
    newDoc->docId = filename;

    if (strcmp(filename, "NULL")==0) {
      FreeDocument(newDoc);
    } else {
      processfile(newDoc);
    }
  }
}

static void AR_wsj(FileHandle *fp,  void (*processfile)(Document *))
{
  int archiveSize;
  char *doc_start;
  char *doc_end;

  char buf[BUFFER_SIZE];

  int buflen = ReadFile(buf, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';

  for (;;) {
    if ((doc_start = strstr(buf, "<DOC>")) != NULL) {
      if ((doc_end = strstr(buf, "</DOC>")) != NULL) {
        doc_end += 7;
        doclen = doc_end-buf;

        char *title_start = strstr(buf, "<DOCNO>");
        char *title_end = strstr(buf, "</DOCNO>");

        title_start += 1;
        title_end -= 1;

        title_start += 7;

        int title_len = title_end - title_start;
        char *filename = malloc(title_len + 1);
        memcpy(filename, title_start, title_len);
        filename[title_len] = '\0';

        archiveSize = doc_end-doc_start;

        char *filedat = malloc(archiveSize + 1);
        memcpy(filedat, doc_start, archiveSize);
        filedat[archiveSize] = '\0';

        Document *newDoc = NewDocument(NULL, NULL);
        newDoc->docId = filename;
        newDoc->data = filedat;
        newDoc->dataLength = archiveSize;

        processfile(newDoc);

        memmove(buf, doc_end, buflen-doclen);
        buflen -= doclen;

        buflen += ReadFile(buf+buflen, BUFFER_SIZE-1-buflen, fp);
        buf[buflen] = '\0';
      }
    } else {
      break;
    }
  }
}

static void AR_newline(FileHandle *fp,  void (*processfile)(Document *))
{
  int archiveSize;
  char *doc_start;
  char *doc_end;

  char buf[BUFFER_SIZE];

  int buflen = ReadFile(buf, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';
  int file_index = 0;

  int file_index_startat = 1;
  int file_index_divby = 1;
  if (Config("TARGET-INDEX-NEWLINE-STARTAT"))
    file_index_startat = atoi(Config("TARGET-INDEX-NEWLINE-STARTAT"));
  if (Config("TARGET-INDEX-NEWLINE-DIVBY"))
    file_index_divby = atoi(Config("TARGET-INDEX-NEWLINE-DIVBY"));

  for (;;) {
    if ((doc_start = strstr(buf, "\n")) != NULL) {
      if ((doc_end = strstr(doc_start+1, "\n")) != NULL) {
        doclen = doc_end-buf;

        char *filename = malloc(8);
        sprintf(filename, "%04d", file_index / file_index_divby + file_index_startat);

        archiveSize = doc_end-doc_start;

        char *filedat = malloc(archiveSize + 1);
        memcpy(filedat, doc_start, archiveSize);
        filedat[archiveSize] = '\0';

        Document *newDoc = NewDocument(NULL, NULL);
        newDoc->docId = filename;
        newDoc->data = filedat;
        newDoc->dataLength = archiveSize;

        processfile(newDoc);

        memmove(buf, doc_end, buflen-doclen);
        buflen -= doclen;

        buflen += ReadFile(buf+buflen, BUFFER_SIZE-1-buflen, fp);
        buf[buflen] = '\0';

        file_index++;
      } else {
        break;
      }
    } else {
      break;
    }
  }
}


// Reader for the Khresmoi medical documents 2012 web crawl (and possibly other similar crawls)
static void AR_khresmoi(FileHandle *fp,  void (*processfile)(Document *))
{
  int archiveSize;
  char *filename_start;
  char *doc_start;
  char *doc_end;

  char buf[BUFFER_SIZE];

  int buflen = ReadFile(buf, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';
  int file_index = 0;

  for (;;) {
    if ((filename_start = strstr(buf, "#UID:")) != NULL) {
      if ((doc_start = strstr(filename_start+1, "#CONTENT:")) != NULL) {
        if ((doc_end = strstr(doc_start+1, "\n#EOR")) != NULL) {
          file_index++;
          doclen = doc_end-buf;

          doc_start += strlen("#CONTENT:");

          filename_start += strlen("#UID:");
          char *filename_end = strchr(filename_start, '\n');

          int filename_len = filename_end - filename_start;
          char *filename = malloc(filename_len + 1);
          memcpy(filename, filename_start, filename_len);
          filename[filename_len] = '\0';

          archiveSize = doc_end-doc_start;

          char *filedat = malloc(archiveSize + 1);
          memcpy(filedat, doc_start, archiveSize);
          filedat[archiveSize] = '\0';

          Document *newDoc = NewDocument(NULL, NULL);
          newDoc->docId = filename;
          newDoc->data = filedat;
          newDoc->dataLength = archiveSize;

          processfile(newDoc);

          memmove(buf, doc_end, buflen-doclen);
          buflen -= doclen;

          buflen += ReadFile(buf+buflen, BUFFER_SIZE-1-buflen, fp);
          buf[buflen] = '\0';
        } else {
          break;
        }
      } else {
        break;
      }
    } else {
      break;
    }
  }
}

static void AR_mediaeval(FileHandle *fp,  void (*processfile)(Document *))
{
  int archiveSize;
  char *doc_start;
  char *doc_end;

  char buf[BUFFER_SIZE];

  int buflen = ReadFile(buf, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';

  for (;;) {
    if ((doc_start = strstr(buf, "<photo")) != NULL) {
      if ((doc_end = strstr(doc_start+1, "</photo>")) != NULL) {
        doc_start += strlen("<photo");
        doc_end += strlen("</photo>");
        doclen = doc_end-buf;

        char *title_start = strstr(buf, "id=\"");
        title_start += strlen("id=\"");
        char *title_end = strstr(title_start+1, "\"");


        int title_len = title_end - title_start;
        char *filename = malloc(title_len + 1);
        memcpy(filename, title_start, title_len);
        filename[title_len] = '\0';

        archiveSize = doc_end-doc_start;

        char *filedat = malloc(archiveSize + 1);
        memcpy(filedat, doc_start, archiveSize);
        filedat[archiveSize] = '\0';

        Document *newDoc = NewDocument(NULL, NULL);
        newDoc->docId = filename;
        newDoc->data = filedat;
        newDoc->dataLength = archiveSize;

        processfile(newDoc);

        memmove(buf, doc_end, buflen-doclen);
        buflen -= doclen;

        buflen += ReadFile(buf+buflen, BUFFER_SIZE-1-buflen, fp);
        buf[buflen] = '\0';
      }
    } else {
      break;
    }
  }
}

static void (*getArchiveReader(const char *targetformat))(FileHandle *, void (*)(Document *))
{
  void (*archiveReader)(FileHandle *, void (*)(Document *)) = NULL;
  if (strcmp_lc(targetformat, "file")==0) archiveReader = AR_file;
  if (strcmp_lc(targetformat, "tar")==0) archiveReader = AR_tar;
  if (strcmp_lc(targetformat, "wsj")==0) archiveReader = AR_wsj;
  if (strcmp_lc(targetformat, "warc")==0) archiveReader = AR_warc;
  if (strcmp_lc(targetformat, "newline")==0) archiveReader = AR_newline;
  if (strcmp_lc(targetformat, "khresmoi")==0) archiveReader = AR_khresmoi;
  if (strcmp_lc(targetformat, "mediaeval")==0) archiveReader = AR_mediaeval;
  return archiveReader;
}

void RunIndex()
{
  // Initialise term statistics (if relevant)
  Stats_Initcfg();
  
  char path[2048];
  void (*archiveReader)(FileHandle *, void (*)(Document *));
  archiveReader = getArchiveReader(GetOptionalConfig("TARGET-FORMAT", "file"));

  if (archiveReader == NULL) {
    fprintf(stderr, "Error: Invalid TARGET-FORMAT\n");
    exit(1);
  }
  int indexedFiles = 0;
  while (getnextfile(path)) {
    indexedFiles++;
    FileHandle *fp = OpenFile(path);
    if (fp) {
      strcpy(currentArchivePath, path);
      archiveReader(fp, indexfile);
      CloseFile(fp);
    }
  }
  if (indexedFiles == 0) {
    fprintf(stderr, "Error: no input files specified for indexing. Provide an input file or directory with -target-path (path)\n");
    exit(1);
  } else {
    ProgressFinalise();
  }
  Flush_Threaded();
  
}

static void addstats(Document *doc)
{
  ProcessFile(NULL, doc);
}

void RunTermStats()
{
  char path[2048];
  void (*archiveReader)(FileHandle *, void (*)(Document *));
  archiveReader = getArchiveReader(GetOptionalConfig("TARGET-FORMAT", "file"));

  if (archiveReader == NULL) {
    fprintf(stderr, "Invalid/unspecified TARGET-FORMAT\n");
    exit(1);
  }

  while (getnextfile(path)) {
    FileHandle *fp = OpenFile(path);
    if (fp) {
      strcpy(currentArchivePath, path);
      archiveReader(fp, addstats);
      CloseFile(fp);
    }
  }
  WriteStats();
}

void InitIndexerConfig()
{
  char *C = Config("MEDTRACK-MAPPING-FILE");
  char *T = Config("MEDTRACK-MAPPING-TYPE");
  if (C) {
    FILE *fp = fopen(C, "r");
    int records = atoi(Config("MEDTRACK-MAPPING-RECORDS"));
    docid_mapping_list = malloc(sizeof(docid_mapping) * records);
    int recordnum = 0;
    for (int i = 0; i < records; i++) {
      char from[1024];
      char to[1024];
      char rectype[1024];
      fscanf(fp, "%s %s %s\n", from, rectype, to);

      int process_record = 1;
      if (T && strstr(T, rectype)==NULL) {
        process_record = 0;
      }

      docid_mapping *newrecord = docid_mapping_list+recordnum;
      strcpy(newrecord->from, from);
      if (process_record) {
        strcpy(newrecord->to, to);
      } else {
        strcpy(newrecord->to, "NULL");
      }
      HASH_ADD_STR(docid_mapping_hash, from, newrecord);
      recordnum++;

    }
    fclose(fp);
  }
}
