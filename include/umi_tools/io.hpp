// io.hpp — Utilities.openFile: transparent gzip by filename extension.
//
// The Python:
//     _, ext = os.path.splitext(filename)
//     if ext.lower() in (".gz", ".z"):  gzip.open(filename, 'rt'|'wt',
//                                          compresslevel=options.compresslevel,
//                                          encoding="ascii")
//     else:                             open(filename, mode)
//
// Three details that are contractual rather than incidental:
//   * the extension set is {".gz", ".z"}, case-insensitive — NOT just ".gz";
//   * only modes "r" and "w" are implemented for gzip; anything else raises
//     NotImplementedError with a specific message;
//   * the compression level comes from --compresslevel, whose default is 6
//     "matchesGNU gzip rather than python gzip default (which is 9)" per its own
//     help text. Writing at 9 would produce a byte-different .gz for identical
//     content, and one fixture compares a gzipped output file.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace umi_tools {

/// True when Utilities.openFile would use gzip for this name.
bool is_gzip_name(std::string_view filename);

/// Line-oriented reader over a plain or gzipped file, or stdin ("-").
/// Streaming by construction: never holds more than one line, because the FASTQ
/// and TSV inputs can exceed RAM and collecting them would pass every fixture and
/// fail on real data.
class LineReader {
 public:
  explicit LineReader(const std::string& filename);  ///< "-" reads stdin
  ~LineReader();
  LineReader(const LineReader&) = delete;
  LineReader& operator=(const LineReader&) = delete;

  /// Reads the next line WITHOUT its trailing newline. Returns false at EOF.
  bool next(std::string& line);

  const std::string& name() const { return name_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string name_;
};

/// Writer to a plain or gzipped file, or stdout ("-").
class Writer {
 public:
  explicit Writer(const std::string& filename, int compresslevel = 6);
  ~Writer();
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  void write(std::string_view text);
  void close();

  const std::string& name() const { return name_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string name_;
};

/// Utilities.getTempFilename(dir=tmpdir) — creates the file (so the name is
/// reserved) and returns its path. `dir` empty means the system temp dir, which
/// is what tempfile does when dir is None.
/// Utilities.getTempFile(dir, shared=False, suffix="") ->
/// tempfile.NamedTemporaryFile(dir=dir, delete=False, prefix="ctmp", suffix=...)
/// The file is CREATED and left on disk (delete=False); the caller reopens it by
/// name. Returns the path, since a C++ caller has no use for Python's file object.
std::string get_temp_file(const std::string& dir, const std::string& suffix = "");

std::string get_temp_filename(const std::string& dir);

/// Python's `"%.<n>f" % x`. Both Python and C round-half-to-even through the
/// same double formatting, so snprintf is the same function, not an approximation.
std::string format_fixed(double value, int precision);

}  // namespace umi_tools
