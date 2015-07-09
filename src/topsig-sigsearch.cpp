#include "topsig-sigsearch.hpp"

#include <string>
#include <iostream>
#include <vector>
#include <limits>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "topsig-file.hpp"

extern "C" {
#include "topsig-config.h"
}

/*

Signature search:

from single query-
- Convert query to signature, iterate over signature file, get top K / within top Hamming distance

from topic file-
- Convert topics to signatures, iterate over signature file * topic signatures, get top K / within top Hamming distance

from signature file-
- Iterate over collection signature file * search signature file, get top K / within top Hamming distance


Multithreading-
- Should allow the user to choose the level at which the problem is divided into subproblems for multithreading


At its core, signature search means taking one buffer of search signatures and another buffer of collection signatures, and searching the entire buffer of collection signatures using the entire buffer of search signatures. The output we desire from this operation is a buffer of Hamming distance scores - one int per collection signature per search signature. After this buffer is created, we also want to extract the desired output into a top-K list of list of documents with Hamming distances in a specified range. This operation is potentially less multithread-able, but not necessarily that much so - combining these lists should be extremely quick. Multithreading per search signature is also free, it is only decomposing beyond that which can cause trouble. It does raise questions about exactly how to decompose this problem, however



__builtin_popcountll

*/

static const std::uint64_t one = 1;

class DocumentSignature
{
  public:
  private:
};

class SignatureCollection
{
  public:
    SignatureCollection(const char *path)
    {
      // Open the signature file
      File file{path};
      
      // Get the size of the file
      auto file_size = file.size();
      
      // Read in the signature file headers
      header.header_size = file.read32();
      header.version = file.read32();
      header.doc_name_len = file.read32();
      header.sig_width = file.read32();
      header.sig_density = file.read32();
      if (header.version >= 2) {
        header.sig_seed = file.read32();
      } else {
        header.sig_seed = 0;
      }
      header.sig_method = file.readString(64);
      
      // Calculate the space required for the header and data of each signature
      
      // The header requires docnamelen + 1 + 32 bytes (8 int fields)
      auto sig_header_size = header.doc_name_len + 1 + 32;
      // Each signature requires sig_width bits
      auto sig_data_size = header.sig_width / 8;
      
      // Calculate the number of signatures in the sig file. This is the file size, minus the header size
      // and divided by the sum of the sig header size and the sig data size
      sig_count = (file_size - header.header_size) / (sig_header_size + sig_data_size);
      std::cerr << "sig_count: " << sig_count << std::endl;
      
      // We want to allocate an amount of space for each that consists of a multiple of 64 bits
      // This is so we can use the efficient 64-bit operations and the like without worrying about
      // alignment issues.
      
      buf.sig_header_buf_size = (sig_header_size + 7) / 8 * 8;
      buf.sig_data_buf_size = (sig_data_size + 7) / 8 * 8;
      
      // Reserve this space in two vectors. We will be then using those vectors as memory directly
      // This isn't fantastic but it's better than wasting time on resize()
      
      buf.headers.reserve(one * buf.sig_header_buf_size * sig_count);
      buf.sigdata.reserve(one * buf.sig_data_buf_size * sig_count);
            
      // Read data into these two buffers. We want to keep all the header data together and all of the
      // signature data together to speed up exhaustive searching by making better use of throughput and
      // cache lines
      
      /*
      // Old approach - read header and signature data into their appropriate positions in memory, one at a time
      {
        auto *header_pt = buf.headers.data();
        auto *sigdata_pt = buf.sigdata.data();
        for (auto i = 0; i < sig_count; i++) {
          file.read(header_pt, sig_header_size, 1);
          file.read(sigdata_pt, sig_data_size, 1);
          header_pt += buf.sig_header_buf_size;
          sigdata_pt += buf.sig_data_buf_size;
          if ((i+1) % 1000000 == 0) {
            std::cerr << (i+1) << std::endl;
          }
        }
      }
      */
      
      // New approach - read a block of data in, then copy the data for the individual signatures into their respective positions
      {
        // 4mb block
        auto block_size_bytes = 4 * 1024 * 1024;
        
        auto sigs_per_block = block_size_bytes / (sig_header_size + sig_data_size);
        block_size_bytes = (sig_header_size + sig_data_size) * sigs_per_block;
        
        std::vector<unsigned char> block;
        block.reserve(block_size_bytes);
        auto *block_data = block.data();
        
        
        auto *header_pt = buf.headers.data();
        auto *sigdata_pt = buf.sigdata.data();
        int sigs_processed_dbg = 0;
        
        do {
          //std::cerr << "Reading " << sigs_per_block << " signatures of size " << sig_header_size + sig_data_size << std::endl;
          auto sigs_read = file.read(block_data, sig_header_size + sig_data_size, sigs_per_block);
          //std::cerr << "Read " << sigs_read << std::endl;
          if (sigs_read == 0) break;
          auto *block_data_ptr = block_data;
          
          for (int i = 0; i < sigs_read; i++) {
            std::memcpy(header_pt, block_data_ptr, sig_header_size);
            block_data_ptr += sig_header_size;
            header_pt += buf.sig_header_buf_size;
            std::memcpy(sigdata_pt, block_data_ptr, sig_data_size);
            block_data_ptr += sig_data_size;
            sigdata_pt += buf.sig_data_buf_size;
            
            sigs_processed_dbg++;
            
            if (sigs_processed_dbg % 10000000 == 0) {
              std::cerr << sigs_processed_dbg << std::endl;
            }
          }

        } while (1);
      }
      
      /*
      // TMP HACK- just read in data into both buffers as quickly as possible to see how long it takes
      file.read(buf.headers.data(), sig_header_size, sig_count);
      file.read(buf.sigdata.data(), sig_data_size, sig_count);
      */
      // Done! Report progress
      std::cerr << "Completed reading " << sig_count << " signatures into memory.\n";
      std::cerr << "Read in " << one * buf.sig_header_buf_size * sig_count << " bytes of headers.\n";
      std::cerr << "Read in " << one * buf.sig_data_buf_size * sig_count << " bytes of signature data.\n";
    }
  
  int size() const {
    return sig_count;
  }
  
  int signatureWidth() const {
    return header.sig_width;
  }
  
  const void *getSigPtr(int i) const {
    return static_cast<const void *>(buf.sigdata.data() + one * buf.sig_data_buf_size * i);
  }
  
  int hammingDistance(int thisId, const SignatureCollection &col, int searchId) const {
    //std::cerr << "Calculate hamming distance between this->" << thisId << " and col->" << searchId << std::endl;
    // Type punning! Here be dragons
    int hd = 0;

    auto *search_ptr = static_cast<const std::uint64_t *>(getSigPtr(thisId));
    auto *collection_ptr = static_cast<const std::uint64_t *>(col.getSigPtr(searchId));
    
    auto sig_size = buf.sig_data_buf_size / 8;
    
    for (int i = 0; i < sig_size; i++) {
      //std::cerr << "   " << i << "/" << sig_size << std::endl;
      hd += __builtin_popcountll(search_ptr[i] ^ collection_ptr[i]);
    }
    
    return hd;
  }
    
  private:
    struct {
      size_t header_size;
      int version;
      int doc_name_len;
      int sig_width;
      int sig_density;
      int sig_seed;
      std::string sig_method;
    } header;
    
    int sig_count;
    
    std::vector<DocumentSignature> signatures;
    
    struct {
      std::vector<unsigned char> headers;
      std::vector<unsigned char> sigdata;
      int sig_header_buf_size;
      int sig_data_buf_size;
    } buf;
};

class SignatureSearcher
{
  public:
    SignatureSearcher(const SignatureCollection &s, const SignatureCollection &c) : searchset(s), collection(c) {
      
    }
    
    std::vector<std::vector<int>> Search(int searchSignatureId, int hammingThreshold = std::numeric_limits<int>::max()) {
      auto count = collection.size();
      //std::cerr << "Searching " << count << " signatures." << std::endl;
      
      
      //auto signatureScores = std::vector<int>(count, 0);
      auto sig_width = collection.signatureWidth();
      
      auto scoreLists = std::vector<std::vector<int>>(sig_width + 1, std::vector<int>());
      
      for (int i = 0; i < count; i++) {
        //std::cerr << i << "/" << count << std::endl;
        //signatureScores[i] = searchset.hammingDistance(searchSignatureId, collection, i);
        auto hd = searchset.hammingDistance(searchSignatureId, collection, i);
        
        if (hd <= hammingThreshold) {
          scoreLists[hd].push_back(i);
        }
        
        //if ((i+1) % 1000000 == 0) {
        //  std::cerr << (i+1) << std::endl;
        //}
        //std::cerr << i << ": " << signatureScores[i] << std::endl;
      }
      
      return scoreLists;
    }

  // Extracts the top K signatures from the provided distances matrix
  // Returns a vector of (document ID, distance) pairs.
  std::vector<std::pair<int, int>> ExtractTopK(std::vector<std::vector<int>> distances)
  {
    std::vector<std::pair<int, int>> topK;
    
    
  }
    
  private:
    const SignatureCollection &searchset;
    const SignatureCollection &collection;
};

void RunExhaustiveDocsimSearchPP()
{
  try
  {
    std::cerr << "Loading" << std::endl;
    auto SigCol = SignatureCollection(Config("SIGNATURE-PATH"));
    std::cerr << "Loaded. Searching." << std::endl;
    auto SigSearch = SignatureSearcher(SigCol, SigCol);
    for (int docId = 0; docId < 100; docId++) {
      auto res = SigSearch.Search(docId);
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}
