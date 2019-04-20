#include "files.hpp"

size_t get_file_size( std::FILE *stream )
{
  size_t num_bytes = 0;
  while( std::fgetc( stream ) != EOF )
    ++num_bytes;
  std::rewind( stream );
  return num_bytes;
}