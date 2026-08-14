// whitelist — port of umi_tools/whitelist.py::main.
//
// Output is a 4-column TSV, written for `sorted(list(cell_whitelist))`:
//     barcode \t corrected_barcodes \t count \t corrected_counts
// where the corrected columns are comma-joined `sorted(true_to_false_map[bc])`
// and their counts, or EMPTY STRINGS when there is no map at all.
//
// Contractual details verified against the source:
//  * `--method=umis` counts DISTINCT UMIs per cell instead of reads.
//  * `true_to_false_map` is a defaultdict(set), so indexing a whitelisted
//    barcode with no corrections INSERTS an empty set and yields "" — not a
//    KeyError. Reproduced with a lookup that defaults to empty.
//  * `if true_to_false_map:` is a truthiness test on the whole dict: when error
//    correction was not run at all, BOTH corrected columns are "".
//  * `--subset-reads` breaks on n_cell_barcodes for single-end but on n_reads
//    for paired-end. That asymmetry is upstream's, and it is reproduced.
//  * when no local minimum is accepted, the long "No local minima was accepted"
//    message goes through U.info under --allow-threshold-error and through
//    U.error (log and EXIT) otherwise.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "umi_tools/extract_methods.hpp"
#include "umi_tools/fastq.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/knee.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/tools.hpp"

namespace umi_tools {

int tool_whitelist(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("whitelist");
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
    out.write("whitelist version: $Id$\n");
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
  if (log.loglevel() >= 1) {
    std::vector<std::string> full_argv{"whitelist"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  const std::string pattern =
      options.is_none("pattern") ? "" : options.get_string("pattern");
  const std::string pattern2 =
      options.is_none("pattern2") ? "" : options.get_string("pattern2");
  const ExtractMethod method = options.get_string("extract_method") == "regex"
                                   ? ExtractMethod::Regex
                                   : ExtractMethod::String;

  if (options.get_bool("filtered_out") && method != ExtractMethod::Regex)
    error_exit(
        "Reads will not be filtered unless extract method is"
        "set to regex (--extract-method=regex)");

  // TRUTHINESS, not is_none: --expect-cells and --cell-number default to the
  // Python literal False (not None), and every guard in whitelist.py is a
  // truthiness test (`if options.expect_cells:`). Using is_none() here made all
  // nine data fixtures abort with "Cannot use --expect-cells with 'distance'"
  // because "False" is not None.
  const bool has_expect = options.get_int_truthy("expect_cells");
  const bool has_cell_number = options.get_int_truthy("cell_number");
  const std::string knee_method = options.get_string("knee_method");
  if (has_expect) {
    if (knee_method == "distance")
      error_exit(
          "Cannot use --expect-cells with 'distance' knee "
          "method. Switch to --knee-method=density if you want to "
          "provide an expectation for the number of "
          "cells. Alternatively, if you know the number of cell "
          "barcodes, use --cell-number");
    if (has_cell_number)
      error_exit("Cannot supply both --expect-cells and --cell-number options");
  }

  // whitelist.py:346 calls the FULL U.validateExtractOptions. This used to be an
  // inline extract_cell sniff, which skipped every one of its checks:
  // --read2-only without --bc-pattern2 / with --bc-pattern, "Must supply
  // --bc-pattern for single-end", --bc-pattern2 without --read2-in,
  // --filtered-out2 without read2 input, and "Must supply both --filtered-out
  // and --filtered-out2". `whitelist --bc-pattern=CCCCNNNN --filtered-out2=f2.fq`
  // exited 0 having written nothing where upstream raises.
  const auto [extract_cell, extract_umi] =
      validate_extract_options(options, pattern, pattern2, method);
  (void)extract_umi;
  if (!extract_cell) {
    if (method == ExtractMethod::String)
      error_exit("barcode pattern(s) do not include any cell bases (marked with 'Cs') " +
                 pattern_repr(pattern, method) + ", " + pattern_repr(pattern2, method));
    error_exit("barcode regex(es) do not include any cell groups (starting with 'cell_') " +
               pattern_repr(pattern, method) + ", " + pattern_repr(pattern2, method));
  }

  ExtractFilterOptions efo;
  efo.method = method;
  efo.prime3 = options.get_bool("prime3");
  efo.extract_cell = extract_cell;
  ExtractFilterAndUpdate extractor(efo, pattern, pattern2);

  const int compresslevel =
      options.has("compresslevel") ? static_cast<int>(options.get_int("compresslevel")) : 6;
  const std::string in_name =
      options.is_none("stdin") ? std::string("-") : options.get_string("stdin");
  const std::string out_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");

  LineReader in1(in_name);
  Writer out(out_name, compresslevel);
  FastqIterator read1s(in1, /*remove_suffix=*/false);

  std::unique_ptr<Writer> filtered_out, filtered_out2;
  if (options.get_bool("filtered_out"))
    filtered_out = std::make_unique<Writer>(options.get_string("filtered_out"), compresslevel);
  if (options.get_bool("filtered_out2"))
    filtered_out2 = std::make_unique<Writer>(options.get_string("filtered_out2"), compresslevel);

  CellBarcodeCounts cell_barcode_counts;
  OrderedMap<std::string, std::set<std::string>> cell_barcode_umis;
  const bool umis_method = options.get_string("method") == "umis";

  std::int64_t n_reads = 0, n_cell_barcodes = 0;
  // `if options.subset_reads:` is truthiness and the DEFAULT IS 100000000, so
  // upstream always takes this branch — just with a limit no fixture reaches.
  const bool has_subset = options.get_int_truthy("subset_reads");
  const std::int64_t subset_reads = has_subset ? options.get_int("subset_reads") : 0;

  log.info("Starting barcode extraction");

  auto observe = [&](const std::string& cell, const std::string& umi) {
    if (umis_method)
      cell_barcode_umis[cell].insert(umi);
    else
      cell_barcode_counts[cell] += 1;
    ++n_cell_barcodes;
  };

  if (options.is_none("read2_in")) {
    while (auto read1 = read1s.next()) {
      // whitelist.py logs BEFORE the increment, so a non-empty input always
      // produces a "Parsed 0 reads" record and a 250k-read input produces three.
      // Both loops had no progress logging at all.
      if (n_reads % 100000 == 0)
        log.info("Parsed " + std::to_string(n_reads) + " reads");
      ++n_reads;
      auto bc = extractor.get_barcodes_for_whitelist(*read1, nullptr);
      if (!bc) {
        if (filtered_out) filtered_out->write(read1->str() + "\n");
        continue;
      }
      observe(bc->first, bc->second);
      // Single-end breaks on n_cell_barcodes.
      if (has_subset && n_cell_barcodes > subset_reads) break;
    }
  } else {
    LineReader in2(options.get_string("read2_in"));
    FastqIterator read2s(in2, /*remove_suffix=*/false);
    // Python uses izip here, NOT joinedFastqIterate: no id check, and it stops
    // at the shorter file.
    while (true) {
      // `for read1, read2 in izip(read1s, read2s)` advances BOTH
      // iterators FIRST and runs the body only when both yielded, so the
      // progress record never fires on the terminating iteration. Logging
      // before the advance emitted one EXTRA record at every exact multiple of
      // 100000 — including zero. MEASURED on an EMPTY paired input: the oracle
      // logs no "Parsed" line at all and the port logged "Parsed 0 reads".
      //
      // The single-end loop above already has the right shape, which is why the
      // two disagreed with each other.
      auto read1 = read1s.next();
      if (!read1) break;
      auto read2 = read2s.next();
      if (!read2) break;
      if (n_reads % 100000 == 0)
        log.info("Parsed " + std::to_string(n_reads) + " reads");
      ++n_reads;
      auto bc = extractor.get_barcodes_for_whitelist(*read1, &*read2);
      if (!bc) {
        if (filtered_out) filtered_out->write(read1->str() + "\n");
        if (filtered_out2) filtered_out2->write(read2->str() + "\n");
        continue;
      }
      observe(bc->first, bc->second);
      // Paired-end breaks on n_reads — upstream's asymmetry, reproduced.
      if (has_subset && n_reads > subset_reads) break;
    }
  }

  log.info("Starting - whitelist determination");

  if (umis_method)
    for (const auto& [cell, umis] : cell_barcode_umis)
      cell_barcode_counts[cell] = static_cast<std::int64_t>(umis.size());

  // The message ends with the THREE numbers a user needs to pick a
  // valid value (whitelist.py:448-456), and the port dropped all of them:
  //   "... all cells. %s cell barcodes observed from %s parsed reads.
  //    Expected>= %s cell barcodes"  %  (len(counts), subset_reads, cell_number)
  // `Expected>=` has no space before the operator upstream; reproduced verbatim.
  // MEASURED: with --set-cell-number=999 on a 20-barcode input the oracle says
  // "20 cell barcodes observed from 100000000 parsed reads. Expected>= 999",
  // where 100000000 is --subset-reads' DEFAULT rendered literally, not None.
  //
  // It also used a bare throw where this exact `raise ValueError` maps to
  // raise_value_error, which throws ExitRequest so the writers unwind first.
  if (has_cell_number &&
      options.get_int("cell_number") > static_cast<std::int64_t>(cell_barcode_counts.size()))
    raise_value_error(
        "--set-cell-barcode option specifies more cell barcodes than the number of "
        "observed cell barcodes. This may be because --subset-reads was set to a value "
        "too low to capture reads from all cells. " +
        std::to_string(cell_barcode_counts.size()) + " cell barcodes observed from " +
        options.get_string("subset_reads") + " parsed reads. Expected>= " +
        options.get_string("cell_number") + " cell barcodes");

  CellWhitelist cw = get_cell_whitelist(
      cell_barcode_counts, knee_method,
      has_expect ? std::optional<std::int64_t>(options.get_int("expect_cells")) : std::nullopt,
      has_cell_number ? std::optional<std::int64_t>(options.get_int("cell_number"))
                      : std::nullopt,
      options.get_int("error_correct_threshold"));

  if (cw.whitelist && !cw.whitelist->empty())
    log.info("Top " + std::to_string(cw.whitelist->size()) +
             " cell barcodes passed the selected threshold");

  // Upstream writes <prefix>_cell_thresholds.tsv INSIDE
  // getKneeEstimateDensity (whitelist_methods.py:239-248), so it exists by the
  // time main decides what to do — including when main then calls
  // U.error("No local minima was accepted") and exits 1. This block used to sit
  // AFTER that error_exit, which throws, so the file was never created on the
  // one path whose docstring recommends it ("the counts per local minima
  // (requires --plot-prefix) ... then re-run with a manually selected
  // threshold"). MEASURED with --knee-method=density --plot-prefix=P on a
  // two-level count distribution: oracle rc=1 leaving P_cell_thresholds.tsv,
  // port rc=1 leaving nothing.
  //
  // Moving it here also puts it before --ed-above-threshold can replace
  // cw.whitelist, which is what the "Selected" marking already depended
  // on.
  // --plot-prefix: the PNGs are a recorded deviation (no plotting dependency);
  // the companion TSV is data and IS produced. Only the density method writes a
  // count/action table; the distance method writes the single knee index.
  if (options.get_bool("plot_prefix") && !has_cell_number) {
    const std::string prefix = options.get_string("plot_prefix");
    Writer tsv(prefix + "_cell_thresholds.tsv", compresslevel);
    if (knee_method == "density") {
      tsv.write("count\taction\n");
      for (std::int64_t c : cw.local_mins_counts) {
        // `local_min and local_mins_count == len(final_barcodes)`, where
        // final_barcodes is the knee's OWN list — not cw.whitelist, which
        // --ed-above-threshold replaced above. Measured: the oracle
        // marks 414 "Selected" on
        //   whitelist --knee-method=density --plot-prefix=knee
        //             --ed-above-threshold=discard --error-correct-threshold=3
        // over indrop.fastq.1.gz, where this marked every row "Rejected".
        const bool selected = cw.selected_local_min != -1 &&
                              cw.whitelist_size_at_knee >= 0 &&
                              c == cw.whitelist_size_at_knee;
        tsv.write(std::to_string(c) + "\t" + (selected ? "Selected" : "Rejected") + "\n");
      }
    } else {
      tsv.write("count\n");
      tsv.write(std::to_string(cw.idx_of_best_point) + "\n");
    }
    tsv.close();
  }


  // Upstream guards on `if options.ed_above_threshold:` ALONE
  // (whitelist.py:470) and passes cell_whitelist straight through, so when the
  // knee accepted no threshold it hands None to errorDetectAboveThreshold, where
  // `cell_whitelist = list(cell_whitelist)` raises
  //   TypeError: 'NoneType' object is not iterable
  // and the run exits 1. The extra `&& cw.whitelist` here fell through to the
  // write block instead and exited 0 having written an EMPTY whitelist.
  //
  // MEASURED, --knee-method=density --allow-threshold-error on a two-level count
  // distribution (40 barcodes at 2 reads, 40 at 3) which reaches "No local
  // minima was accepted" on both sides:
  //   --ed-above-threshold=discard  oracle rc=1 TypeError, port rc=0
  //   --ed-above-threshold=correct  oracle rc=1 TypeError, port rc=0
  //   (neither flag)                both rc=0 with an empty whitelist — the
  //                                 control, which must stay that way.
  if (options.get_bool("ed_above_threshold") && !cw.whitelist)
    throw std::invalid_argument("'NoneType' object is not iterable");
  if (options.get_bool("ed_above_threshold") && cw.whitelist) {
    const auto ed = error_detect_above_threshold(
        cell_barcode_counts, *cw.whitelist, cw.true_to_false_map,
        options.get_int("error_correct_threshold"),
        options.get_string("ed_above_threshold"));
    cw.whitelist = ed.cell_whitelist;
    cw.true_to_false_map = ed.true_to_false_map;
    cw.has_true_to_false = !ed.true_to_false_map.empty();
  }

  std::int64_t total_correct_barcodes = 0, total_corrected_barcodes = 0;
  if (cw.whitelist && !cw.whitelist->empty()) {
    log.info("Writing out whitelist");
    // `for barcode in sorted(list(cell_whitelist))` — the OUTPUT is sorted even
    // though the whitelist itself is order-carrying.
    std::vector<std::string> out_order = *cw.whitelist;
    std::sort(out_order.begin(), out_order.end());
    for (const std::string& barcode : out_order) {
      total_correct_barcodes += cell_barcode_counts.get(barcode, 0);

      std::string corrected_barcodes, corrected_barcode_counts;
      if (!cw.true_to_false_map.empty()) {
        // defaultdict(set): a missing key yields an empty set, not a KeyError.
        auto it = cw.true_to_false_map.find(barcode);
        if (it != cw.true_to_false_map.end()) {
          bool first = true;
          for (const std::string& fb : it->second) {   // std::set == sorted()
            if (!first) {
              corrected_barcodes += ',';
              corrected_barcode_counts += ',';
            }
            first = false;
            corrected_barcodes += fb;
            const std::int64_t c = cell_barcode_counts.get(fb, 0);
            corrected_barcode_counts += std::to_string(c);
            total_corrected_barcodes += c;
          }
        }
      }
      out.write(barcode + "\t" + corrected_barcodes + "\t" +
                std::to_string(cell_barcode_counts.get(barcode, 0)) + "\t" +
                corrected_barcode_counts + "\n");
    }
  } else {
    const std::string msg =
        "No local minima was accepted. Recommend checking the plot "
        "output and counts per local minima (requires `--plot-prefix`"
        "option) and then re-running with manually selected threshold "
        "(`--set-cell-number` option)";
    if (options.get_bool("allow_threshold_error"))
      log.info(msg);
    else
      error_exit(msg);   // U.error logs AND exits
  }

  log.info("Parsed " + std::to_string(n_reads) + " reads");
  log.info(std::to_string(n_cell_barcodes) + " reads matched the barcode pattern");
  log.info("Found " + std::to_string(cell_barcode_counts.size()) + " unique cell barcodes");
  if (cw.whitelist && !cw.whitelist->empty()) {
    log.info("Found " + std::to_string(total_correct_barcodes) +
             " total reads matching the selected cell barcodes");
    log.info("Found " + std::to_string(total_corrected_barcodes) +
             " total reads which can be error corrected to the selected cell barcodes");
  }


  if (filtered_out) filtered_out->close();
  if (filtered_out2) filtered_out2->close();

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
