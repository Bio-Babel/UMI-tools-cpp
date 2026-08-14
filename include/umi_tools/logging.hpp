// logging.hpp — Utilities' logging and the Start/Stop experiment lifecycle.
//
// WHAT IS AND IS NOT BYTE-REPRODUCIBLE HERE, established by running the oracle:
//
// Every line these functions emit goes to `global_options.stdlog` — the file
// named by `-L/--log`, or stdout when no `-L` is given. And every one of them is
// either `#`-prefixed (getHeader/getParams/getFooter) or a timestamped `logging`
// record. The golden harness drops all `#`-prefixed lines on BOTH sides, and
// almost every fixture passes `-L test.log`, so none of this text reaches a
// compared byte stream.
//
// That is why this module is faithful in STRUCTURE and explicitly not in the
// following fields, each of which is unreproducible by nature:
//   * `# job started at <asctime> on <host> -- <uuid>` — wall clock, hostname,
//     and a per-run uuid4;
//   * `# pid: <n>, system: <uname>`;
//   * `2026-08-05 16:44:50,535 INFO <message>` — Python logging's timestamp;
//   * `# job finished in N seconds ... <times>` — CPU/wall times;
//   * the `stdin`/`stdout`/`stdlog`/`stderr` parameter lines, which in Python
//     print a file-object repr such as
//     `<_io.TextIOWrapper name='/tmp/t.log' mode='a' encoding='UTF-8'>`.
// The deviation is recorded in 10_validation.md rather than papered over.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace umi_tools {

class Values;

/// Python's logging levels as umi_tools uses them (loglevel is an int option).
enum class LogLevel { Debug, Info, Warning, Error, Critical };

/// The process-wide log sink, mirroring Utilities' module globals. A singleton is
/// the honest model: the Python really does keep `global_options` and write to it
/// from anywhere, and pretending otherwise would mean threading a logger through
/// every ported function for no behavioural gain.
class Log {
 public:
  static Log& instance();

  /// Open the log destination. `filename` empty or "-" means stdout;
  /// log2stderr redirects to stderr, as --log2stderr does.
  ///
  /// `stdout_is_same_stream` reproduces the branch in Utilities.Start:
  ///
  ///     if global_options.stdout == global_options.stdlog:
  ///         format = '# %(asctime)s %(levelname)s %(message)s'
  ///     else:
  ///         format = '%(asctime)s %(levelname)s %(message)s'
  ///
  /// and MultiLineFormatter then prepends ANOTHER '#' when the formatted record
  /// already starts with one — so log records interleaved with data on stdout come
  /// out as "## <asctime> INFO <msg>". That double hash is load-bearing: the
  /// shipped harness drops every '#'-prefixed line on both sides, so it is what
  /// keeps the log out of the compared output when no -L is given. Getting it
  /// wrong makes exactly the fixtures WITHOUT -L fail, and only those — measured
  /// on extract_indrop_output_filtered, the one extract fixture with no -L.
  void open(const std::string& filename, bool log2stderr, std::int64_t loglevel,
            bool stdout_is_same_stream = false);

  void info(std::string_view message);
  void warning(std::string_view message);
  void warn(std::string_view message);  ///< alias, as Utilities has both
  void debug(std::string_view message);
  void error(std::string_view message);

  /// Utilities.log(loglevel, message) -> logging.log(loglevel, message). Takes a
  /// PYTHON LOGGING level (10/20/30/40/50), not umi_tools' -v scale. It has no
  /// caller anywhere in the package — dead public API, ported for completeness.
  void log(std::int64_t level, std::string_view message);
  void critical(std::string_view message);
  void log(LogLevel level, std::string_view message);

  /// Raw write, for the `#` header/param/footer blocks.
  void write_raw(std::string_view text);

  std::int64_t loglevel() const { return loglevel_; }
  void close();

 private:
  Log() = default;
  struct Impl;
  std::shared_ptr<Impl> impl_;
  std::int64_t threshold() const;
  std::int64_t loglevel_ = 1;
  bool hash_prefix_ = false;
};

/// Utilities.getHeader / getParams / getFooter. Each returns only `#`-prefixed
/// lines; see the note at the top of this file about which fields differ.
std::string get_header(const std::vector<std::string>& argv);
std::string get_params(const Values& options);
std::string get_footer();

/// Utilities.Start's handling of -E/--error, and Stop's matching close.
///
/// Start does
///     if global_options.stderr != sys.stderr:
///         if global_options.stderr == "stderr": pass          # keep the STRING
///         else: global_options.stderr = openFile(..., "w")    # create/truncate
/// and Stop then does `global_options.stderr.close()` under the same guard.
/// The port never read the dest at all. MEASURED, all five values of -E:
///
///   | -E          | oracle                                   | port (before)   |
///   |-------------|------------------------------------------|-----------------|
///   | absent      | rc 0, no file                            | same            |
///   | `run.err`   | rc 0, run.err created and TRUNCATED (0B)  | no file        |
///   | `-`         | rc 0, a file literally NAMED `-`         | no file         |
///   | `/nodir/x`  | rc 1 FileNotFoundError, BEFORE any work  | rc 0, full work |
///   | `stderr`    | rc 1 AttributeError at Stop, AFTER all    | rc 0            |
///   |             |   work and after the footer is logged     |                 |
///
/// `-` is why this keys off was_given() rather than the value: `-E -` is a
/// request to open a file named `-`, and the port's default for the dest is the
/// string "-", so the value alone cannot tell the two apart.
void start_open_error_file(const Values& options);

/// The Stop half. Throws for the literal `-E stderr`, which upstream leaves as a
/// str and then calls .close() on. Call AFTER the footer is written, because
/// upstream's footer reaches the log before the close raises (measured).
void stop_close_error_file(const Values& options);

/// Utilities.Stop's `--timeit` block (Utilities.py:1415-1446): when
/// `--timeit` names a file, APPEND one 14-column TSV row describing the run,
/// preceded by a header line when `--timeit-header` is given. The file is
/// opened in append mode, so repeated runs accumulate.
///
/// `argv0` is Python's `sys.argv[0]` — the last column. Upstream has a
/// `run.py` special case that cannot apply here (that is a CGAT wrapper, not
/// this package's entry point), so the else-branch is what runs.
void write_timeit(const std::string& path, const std::string& name, bool header,
                  const std::string& argv0);

/// Records `argv[0]` for the `--timeit` row. Called once from main().
void set_program_path(std::string argv0);
const std::string& program_path();

/// Utilities.error(): logs and then exits. It is NOT an exception in Python
/// either — U.error writes the message and the process terminates.
/// How the port leaves the process.
///
/// `std::exit` DOES NOT UNWIND, so any RAII object still alive keeps its
/// destructor unrun. C's exit() flushes FILE* and std::cout, but it does NOT
/// finalise a zlib gzFile or an htslib BGZF stream — so an error inside
/// extract's read loop with `-S out.fastq.gz` used to lose the buffered data and
/// the gzip trailer, leaving an unreadable .gz, and an error inside dedup left
/// the output BAM without its BGZF EOF block. CPython's refcounting closes those
/// handles on SystemExit, so upstream does not have the problem.
///
/// So the exit paths THROW this instead, and main() catches it after unwinding
/// has run every Writer/AlignmentWriter/LineReader destructor. It deliberately
/// does NOT derive from std::exception: the per-tool `catch (const std::exception&)`
/// around parse_args must not swallow it.
struct ExitRequest {
  int code;
  std::string message;   // already formatted; main writes it to stderr verbatim
};

[[noreturn]]
/// Upstream has TWO distinct failure shapes and they are NOT interchangeable:
///
///   U.error(msg)        logging.error(msg)  ->  an ERROR line in the log
///                       stderr gets the FIXED string "UMI-tools failed with an
///                       error. Check the log file", not the message
///                       sys.exit(1)
///   raise ValueError    NO log line at all; the message reaches stderr as part
///                       of a Python traceback; the interpreter exits 1
///
/// Measured on `dedup --unmapped-reads=use` without --paired: the oracle's log
/// has ONE line and the port's had TWO, because every failure was routed through
/// error_exit. validateSamOptions RAISES, so it must not log.
void raise_value_error(std::string_view message, int code = 1);

void error_exit(std::string_view message, int code = 1);

/// Utilities.DefaultOptions — the values `global_options` carries BEFORE Start()
/// runs. Only loglevel and compresslevel are numbers; the four streams are the
/// process's own, which is what "-" means throughout this port.
struct DefaultOptions {
  static constexpr std::int64_t kLogLevel = 2;
  static constexpr int kCompressLevel = 6;
  static constexpr const char* kStdlog = "-";
  static constexpr const char* kStdout = "-";
  static constexpr const char* kStderr = "stderr";
  static constexpr const char* kStdin = "-";
};

}  // namespace umi_tools
