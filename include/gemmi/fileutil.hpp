// Copyright 2018 Global Phasing Ltd.
//
// File-related utilities.

#ifndef GEMMI_FILEUTIL_HPP_
#define GEMMI_FILEUTIL_HPP_

#include <cctype>    // for isdigit, isalnum
#include <cstdio>    // for FILE, fopen, fclose
#include <cstdlib>   // getenv
#include <cstring>   // strlen
#include <initializer_list>
#include <memory>    // for unique_ptr
#include <string>
#include "util.hpp"  // for fail, to_lower

#if defined(_WIN32) && defined(GEMMI_WINDOWS_PATHS_IN_UTF8)
#include <locale>
#include <codecvt>
#endif

namespace gemmi {

// strip directory and suffixes from filename
inline std::string path_basename(const std::string& path,
                                 std::initializer_list<const char*> exts) {
  size_t pos = path.find_last_of("\\/");
  std::string basename = pos == std::string::npos ? path : path.substr(pos + 1);
  for (const char* ext : exts) {
    size_t len = std::strlen(ext);
    if (basename.size() > len &&
        basename.compare(basename.length() - len, len, ext, len) == 0)
      basename.resize(basename.length() - len);
  }
  return basename;
}

// file operations
typedef std::unique_ptr<FILE, decltype(&std::fclose)> fileptr_t;

inline fileptr_t file_open(const char *path, const char *mode) {
  std::FILE* file;
#if defined(_WIN32) && defined(GEMMI_WINDOWS_PATHS_IN_UTF8)
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
  std::wstring wpath = convert.from_bytes(path);
  std::wstring wmode = convert.from_bytes(mode);
  if ((file = ::_wfopen(wpath.c_str(), wmode.c_str())) == nullptr)
#else
  if ((file = std::fopen(path, mode)) == nullptr)
#endif
    fail("Failed to open file: " + std::string(path));
  return fileptr_t(file, &std::fclose);
}

inline std::size_t file_size(FILE* f, const std::string& path) {
  if (std::fseek(f, 0, SEEK_END) != 0)
    fail(path + ": fseek failed");
  long length = std::ftell(f);
  if (length < 0)
    fail(path + ": ftell failed");
  if (std::fseek(f, 0, SEEK_SET) != 0)
    fail(path + ": fseek failed");
  return length;
}

inline bool is_pdb_code(const std::string& str) {
  return str.length() == 4 && std::isdigit(str[0]) && std::isalnum(str[1]) &&
                              std::isalnum(str[2]) && std::isalnum(str[3]);
}

// Call it after checking the code with gemmi::is_pdb_code(code).
// The convention for $PDB_DIR is the same as in BioJava, see the docs.
inline std::string expand_pdb_code_to_path(const std::string& code) {
  if (const char* pdb_dir = std::getenv("PDB_DIR")) {
    std::string lc = to_lower(code);
    return std::string(pdb_dir) + "/structures/divided/mmCIF/" +
           lc.substr(1, 2) + "/" + lc + ".cif.gz";
  }
  return std::string{};
}

inline std::string expand_if_pdb_code(const std::string& input) {
  std::string path;
  if (is_pdb_code(input)) {
    path = gemmi::expand_pdb_code_to_path(input);
    if (path.empty())
      fail(input + " is a PDB code, but $PDB_DIR is not set.\n");
  } else {
    path = input;
  }
  return path;
}

// helper function for working with binary files
inline bool is_little_endian() {
  std::uint32_t x = 1;
  return *reinterpret_cast<char *>(&x) == 1;
}

inline void swap_two_bytes(void* start) {
  char* bytes = static_cast<char*>(start);
  std::swap(bytes[0], bytes[1]);
}

inline void swap_four_bytes(void* start) {
  char* bytes = static_cast<char*>(start);
  std::swap(bytes[0], bytes[3]);
  std::swap(bytes[1], bytes[2]);
}

} // namespace gemmi
#endif
// vim:sw=2:ts=2:et
