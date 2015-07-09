#ifndef TOPSIG_FILE_HPP
#define TOPSIG_FILE_HPP

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include "topsig-stat.hpp"

class File {
  public:
    File(const char *path)
    {
      auto file_info = Stat(path);
      file_size = file_info.GetFileSize();
      fp = std::fopen(path, "rb");
      if (!fp) {
        throw std::runtime_error("Failed to open file");
      }
    }
    ~File()
    {
      if (fp) {
        std::fclose(fp);
      }
    }
    File(const File &) = delete;
    File& operator=(const File &) = delete;
    inline int read8()
    {
      return std::fgetc(fp);
    }
    inline int read16()
    {
      unsigned int r = fgetc(fp);
      r |= std::fgetc(fp) << 8;
      return r;
    }
    inline int read32()
    {
      unsigned int r = std::fgetc(fp);
      r |= std::fgetc(fp) << 8;
      r |= std::fgetc(fp) << 16;
      r |= std::fgetc(fp) << 24;
      return r;
    }
    inline size_t read(void *ptr, size_t size, size_t count)
    {
      return std::fread(ptr, size, count, fp);
    }
    inline std::string readString(int w)
    {
      std::vector<char> buf;
      buf.reserve(w + 1);
      buf[w] = '\0';
      read(buf.data(), 1, w);
      return std::string(buf.data());
    }
    inline size_t size()
    {
      return file_size;
    }
  private:
    std::FILE *fp;
    size_t file_size;
};

#endif
