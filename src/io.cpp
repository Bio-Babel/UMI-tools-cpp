#include "umi_tools/io.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace umi_tools {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

bool is_gzip_name(std::string_view filename) {
  // os.path.splitext: the extension is everything from the LAST dot in the final
  // path component, and a leading dot does not count as an extension.
  const std::size_t slash = filename.find_last_of('/');
  const std::string_view base =
      slash == std::string_view::npos ? filename : filename.substr(slash + 1);
  const std::size_t dot = base.find_last_of('.');
  if (dot == std::string_view::npos || dot == 0) return false;
  const std::string ext = lower(std::string(base.substr(dot)));
  return ext == ".gz" || ext == ".z";
}

// --------------------------------------------------------------------------
// LineReader
// --------------------------------------------------------------------------
struct LineReader::Impl {
  gzFile gz = nullptr;
  std::FILE* fp = nullptr;
  bool use_stdin = false;
  std::string buffer;
};

LineReader::LineReader(const std::string& filename)
    : impl_(std::make_unique<Impl>()), name_(filename) {
  if (filename == "-") {
    impl_->use_stdin = true;
    return;
  }
  if (is_gzip_name(filename)) {
    impl_->gz = gzopen(filename.c_str(), "rb");
    if (impl_->gz == nullptr)
      throw std::runtime_error("cannot open gzip file for reading: " + filename);
  } else {
    impl_->fp = std::fopen(filename.c_str(), "rb");
    if (impl_->fp == nullptr)
      throw std::runtime_error("cannot open file for reading: " + filename);
  }
}

LineReader::~LineReader() {
  if (impl_->gz != nullptr) gzclose(impl_->gz);
  if (impl_->fp != nullptr) std::fclose(impl_->fp);
}

bool LineReader::next(std::string& line) {
  line.clear();
  if (impl_->use_stdin) return static_cast<bool>(std::getline(std::cin, line));

  if (impl_->gz != nullptr) {
    // gzgets needs a buffer; loop so arbitrarily long lines still work.
    char buf[8192];
    bool any = false;
    while (gzgets(impl_->gz, buf, sizeof(buf)) != nullptr) {
      any = true;
      line += buf;
      if (!line.empty() && line.back() == '\n') break;
    }
    if (!any) return false;
  } else {
    int c;
    bool any = false;
    while ((c = std::fgetc(impl_->fp)) != EOF) {
      any = true;
      if (c == '\n') break;
      line.push_back(static_cast<char>(c));
    }
    if (!any) return false;
  }
  // Strip the trailing newline (and a CR before it), matching Python's
  // universal-newline text mode.
  if (!line.empty() && line.back() == '\n') line.pop_back();
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return true;
}

// --------------------------------------------------------------------------
// Writer
// --------------------------------------------------------------------------
struct Writer::Impl {
  gzFile gz = nullptr;
  std::FILE* fp = nullptr;
  bool use_stdout = false;
  bool closed = false;
};

Writer::Writer(const std::string& filename, int compresslevel)
    : impl_(std::make_unique<Impl>()), name_(filename) {
  if (filename == "-") {
    impl_->use_stdout = true;
    return;
  }
  if (is_gzip_name(filename)) {
    // "wb<level>" is zlib's spelling of the compression level; the default of 6
    // is what --compresslevel documents ("matches GNU gzip").
    const std::string mode = "wb" + std::to_string(compresslevel);
    impl_->gz = gzopen(filename.c_str(), mode.c_str());
    if (impl_->gz == nullptr)
      throw std::runtime_error("cannot open gzip file for writing: " + filename);
  } else {
    impl_->fp = std::fopen(filename.c_str(), "wb");
    if (impl_->fp == nullptr)
      throw std::runtime_error("cannot open file for writing: " + filename);
  }
}

Writer::~Writer() { close(); }

void Writer::write(std::string_view text) {
  if (impl_->closed) throw std::logic_error("Writer::write after close");
  if (impl_->use_stdout) {
    std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
    return;
  }
  if (impl_->gz != nullptr) {
    if (!text.empty() &&
        gzwrite(impl_->gz, text.data(), static_cast<unsigned>(text.size())) == 0)
      throw std::runtime_error("gzwrite failed for " + name_);
    return;
  }
  if (!text.empty() && std::fwrite(text.data(), 1, text.size(), impl_->fp) != text.size())
    throw std::runtime_error("fwrite failed for " + name_);
}

void Writer::close() {
  if (impl_->closed) return;
  impl_->closed = true;
  if (impl_->use_stdout) {
    std::cout.flush();
    return;
  }
  if (impl_->gz != nullptr) {
    gzclose(impl_->gz);
    impl_->gz = nullptr;
  }
  if (impl_->fp != nullptr) {
    std::fclose(impl_->fp);
    impl_->fp = nullptr;
  }
}

}  // namespace umi_tools
