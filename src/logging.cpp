#include "umi_tools/logging.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/utsname.h>
#include <sys/times.h>
#include <random>
#include <unistd.h>

#include "umi_tools/options.hpp"

namespace umi_tools {

struct Log::Impl {
  std::FILE* fp = nullptr;   // nullptr => use the stream below
  std::ostream* stream = &std::cout;
  bool owns_file = false;
  ~Impl() {
    if (owns_file && fp != nullptr) std::fclose(fp);
  }
};

Log& Log::instance() {
  static Log inst;
  return inst;
}

void Log::open(const std::string& filename, bool log2stderr, std::int64_t loglevel,
               bool stdout_is_same_stream) {
  impl_ = std::make_shared<Impl>();
  loglevel_ = loglevel;
  hash_prefix_ = stdout_is_same_stream;
  // Upstream's three-way choice is an if/ELIF, so an explicit
  // -L/--log FILE takes PRIORITY over --log2stderr (Utilities.py:1120-1123):
  //     if global_options.stdlog != sys.stdout: openFile(stdlog, "a")
  //     elif global_options.log2stderr:         stdlog = stderr
  // Testing log2stderr FIRST and returning early inverted that. MEASURED with
  // `dedup -L run.log --log2stderr`: oracle wrote 4,095 bytes to run.log and
  // nothing to stderr; the port left run.log uncreated and put all 4,095 bytes
  // on stderr — the log silently went somewhere the user did not ask for.
  const bool log_is_default = filename.empty() || filename == "-";
  if (log_is_default) {
    if (log2stderr) {
      impl_->stream = &std::cerr;
      return;
    }
    impl_->stream = &std::cout;
    return;
  }
  // openFile (Utilities.py:489-527) dispatches on the EXTENSION and
  // raises NotImplementedError("mode '{}' not implemented") for any gzip mode
  // other than "r"/"w". Start opens the log with mode "a", so
  // `umi_tools dedup -L run.log.gz` aborts upstream. MEASURED:
  //   NotImplementedError: mode 'a' not implemented   (rc 1, no file)
  // This ignored the extension and wrote an UNCOMPRESSED text file named
  // run.log.gz — a file whose name promises gzip and whose contents are not.
  {
    const auto dot = filename.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string()
                                                     : filename.substr(dot);
    if (ext == ".gz" || ext == ".z")
      throw std::runtime_error("mode 'a' not implemented");
  }
  // Python opens the log with mode "a" (append) — visible in the parameter dump
  // as mode='a'. Matching it matters when a caller reuses a log file.
  impl_->fp = std::fopen(filename.c_str(), "a");
  if (impl_->fp == nullptr) {
    std::cerr << "cannot open log file: " << filename << "\n";
    std::exit(1);
  }
  impl_->owns_file = true;
}

void Log::write_raw(std::string_view text) {
  if (!impl_) impl_ = std::make_shared<Impl>();
  if (impl_->fp != nullptr) {
    std::fwrite(text.data(), 1, text.size(), impl_->fp);
    std::fflush(impl_->fp);
  } else {
    impl_->stream->write(text.data(), static_cast<std::streamsize>(text.size()));
    impl_->stream->flush();
  }
}

namespace {

// Python logging's default-ish record used by umi_tools:
//     "2026-08-05 16:44:50,535 INFO <message>"
// The timestamp is inherently unreproducible; the SHAPE is reproduced so a
// human reading both logs sees the same structure.
std::string log_line(const char* level, std::string_view message, bool hash_prefix) {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  // Utilities.Start's format string, then MultiLineFormatter's extra '#'.
  std::ostringstream hs;
  if (hash_prefix) hs << "# ";
  hs << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << ',' << std::setfill('0')
     << std::setw(3) << ms.count() << ' ' << level << ' ';
  const std::string header = hs.str();

  // MultiLineFormatter.format: when the record already starts with '#', prepend
  // one more and indent every continuation line by len(header) after it.
  const std::string prefix = hash_prefix ? "#" : "";
  std::string body(message);
  const std::string cont = "\n" + prefix + std::string(header.size(), ' ');
  std::string wrapped;
  for (char ch : body) {
    if (ch == '\n') wrapped += cont; else wrapped += ch;
  }
  return prefix + header + wrapped + "\n";
}

}  // namespace

// Utilities.Start's mapping from the -v scale onto logging levels:
//   -v 0 -> logging.ERROR (40)   -v 1 -> logging.INFO (20)   -v >1 -> DEBUG (10)
// MEASURED: -v 0 is NOT silent -- error and critical still reach the log. The
// port used to drop everything at -v 0, which no fixture caught because the
// fixtures run at the default verbosity and their -v 0 paths log nothing above
// INFO anyway.
std::int64_t Log::threshold() const {
  if (loglevel_ == 0) return 40;
  if (loglevel_ == 1) return 20;
  return 10;
}

void Log::log(LogLevel level, std::string_view message) {
  switch (level) {
    case LogLevel::Debug:    log(10, message); return;
    case LogLevel::Info:     log(20, message); return;
    case LogLevel::Warning:  log(30, message); return;
    case LogLevel::Error:    log(40, message); return;
    case LogLevel::Critical: log(50, message); return;
  }
}

void Log::info(std::string_view m) { log(LogLevel::Info, m); }
void Log::warning(std::string_view m) { log(LogLevel::Warning, m); }
void Log::warn(std::string_view m) { log(LogLevel::Warning, m); }
void Log::debug(std::string_view m) { log(LogLevel::Debug, m); }
void Log::error(std::string_view m) { log(LogLevel::Error, m); }
void Log::critical(std::string_view m) { log(LogLevel::Critical, m); }

void Log::log(std::int64_t level, std::string_view message) {
  if (level < threshold()) return;
  // logging.getLevelName(n): a NON-STANDARD level renders as "Level N", it does
  // NOT round down to the nearest named one. Measured: logging.log(45, m) writes
  // "Level 45 m", not "ERROR m".
  std::string name;
  switch (level) {
    case 10: name = "DEBUG"; break;
    case 20: name = "INFO"; break;
    case 30: name = "WARNING"; break;
    case 40: name = "ERROR"; break;
    case 50: name = "CRITICAL"; break;
    default: name = "Level " + std::to_string(level); break;
  }
  write_raw(log_line(name.c_str(), message, hash_prefix_));
}

void Log::close() {
  if (impl_ && impl_->fp != nullptr) std::fflush(impl_->fp);
}

// --------------------------------------------------------------------------
// getHeader / getParams / getFooter
// --------------------------------------------------------------------------
std::string& program_path_storage() {
  static std::string p;
  return p;
}

namespace {

// uuid.uuid4() as Utilities' module-level `global_id`: one per process, in the
// 8-4-4-4-12 hex form with the version-4 and variant bits set.
const std::string& run_id() {
  static const std::string id = [] {
    std::random_device rd;
    std::uint8_t b[16];
    for (std::uint8_t& x : b) x = static_cast<std::uint8_t>(rd() & 0xFF);
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);   // version 4
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);   // variant
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
      out += hex[b[i] >> 4];
      out += hex[b[i] & 0x0F];
    }
    return out;
  }();
  return id;
}

// Utilities' module-level `global_starting_time`, set at import.
std::time_t start_time() {
  static const std::time_t t = std::time(nullptr);
  return t;
}

}  // namespace

std::string get_header(const std::vector<std::string>& argv) {
  (void)start_time();   // pin the start before any work happens
  // Python:
  //   "# UMI-tools version: %s\n# output generated by %s\n"
  //   "# job started at %s on %s -- %s\n# pid: %i, system: %s %s %s %s"
  // The version string is the hard-coded __version__ ("1.1.6"), NOT the
  // distribution version (1.1.7.dev53+...) — see 00_baseline.md.
  std::string joined;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i) joined += ' ';
    joined += argv[i];
  }
  const auto t = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  char asctime_buf[64];
  std::strftime(asctime_buf, sizeof(asctime_buf), "%a %b %e %H:%M:%S %Y", &tm_buf);

  char host[256] = {0};
  gethostname(host, sizeof(host) - 1);

  // `system, host, release, version, machine = os.uname()` — the header prints
  // system, release, version and machine. The port used to print "Linux" alone
  // and the literal "(run id)"; both are reproducible, so both are reproduced.
  struct utsname un{};
  uname(&un);

  std::ostringstream os;
  os << "# UMI-tools version: 1.1.6\n"
     << "# output generated by " << joined << "\n"
     << "# job started at " << asctime_buf << " on " << host << " -- " << run_id() << "\n"
     << "# pid: " << getpid() << ", system: " << un.sysname << " " << un.release
     << " " << un.version << " " << un.machine;
  return os.str();
}

namespace {

// The four stream dests hold FILE OBJECTS in Python, so `str(v)` prints a repr,
// not a path. Reproduced because the params block is part of every log:
//   default          <_io.TextIOWrapper name='<stdout>' mode='w' encoding='utf-8'>
//   an opened file   <_io.TextIOWrapper name='/path' mode='r' encoding='UTF-8'>
// Note the encoding CASE differs between the two — lower-case for the std
// streams Python inherits, upper-case for a file open() produced.
std::string stream_repr(const std::string& dest, const std::string& value, bool is_none,
                        bool was_given) {
  const bool is_stdin = dest == "stdin";
  // The MODE each stream is opened with, from Start (Utilities.py:1112-1125):
  //   stdin 'r', stdout 'w', stderr 'w', stdlog 'a'.
  // 'w' was used for all three non-stdin dests, so every run with -L rendered
  // `# stdlog ... mode='w'` where upstream renders mode='a'. The params block is
  // in every log, and parity_logging.py drops '#' lines (they carry timings and
  // a UUID), so nothing compared it.
  // The DEFAULT branch below reports the inherited std stream's own mode, which
  // is 'w' for stdout/stderr/stdlog and 'r' for stdin — `sys.stdout`'s repr says
  // mode='w' whatever the dest is. Only an OPENED file carries the mode Start
  // passed to openFile, and for stdlog alone that is 'a'. Applying 'a'
  // unconditionally broke the default case: parity_options.py caught
  //   stdlog: python=[... name='<stdout>' mode='w' ...] cpp=[... mode='a' ...]
  const char* std_mode = is_stdin ? "r" : "w";
  const char* open_mode = is_stdin ? "r" : (dest == "stdlog" ? "a" : "w");

  // `-E stderr` is special-cased in Start: the value stays a STRING,
  // so `str(v)` in getParams prints the bare word, not a TextIOWrapper repr.
  if (dest == "stderr" && was_given && value == "stderr") return "stderr";

  // Default-vs-supplied is was_given, NOT `value == "-"`: `-S -` and `-E -` ask
  // for a file literally NAMED `-`, and upstream renders `name='-'` with the
  // upper-case encoding a real open() produces.
  if (is_none || (!was_given && (value.empty() || value == "-"))) {
    const char* name = is_stdin ? "<stdin>" : (dest == "stderr" ? "<stderr>" : "<stdout>");
    return std::string("<_io.TextIOWrapper name='") + name + "' mode='" + std_mode +
           "' encoding='utf-8'>";
  }
  // openFile dispatches on the EXTENSION (Utilities.py:489-527): a
  // .gz/.z path is a gzip stream wrapped in a TextIOWrapper, whose repr has NO
  // `mode=` at all and says encoding='ascii'. MEASURED on both directions:
  //   oracle  <_io.TextIOWrapper name='out.fastq.gz' encoding='ascii'>
  //   port    <_io.TextIOWrapper name='out.fastq.gz' mode='w' encoding='UTF-8'>
  // getParams writes this verbatim into the user's -L log.
  const auto dot = value.rfind('.');
  const std::string ext = dot == std::string::npos ? std::string() : value.substr(dot);
  if (ext == ".gz" || ext == ".z")
    return "<_io.TextIOWrapper name='" + value + "' encoding='ascii'>";

  return "<_io.TextIOWrapper name='" + value + "' mode='" + open_mode +
         "' encoding='UTF-8'>";
}

bool is_stream_dest(const std::string& d) {
  return d == "stdin" || d == "stdout" || d == "stdlog" || d == "stderr";
}

}  // namespace

std::string get_params(const Values& options) {
  // Python: for k, v in sorted(members.items()): "# %-40s: %s" % (k, str(v))
  // std::map is already sorted by key, which is what `sorted()` gives.
  std::ostringstream os;
  bool first = true;
  for (const auto& [dest, val] : options.raw()) {
    if (!first) os << '\n';
    first = false;
    os << "# " << std::left << std::setw(40) << dest << ": "
       << (is_stream_dest(dest) ? stream_repr(dest, val.first, val.second,
                                            options.was_given(dest))
                                : (val.second ? "None" : val.first));
  }
  if (first) return "# no parameters.";
  return os.str();
}

std::string get_footer() {
  const auto t = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  char asctime_buf[64];
  std::strftime(asctime_buf, sizeof(asctime_buf), "%a %b %e %H:%M:%S %Y", &tm_buf);

  // "# job finished in %i seconds at %s -- %s -- %s" with the four os.times()
  // values at "%5.2f". The port emitted only the timestamp, dropping the elapsed
  // count, all four times and the job id — every one of which is reproducible.
  const std::int64_t elapsed = static_cast<std::int64_t>(t - start_time());
  struct tms tb{};
  ::times(&tb);
  const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
  const double vals[4] = {static_cast<double>(tb.tms_utime) / hz,
                          static_cast<double>(tb.tms_stime) / hz,
                          static_cast<double>(tb.tms_cutime) / hz,
                          static_cast<double>(tb.tms_cstime) / hz};
  char times_buf[128];
  std::snprintf(times_buf, sizeof(times_buf), "%5.2f %5.2f %5.2f %5.2f",
                vals[0], vals[1], vals[2], vals[3]);

  std::ostringstream os;
  os << "# job finished in " << elapsed << " seconds at " << asctime_buf
     << " -- " << times_buf << " -- " << run_id();
  return os.str();
}

void set_program_path(std::string argv0) { program_path_storage() = std::move(argv0); }
const std::string& program_path() { return program_path_storage(); }

void write_timeit(const std::string& path, const std::string& name, bool header,
                  const std::string& argv0) {
  // Append mode, exactly as `open(..., "a")`: a second run adds a second row.
  std::ofstream out(path, std::ios::app);
  if (!out) return;

  if (header)
    out << "name\twall\tuser\tsys\tcuser\tcsys\thost\tsystem\trelease\tmachine"
           "\tstart\tend\tpath\tcmd\n";

  const std::time_t t_end = std::time(nullptr);
  struct tms tb{};
  ::times(&tb);
  const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
  char nums[128];
  std::snprintf(nums, sizeof(nums), "%5.2f\t%5.2f\t%5.2f\t%5.2f\t%5.2f",
                static_cast<double>(t_end - start_time()),
                static_cast<double>(tb.tms_utime) / hz,
                static_cast<double>(tb.tms_stime) / hz,
                static_cast<double>(tb.tms_cutime) / hz,
                static_cast<double>(tb.tms_cstime) / hz);

  // os.uname() is (sysname, nodename, release, version, machine), unpacked
  // upstream as (csystem, host, release, version, machine) — so the row's
  // "host" is nodename and its "system" is sysname. `version` is unused.
  struct utsname un{};
  ::uname(&un);

  auto asctime_of = [](std::time_t when) {
    std::tm tm_buf{};
    localtime_r(&when, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Y", &tm_buf);
    return std::string(buf);
  };

  std::error_code ec;
  const std::string cwd = std::filesystem::current_path(ec).string();

  out << name << "\t" << nums << "\t" << un.nodename << "\t" << un.sysname << "\t"
      << un.release << "\t" << un.machine << "\t" << asctime_of(start_time()) << "\t"
      << asctime_of(t_end) << "\t" << cwd << "\t" << argv0 << "\n";
}

void error_exit(std::string_view message, int code) {
  Log::instance().error(message);
  // U.error writes this FIXED text, not the message; the message goes to the log.
  throw ExitRequest{code, "UMI-tools failed with an error. Check the log file\n"};
}

void raise_value_error(std::string_view message, int code) {
  // `raise ValueError(msg)`: no log line, message on stderr, exit 1. A Python
  // traceback has no faithful C++ counterpart, so the port emits the message
  // alone — see L31.
  throw ExitRequest{code, std::string(message) + "\n"};
}

void start_open_error_file(const Values& options) {
  if (!options.was_given("stderr")) return;              // default: sys.stderr
  const std::string dest = options.get_string("stderr");
  if (dest == "stderr") return;                          // kept as a str; see Stop
  // openFile(dest, "w") — create or TRUNCATE. A failure is upstream's
  // FileNotFoundError, raised from Start and so BEFORE any of main's work.
  std::FILE* f = std::fopen(dest.c_str(), "w");
  if (f == nullptr)
    throw std::invalid_argument("[Errno 2] No such file or directory: '" + dest + "'");
  std::fclose(f);
}

void stop_close_error_file(const Values& options) {
  if (!options.was_given("stderr")) return;
  if (options.get_string("stderr") != "stderr") return;
  // `global_options.stderr.close()` on the STRING "stderr" (Utilities.py:1413).
  throw std::invalid_argument("'str' object has no attribute 'close'");
}

}  // namespace umi_tools
