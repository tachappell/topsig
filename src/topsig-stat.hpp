#ifndef TOPSIG_STAT_HPP
#define TOPSIG_STAT_HPP

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>

class Stat
{
  public:
    Stat(const char *path)
    {
      struct stat64 buf;
      stat64(path, &buf);
      file_size = static_cast<uint64_t>(buf.st_size);
    }
    std::uint64_t GetFileSize()
    {
      return file_size;
    }
  private:
    std::uint64_t file_size;
};

#endif