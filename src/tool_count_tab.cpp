// count_tab — port of umi_tools/count_tab.py::main.
//
// Transcribed from the Python (read and run). The Python, condensed:
//
//   if options.per_cell: bc_getter = get_cell_umi_read_string(sep=bc_sep)
//   else:                bc_getter = get_umi_read_string(sep=bc_sep)
//   if options.per_cell: stdout.write("cell\tgene\tcount\n")
//   else:                stdout.write("gene\tcount\n")
//   processor = network.UMIClusterer(options.method)
//   for gene, counts in get_gene_count_tab(options.stdin, bc_getter):
//       for cell in counts.keys():
//           umis = counts[cell].keys()
//           nInput += sum(counts[cell].values())
//           groups = processor(counts[cell], threshold=options.threshold)
//           gene_count = len(groups)
//           if options.per_cell: stdout.write("%s\t%s\t%i\n" % (cell, gene, gene_count))
//           else:                stdout.write("%s\t%i\n" % (gene, gene_count));  nOutput += gene_count
//   U.info("Number of reads counted: %i" % nOutput)
//
// Four details that are contractual:
//
//  1. The COLUMN ORDER differs between the two modes and from `count`'s:
//     count_tab --per-cell writes cell,gene,count while `count` --per-cell (long
//     form) writes gene,cell,count. Preserved as-is (01_audit.md D8).
//
//  2. `for cell in counts.keys()` is INSERTION ORDER, and it reaches the output:
//     the golden lists cells in order of first appearance in the input, which for
//     gene ENSG00000011304.18 is GATCGATTCGAGGATA before ATAGATAGCGATAGCG — the
//     reverse of alphabetical. An unordered map here would silently reorder every
//     per-cell row.
//
//  3. `nOutput` is incremented ONLY in the non-per-cell branch, so the log line
//     "Number of reads counted:" reports 0 for every --per-cell run. That is
//     upstream bug D7#4 and is reproduced, not fixed.
//
//  4. `nInput` is accumulated and never used for anything but that arithmetic —
//     no output depends on it — so it is kept only to mirror the source.
#include <cstdint>
#include <string>
#include <vector>

#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/tools.hpp"

namespace umi_tools {

int tool_count_tab(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  if (spec == nullptr) return 1;

  // --no-usage only EXISTS when it is already in argv (Utilities.py:456).
  const MaybeNoUsage no_usage(*spec, argv);
  spec = &no_usage.spec();

  // count_tab.py: if len(argv) == 1: print_usage(); "Required options missing";
  // return 1.  argv here excludes the program name, so the equivalent test is an
  // empty option list.
  if (argv.empty()) {
    // parser.print_usage() = format_usage(usage), i.e. the usage text, a blank
    // line, the documentation URL and another blank line — the same block the
    // other six tools emit. Writing usage alone dropped three lines.
    Writer out("-");
    out.write(usage_block(*spec));
    out.write("Required options missing, see --help for more details\n");
    return 1;
  }

  // optparse turns a bad option into `<prog>: error: ...` and exit 2, not
  // an abort. Without this catch the exception escaped main and the
  // process died with SIGABRT (rc 134) -- measured against the oracle.
  ParseResult parsed;
  try {
    parsed = parse_args(*spec, argv);
  } catch (const std::exception& e) {
    parser_error(*spec, e.what());
  }
  if (parsed.wants_help) {
    Writer out("-");
    out.write(format_help(*spec, parsed.help_extended));
    return 0;
  }
  if (parsed.wants_version) {
    Writer out("-");
    out.write("count_tab version: $Id$\n");
    return 0;
  }

  const Values& options = parsed.values;

  // U.Start's I/O redirection: -L/--log, --log2stderr, -S/--stdout, -I/--stdin.
  const std::string log_name =
      options.is_none("stdlog") ? std::string() : options.get_string("stdlog");
  Log& log = Log::instance();
  // stdlog == stdout exactly when neither is redirected (Utilities.Start).
  const bool log_is_stdout = (log_name.empty() || log_name == "-") &&
                             !options.get_bool("log2stderr");
  const bool out_is_stdout =
      options.is_none("stdout") || options.get_string("stdout") == "-";
  // Start opens options.stdout FIRST, before -E and before the
  // log, so every later failure leaves a zero-length file behind. An earlier pass
  // fixed dedup/group/count only; MEASURED on an rc=1 path, whitelist,
  // count_tab and prepare_for_em each left NO file where the oracle leaves 0B.
  // extract already matched.
  touch_start_output(options.was_given("stdout") && !options.is_none("stdout")
                         ? options.get_string("stdout")
                         : std::string());

  // Utilities.Start opens the three pipe streams IN ORDER: stdout
  // (Utilities.py:1113), then stderr (:1115-1119), then stdlog (:1120). The
  // order is observable — a -E path that cannot be opened raises from Start
  // BEFORE the log exists, so upstream leaves NO log file. MEASURED with
  // `-E /nonexistent_dir/run.err`: oracle rc=1 with out.sam created (0B) and no
  // run.log; with -E opened after the log the port had it the other way round.
  start_open_error_file(options);

  log.open(log_name, options.get_bool("log2stderr"),
           options.has("loglevel") ? options.get_int("loglevel") : 1,
           log_is_stdout && out_is_stdout);

  // Start writes the header and the parameter dump to the log at loglevel >= 1.
  if (log.loglevel() >= 1) {
    std::vector<std::string> full_argv{"count_tab"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  const std::string in_name =
      options.is_none("stdin") ? std::string("-") : options.get_string("stdin");
  const std::string out_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");
  const int compresslevel =
      options.has("compresslevel") ? static_cast<int>(options.get_int("compresslevel")) : 6;

  LineReader reader(in_name);
  Writer out(out_name, compresslevel);

  const bool per_cell = options.get_bool("per_cell");
  const std::string bc_sep = options.get_string("bc_sep");

  if (per_cell)
    out.write("cell\tgene\tcount\n");
  else
    out.write("gene\tcount\n");

  const auto method = parse_cluster_method(options.get_string("method"));
  if (!method) {
    error_exit("unknown --method: " + options.get_string("method"));
  }
  UMIClusterer processor(*method);
  const std::int64_t threshold = options.get_int("threshold");

  std::int64_t n_input = 0, n_output = 0;

  auto next_line = [&reader](std::string& line) { return reader.next(line); };
  auto bc_getter = [&](std::string_view read_id) {
    return per_cell ? get_cell_umi_read_string(read_id, bc_sep)
                    : get_umi_read_string(read_id, bc_sep);
  };

  get_gene_count_tab(
      next_line, bc_getter,
      [&](const std::string& gene, const PerCellUmiCounts& counts) {
        for (const auto& [cell, umi_counts] : counts) {
          for (const auto& [umi, c] : umi_counts) {
            (void)umi;
            n_input += c;
          }
          const auto groups = processor(umi_counts, threshold);
          const std::int64_t gene_count = static_cast<std::int64_t>(groups.size());
          if (per_cell) {
            out.write(cell + "\t" + gene + "\t" + std::to_string(gene_count) + "\n");
          } else {
            out.write(gene + "\t" + std::to_string(gene_count) + "\n");
            // Upstream increments nOutput only here — see note 3 above.
            n_output += gene_count;
          }
        }
      });

  (void)n_input;
  log.info("Number of reads counted: " + std::to_string(n_output));

  // U.Stop(): the footer at loglevel >= 1.
  if (log.loglevel() >= 1) log.write_raw(get_footer() + "\n");
  // Stop closes -E after the footer reaches the log; for the literal
  // `-E stderr` that close raises, so this throws HERE and not earlier.
  stop_close_error_file(options);
  // Utilities.Stop's --timeit block. `if global_options.timeit_file:` is a
  // TRUTHINESS test on a dest defaulting to None, and it runs regardless of
  // loglevel — the footer's `>= 1` guard does not apply to it.
  if (options.get_bool("timeit_file"))
    write_timeit(options.get_string("timeit_file"), options.get_string("timeit_name"),
                 options.get_bool("timeit_header"), program_path());
  out.close();
  log.close();
  return 0;
}

}  // namespace umi_tools
