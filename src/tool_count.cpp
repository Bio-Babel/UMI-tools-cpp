// count — port of umi_tools/count.py::main.
//
// `options.per_gene = True` is HARDCODED by count.py, so this tool only ever
// takes get_bundles' per-gene path.
//
// Output shapes, all three of which differ (01_audit.md D8):
//   no --per-cell             "gene\tcount"                        streamed
//   --per-cell (long)         "gene\tcell\tcount"                  sorted
//   --per-cell --wide-format  "gene\t<sorted cells...>"            sorted, 0-filled
//
// NOTE the column order: `count --per-cell` long form writes gene,cell,count
// while `count_tab --per-cell` writes cell,gene,count. Both are preserved as-is.
//
// Upstream writes the per-bundle rows to a TEMP FILE first and re-reads it to
// build the per-cell tables; that indirection is not observable, so this
// accumulates in memory for the per-cell paths and streams the non-per-cell one,
// which is the same output with the same ordering.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/tools.hpp"

namespace umi_tools {

int tool_count(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("count");
  if (spec == nullptr) return 1;

  // --no-usage only EXISTS when it is already in argv (Utilities.py:456).
  const MaybeNoUsage no_usage(*spec, argv);
  spec = &no_usage.spec();

  if (argv.empty()) {
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
    out.write("count version: $Id$\n");
    return 0;
  }
  const Values& options = parsed.values;

  Log& log = Log::instance();
  const std::string log_name =
      options.is_none("stdlog") ? std::string() : options.get_string("stdlog");
  const bool log_is_stdout =
      (log_name.empty() || log_name == "-") && !options.get_bool("log2stderr");
  const bool out_is_stdout =
      options.is_none("stdout") || options.get_string("stdout") == "-";
  // stdout is opened FIRST, before -E and before the log.
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
  if (log.loglevel() >= 1) {
    std::vector<std::string> full_argv{"count"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  // count.py:99 calls the FULL U.validateSamOptions(options, group=False). This
  // used to be two hand-rolled checks, which meant count never raised for
  // --gene-transcript-map without --per-contig, --per-cell + tag without
  // --cell-tag, --umi-tag/--cell-tag without --extract-umi-method=tag,
  // --unmapped-reads=use / --chimeric-pairs / --unpaired-reads / --ignore-tlen
  // without --paired — and dropped the legacy --output-unmapped handling, the
  // three paired WARNING lines and the `command:` info line. It also used
  // error_exit where dedup and group use raise_value_error, so the same upstream
  // ValueError produced different stderr depending on the subcommand.
  std::string command = "count";
  for (const std::string& a : argv) command += " " + a;
  SamOptionsResult sam = validate_sam_options(options, /*group=*/false, command,
                                             /*per_gene_override=*/true);
  if (sam.error) { raise_value_error(*sam.error); return 1; }

  // Utilities.Start seeds the STDLIB stream for every tool with `is not None`;
  // count.py then seeds NUMPY separately on truthiness. Only the stdlib one
  // matters here — it is what get_bundles' --subset draws from — and count used
  // to seed neither, so `count --subset` was reproducible only by accident.
  if (!options.is_none("random_seed"))
    global_random().seed(options.get_int("random_seed"));
  if (options.get_int_truthy("random_seed")) {
    // np.random.seed(...) — count's numpy RNG is never drawn from.
  }

  // `if options.stdin != sys.stdin` — did argv REPLACE the default stream?
  // is_none() answered a different question and was always false here, so this
  // guard never fired: the only way to reach it was a non-empty argv without
  // --stdin (`dedup --paired`), and that fell through to htslib's
  // "cannot read header: -" instead of upstream's message.
  if (!options.was_given("stdin"))
    throw std::invalid_argument("Input on standard in not currently supported");

  // The shared builder, not a hand-rolled subset. The hand-rolled one omitted
  // chrom, paired, unpaired_reads, unmapped_reads, chimeric_pairs, ignore_tlen
  // and whole_contig — all real options in count's own table, and all consumed
  // by the SHARED read triage in get_bundles, not by the positional branch only.
  // `count --chrom=chr19` counted the whole file; `count --paired` skipped the
  // pair triage entirely, changing every gene count.
  BundleOptions bo = make_bundle_options(options, sam);
  bo.per_gene = true;                       // hardcoded by count.py

  const std::string in_name = options.get_string("stdin");
  const std::string out_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");
  const int compresslevel =
      options.has("compresslevel") ? static_cast<int>(options.get_int("compresslevel")) : 6;
  const std::string reference =
      options.is_none("reference_filename") ? "" : options.get_string("reference_filename");

  // --input-options was parsed and dropped; it reaches htslib now.
  const std::string in_format_options =
      options.is_none("input_options") ? std::string() : options.get_string("input_options");
  // count.py:112 — see the note in tool_dedup.cpp: consulted only by pysam's
  // header error messages, which interpolate the mode.
  const std::string in_mode = input_mode_for_format(determine_format(
      in_name, options.get_bool("in_sam"),
      options.is_none("in_format") ? "" : options.get_string("in_format")));
  AlignmentReader reader(in_name, reference, in_format_options, in_mode);

  // count.py:124-134 — all three read branches are index-backed fetches, so an
  // unindexed BAM exits 1 upstream before the first record. See the longer note
  // in tool_dedup.cpp: the port's linear-pass-with-filter model never opened the
  // index, and every shipped fixture ships a .bai.
  reader.require_index();

  // getMetaContig2contig + metafetcher. THREE details, each measured against the
  // tool's own Python rather than shared:
  //   * the block sits inside the `else` of `if options.chrom:` — with --chrom
  //     set, upstream ignores the map entirely and does a plain region fetch.
  //     Building it regardless made the port read every contig the map names,
  //     including the ones --chrom excluded.
  //   * the guard differs per tool: count `if map`, group `if per_gene and map`,
  //     dedup `if per_contig and map`.
  //   * the wiring is UNCONDITIONAL once the branch is taken. Gating it on a
  //     non-empty map (which the port used to do) meant a map naming no BAM
  //     reference fell back to a full linear pass emitting every read, where
  //     upstream's metafetcher yields NOTHING and the output is empty.
  MetaContigMap metacontig;
  if (options.is_none("chrom") && !options.is_none("gene_transcript_map")) {
    metacontig = get_meta_contig_to_contig(reader, options.get_string("gene_transcript_map"));
    bo.metacontig = &metacontig;
    bo.gene_tag = "MC";
  }
  Writer out(out_name, compresslevel);

  const auto method = parse_cluster_method(options.get_string("method"));
  if (!method) error_exit("unknown --method: " + options.get_string("method"));
  UMIClusterer processor(*method);
  const std::int64_t threshold = options.get_int("threshold");
  const bool per_cell = bo.per_cell;
  const bool wide = options.get_bool("wide_format_cell_counts");

  std::int64_t n_input = 0, n_output = 0, input_reads = 0;
  BundleReadEvents events;

  // count.py writes every per-bundle row to a TEMP FILE during the
  // loop (count.py:116-117, 169) and only afterwards writes "gene\tcount\n"
  // plus the copied rows to options.stdout (:216-220). The indirection is
  // OBSERVABLE whenever stdout and the log are the same stream, which is the
  // DEFAULT — Start leaves stdlog = stdout. Any log record produced inside the
  // bundle loop lands BEFORE the header upstream and AFTER it when the rows are
  // streamed. MEASURED on chr19_tag_missing.bam with the default log:
  //   oracle  ... WARNING At least one read is missing UMI and/or cell tag(s)
  //           gene<TAB>count            <- header at line 59
  //   port    gene<TAB>count            <- header at line 58
  //           ... WARNING At least one read is missing ...
  //
  // A temp file rather than an in-memory vector, because that is what upstream
  // does and because the row count scales with the gene count (see sort_output for
  // the memory shape this port already has once).
  std::optional<std::string> rows_tmp;
  std::optional<Writer> rows_out;
  if (!per_cell) {
    rows_tmp = get_temp_filename(options.is_none("tmpdir") ? ""
                                                           : options.get_string("tmpdir"));
    rows_out.emplace(*rows_tmp, compresslevel);
  }
  struct RowsCleanup {
    const std::optional<std::string>* path;
    ~RowsCleanup() { if (path->has_value()) std::remove((*path)->c_str()); }
  } rows_cleanup{&rows_tmp};

  // For the per-cell paths: gene -> cell -> count, plus the gene and cell sets.
  std::map<std::string, std::map<std::string, std::int64_t>> gene_counts;
  std::set<std::string> genes, cells;

  for_each_bundle_per_gene(
      reader, bo, /*only_count_reads=*/true, events,
      [&](const Bundle& bundle, const BundleKey& key) {
        UmiCounts counts;
        for (const auto& [umi, entry] : bundle) counts[umi] = entry.count;
        for (const auto& [umi, entry] : bundle) {
          (void)umi;
          n_input += entry.count;
        }
        while (n_input >= input_reads + 1000000) {
          input_reads += 1000000;
          log.info("Parsed " + std::to_string(input_reads) + " input reads");
        }

        const auto groups = processor(counts, threshold);
        const std::int64_t gene_count = static_cast<std::int64_t>(groups.size());
        const std::string& gene = key.pos;

        if (per_cell) {
          const std::string cell = key.cell ? *key.cell : std::string();
          gene_counts[gene][cell] = gene_count;
          genes.insert(gene);
          cells.insert(cell);
        } else {
          rows_out->write(gene + "\t" + std::to_string(gene_count) + "\n");
        }
        n_output += gene_count;
      },
      options.is_none("subset") ? std::nullopt
                                : std::optional<double>(options.get_float("subset")));

  if (per_cell) {
    if (wide) {
      // `"%s\t%s\n" % ("gene", "\t".join(sorted(cells)))` — the tab is part of
      // the FORMAT, so an empty cell set still yields "gene\t".
      std::string header = "gene\t";
      bool first = true;
      for (const std::string& c : cells) { if (!first) header += "\t"; header += c; first = false; }
      out.write(header + "\n");
      for (const std::string& g : genes) {
        std::string row = g;
        for (const std::string& c : cells) {
          auto it = gene_counts[g].find(c);
          // A missing cell is the integer 0 upstream (`counts.append(0)`).
          row += "\t" + (it == gene_counts[g].end() ? std::string("0")
                                                    : std::to_string(it->second));
        }
        out.write(row + "\n");
      }
    } else {
      out.write("gene\tcell\tcount\n");
      for (const std::string& g : genes)
        for (const auto& [c, n] : gene_counts[g])   // std::map == sorted(cells)
          out.write(g + "\t" + c + "\t" + std::to_string(n) + "\n");
    }
  } else {
    // The header and then the copied rows, exactly as count.py:216-220.
    rows_out->close();
    out.write("gene\tcount\n");
    LineReader rows_in(*rows_tmp);
    std::string line;
    while (rows_in.next(line)) out.write(line + "\n");
  }
  // The non-per-cell path streamed its header and rows already.

  // Upstream's read-events summary, in Counter.most_common() order.
  std::vector<std::pair<std::string, std::int64_t>> ev;
  for (const auto& [k, n] : events.counts) ev.emplace_back(k, n);
  std::stable_sort(ev.begin(), ev.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  for (const auto& [k, n] : ev) log.info(k + ": " + std::to_string(n));

  log.info("Number of (post deduplication) reads counted: " + std::to_string(n_output));

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
