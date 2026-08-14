// extract — port of umi_tools/extract.py::main.
//
// Transcribed from the Python (read and run). Structure:
//   validateExtractOptions -> (extract_cell, extract_umi)
//   ExtractFilterAndUpdate(...)
//   optional whitelist / blacklist loading
//   single-end loop  OR  joinedFastqIterate paired loop
//   log ReadExtractor.getReadCounts().most_common()
//
// Details that are contractual, each verified against the source:
//  * `--whitelist` implies `--filter-cell-barcode` (the option is deprecated and
//    logs an info line when passed explicitly).
//  * in the PAIRED loop, `--read2-stdout` writes read2 to stdout and read1
//    nowhere; otherwise read1 goes to stdout and read2 to --read2-out if given.
//  * a filtered read is written to --filtered-out (and read2 to --filtered-out2)
//    as the ORIGINAL record, before extraction.
//  * `--reconcile-pairs` sets strict=False on joinedFastqIterate.
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "umi_tools/extract_methods.hpp"
#include "umi_tools/fastq.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/pattern.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/tools.hpp"
#include "umi_tools/whitelist_methods.hpp"

namespace umi_tools {

std::string pattern_repr(const std::string& pattern, ExtractMethod method) {
  // An UNSET pattern is None in Python and renders as the four characters
  // "None"; the port used to render the empty string.
  if (pattern.empty()) return "None";
  // For --extract-method=regex the value has already been replaced by the
  // compiled object, so %s prints regex.Regex('...', flags=regex.V0).
  if (method == ExtractMethod::Regex)
    return "regex.Regex('" + pattern + "', flags=regex.V0)";
  return pattern;
}

// Utilities.validateExtractOptions — returns (extract_cell, extract_umi) and
// raises ValueError for every invalid combination. The messages are reproduced
// because several are user-visible.
std::pair<bool, bool> validate_extract_options(const Values& o, const std::string& pattern,
                                               const std::string& pattern2,
                                               ExtractMethod method) {
  const bool has_p = !pattern.empty();
  const bool has_p2 = !pattern2.empty();
  const bool has_read2_in = !o.is_none("read2_in");

  if (o.get_bool("read2_only")) {
    if (!has_p2)
      throw std::invalid_argument("Must supply --bc-pattern2 if extracting from just read2");
    if (has_p)
      throw std::invalid_argument("Don't supply --bc-pattern if extracting from just read2");
  }
  if (!has_p && !has_p2) {
    if (!has_read2_in)
      throw std::invalid_argument("Must supply --bc-pattern for single-end");
    throw std::invalid_argument(
        "Must supply --bc-pattern and/or --bc-pattern2 if paired-end ");
  }
  if (has_p2 && !has_read2_in)
    throw std::invalid_argument("must specify a paired fastq ``--read2-in``");

  const bool has_fo = !o.is_none("filtered_out");
  const bool has_fo2 = !o.is_none("filtered_out2");
  if (has_fo2 && !has_read2_in)
    throw std::invalid_argument(
        "Cannot use --filtered-out2 without read2 input (--read2-in)");
  if (((has_read2_in && has_fo) && !has_fo2) || (has_fo2 && !has_fo))
    throw std::invalid_argument(
        "Must supply both --filtered-out and --filtered-out2"
        "to write out filtered reads for paired end");

  bool extract_cell = false, extract_umi = false;

  // Utilities.py:1207-1222 compiles the regex(es) HERE, inside
  // validateExtractOptions, and converts a compile failure into the documented
  // ValueError. The port only constructed Pattern later, in the
  // ExtractFilterAndUpdate ctor, which moved the failure AFTER extract.main's
  // --either-read / --filter-umi blocks AND reported the internal parser text.
  // Measured: `--bc-pattern='(?P<umi_1>.{4}' --either-read` reported
  // the either-read error here where upstream reports the invalid regex.
  //
  // Note the two messages name DIFFERENT options, and upstream checks pattern
  // first, so a run with both malformed reports --bc-pattern.
  if (method == ExtractMethod::Regex) {
    if (!pattern.empty()) {
      try {
        Pattern probe(pattern);
        (void)probe;
      } catch (const std::exception&) {
        throw std::invalid_argument("--bc-pattern '" + pattern +
                                    "' is not a valid regex");
      }
    }
    if (!pattern2.empty()) {
      try {
        Pattern probe(pattern2);
        (void)probe;
      } catch (const std::exception&) {
        throw std::invalid_argument("--bc-pattern2 '" + pattern2 +
                                    "' is not a valid regex");
      }
    }
  }

  if (method == ExtractMethod::Regex) {
    // The Python inspects pattern.groupindex; the equivalent here is the group
    // names the pattern declares. A cheap textual scan for "(?P<cell_" /
    // "(?P<umi_" is exact for this grammar, which has no other way to name a
    // group.
    for (const std::string* p : {&pattern, &pattern2}) {
      if (p->find("(?P<cell_") != std::string::npos) extract_cell = true;
      if (p->find("(?P<umi_") != std::string::npos) extract_umi = true;
    }
  } else {
    for (const std::string* p : {&pattern, &pattern2}) {
      if (p->find('C') != std::string::npos) extract_cell = true;
      if (p->find('N') != std::string::npos) extract_umi = true;
    }
  }

  if (!extract_umi) {
    // these interpolated the RAW strings, so an unset pattern2 rendered
    // as nothing where Python's None renders as the four characters "None", and
    // under --extract-method=regex the value is already a compiled object whose
    // %s is regex.Regex('...', flags=regex.V0). `pattern_repr` does both and was
    // already used correctly by the --filter-umi copies of these same two
    // messages; only these missed it. MEASURED: `extract -p ACGT` printed
    // "...ACGT, " where upstream prints "...ACGT, None".
    if (method == ExtractMethod::String)
      throw std::invalid_argument(
          "barcode pattern(s) do not include any umi bases (marked with 'Ns') " +
          pattern_repr(pattern, method) + ", " + pattern_repr(pattern2, method));
    throw std::invalid_argument(
        "barcode regex(es) do not include any umi groups (starting with 'umi_') " +
        pattern_repr(pattern, method) + ", " + pattern_repr(pattern2, method));
  }
  return {extract_cell, extract_umi};
}

namespace {

}  // namespace

int tool_extract(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("extract");
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
    out.write("extract version: $Id$\n");
    return 0;
  }
  Values options = parsed.values;

  Log& log = Log::instance();
  const std::string log_name =
      options.is_none("stdlog") ? std::string() : options.get_string("stdlog");
  // stdlog == stdout exactly when neither is redirected (Utilities.Start).
  const bool log_is_stdout = (log_name.empty() || log_name == "-") &&
                             !options.get_bool("log2stderr");
  const bool out_is_stdout =
      options.is_none("stdout") || options.get_string("stdout") == "-";
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
    std::vector<std::string> full_argv{"extract"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  // Utilities.Start opens options.stdin with mode 'r' and
  // options.stdout with mode 'w' (Utilities.py:1112-1125) BEFORE extract.main's
  // body runs, so BOTH are open before any of main's validation. Constructing
  // them after validation, as this did, is observable twice over — MEASURED:
  //
  //   extract -p NNNN -S out.fq --filter-umi
  //     upstream leaves a ZERO-LENGTH out.fq behind; the port left no file.
  //   extract -p NNNN -I missing.fq --retain-umi
  //     upstream fails with FileNotFoundError on the INPUT; the port reported
  //     'option --retain-umi only works with --extract-method=regex' instead.
  //
  // The Writer's destructor runs while ExitRequest unwinds, which closes the
  // empty file rather than deleting it — the same artifact upstream leaves.
  const int compresslevel =
      options.has("compresslevel") ? static_cast<int>(options.get_int("compresslevel")) : 6;
  const std::string in_name =
      options.is_none("stdin") ? std::string("-") : options.get_string("stdin");
  const std::string out_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");

  LineReader in1(in_name);
  Writer out(out_name, compresslevel);

  const std::string pattern = options.is_none("pattern") ? "" : options.get_string("pattern");
  std::string pattern2 = options.is_none("pattern2") ? "" : options.get_string("pattern2");
  const ExtractMethod method = options.get_string("extract_method") == "regex"
                                   ? ExtractMethod::Regex
                                   : ExtractMethod::String;

  bool filter_cell_barcode = options.get_bool("filter_cell_barcode");
  if (filter_cell_barcode)
    log.info(
        "Use of --whitelist ensures cell barcodes are filtered. "
        "--filter-cell-barcode is no longer required and may be removed in "
        "future versions.");
  const bool has_whitelist = !options.is_none("whitelist");
  if (has_whitelist) filter_cell_barcode = true;

  if (options.get_bool("retain_umi") && method != ExtractMethod::Regex)
    error_exit("option --retain-umi only works with --extract-method=regex");
  if (!options.is_none("filtered_out") && method != ExtractMethod::Regex && !has_whitelist)
    error_exit(
        "Reads will not be filtered unless extract method is"
        "set to regex (--extract-method=regex) or cell"
        "barcodes are filtered (--whitelist)");

  const bool qft = !options.is_none("quality_filter_threshold") &&
                   options.get_int("quality_filter_threshold") != 0;
  const bool qfm = !options.is_none("quality_filter_mask") &&
                   options.get_int("quality_filter_mask") != 0;
  if ((qft || qfm) && options.is_none("quality_encoding"))
    error_exit(
        "must provide a quality encoding (--quality-"
        "encoding) to filter UMIs by quality (--quality"
        "-filter-threshold) or mask low quality bases "
        "with (--quality-filter-mask)");

  const auto [extract_cell, extract_umi] =
      validate_extract_options(options, pattern, pattern2, method);

  // ORDER IS OBSERVABLE. extract.py calls validateExtractOptions at :336, then
  // the --either-read block at :338-348, then --filter-umi at :350-367. The port
  // had these two the other way round, so a run violating BOTH reported the
  // --filter-umi message where upstream reports the --either-read one.
  const bool either_read = options.get_bool("either_read");
  if (either_read) {
    if (extract_cell)
      error_exit(
          "Option to extract from either read (--either-read) "
          "is not currently compatible with cell barcode extraction");
    if (method != ExtractMethod::Regex)
      error_exit(
          "Option to extract from either read (--either-read)"
          "requires --extract-method=regex");
    if (pattern.empty() || pattern2.empty())
      error_exit(
          "Option to extract from either read (--either-read)"
          "requires --bc-pattern=[PATTERN1] and"
          "--bc-pattern2=[PATTERN2]");
  }

  // extract.py:350-367 — absent entirely, so an unset --umi-whitelist fell
  // through to get_user_defined_barcodes("") and the user saw an open failure on
  // "" instead of the documented message. The concatenated (un-spaced) wording
  // is upstream's.
  if (options.get_bool("filter_umi")) {
    if (options.is_none("umi_whitelist"))
      error_exit("must provide a UMI whitelist (--umi-whitelist) if using "
                 "--filter-umi option");
    if (!pattern2.empty() && options.is_none("umi_whitelist_paired"))
      error_exit("must provide a UMI whitelist for paired end "
                 "(--umi-whitelist-paired) if using --filter-umi option"
                 "with paired end data");
    if (!extract_umi) {
      if (method == ExtractMethod::String)
        error_exit("barcode pattern(s) do not include any umi bases "
                   "(marked with 'Ns') " + pattern_repr(pattern, method) + ", " +
                   pattern_repr(pattern2, method));
      error_exit("barcode regex(es) do not include any umi groups "
                 "(starting with 'umi_') " + pattern_repr(pattern, method) + ", " +
                 pattern_repr(pattern2, method));
    }
  }

  if (has_whitelist && !extract_cell) {
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
  efo.quality_encoding =
      options.is_none("quality_encoding") ? "" : options.get_string("quality_encoding");
  if (qft) efo.quality_filter_threshold = options.get_int("quality_filter_threshold");
  if (qfm) efo.quality_filter_mask = options.get_int("quality_filter_mask");
  efo.filter_umi_barcode = options.get_bool("filter_umi");
  efo.filter_cell_barcode = filter_cell_barcode;
  efo.retain_umi = options.get_bool("retain_umi");
  efo.either_read = either_read;
  efo.either_read_resolve = options.is_none("either_read_resolve")
                                ? "discard"
                                : options.get_string("either_read_resolve");
  efo.umi_separator =
      options.is_none("umi_separator") ? "_" : options.get_string("umi_separator");

  ExtractFilterAndUpdate extractor(efo, pattern, pattern2);

  // --filter-umi: the UMI whitelist and its error-correction map. Note
  // deriveErrorCorrection=TRUE here (the cell path below uses
  // getErrorCorrection instead), and the map's values stay OPTIONAL: a None
  // means "within threshold of more than one whitelisted UMI", which
  // filterUMIBarcode counts under a DIFFERENT label from "not within threshold
  // of any". Collapsing them would merge two log counters.
  if (options.get_bool("filter_umi")) {
    const auto [umi_whitelist, umi_ftt] = get_user_defined_barcodes(
        options.get_string("umi_whitelist"),
        options.is_none("umi_whitelist_paired")
            ? std::string()
            : options.get_string("umi_whitelist_paired"),
        /*getErrorCorrection=*/false, /*deriveErrorCorrection=*/true,
        /*threshold=*/options.has("correct_umi_threshold")
            ? options.get_int("correct_umi_threshold") : 1);
    log.info("Length of whitelist: " + std::to_string(umi_whitelist.size()));
    log.info("Length of 'correctable' whitelist: " + std::to_string(umi_ftt.size()));
    extractor.set_umi_whitelist(umi_whitelist);
    extractor.set_umi_false_to_true_map(umi_ftt);
  }

  if (has_whitelist) {
    const auto [cell_whitelist, false_to_true] = get_user_defined_barcodes(
        options.get_string("whitelist"), "",
        /*getErrorCorrection=*/options.get_bool("error_correct_cell"),
        /*deriveErrorCorrection=*/false, /*threshold=*/1);
    extractor.set_cell_whitelist(cell_whitelist);
    std::map<std::string, std::string> ftt;
    for (const auto& [k, v] : false_to_true)
      if (v) ftt[k] = *v;
    extractor.set_false_to_true_map(ftt);
  }
  if (!options.is_none("blacklist")) {
    std::set<std::string> blacklist;
    LineReader br(options.get_string("blacklist"));
    std::string line;
    while (br.next(line)) {
      const std::size_t tab = line.find('\t');
      std::string first = tab == std::string::npos ? line : line.substr(0, tab);
      while (!first.empty() && std::isspace(static_cast<unsigned char>(first.back())))
        first.pop_back();
      blacklist.insert(first);
    }
    extractor.set_cell_blacklist(blacklist);
  }

  FastqIterator read1s(in1, options.get_bool("ignore_suffix"));

  std::unique_ptr<Writer> filtered_out, filtered_out2, read2_out;
  if (!options.is_none("filtered_out"))
    filtered_out = std::make_unique<Writer>(options.get_string("filtered_out"), compresslevel);
  if (!options.is_none("filtered_out2"))
    filtered_out2 = std::make_unique<Writer>(options.get_string("filtered_out2"), compresslevel);
  // `--read2-out` DEFAULTS TO PYTHON `False`, not None (extract.py:293), and
  // upstream tests it for TRUTH (`if options.read2_out:`, extract.py:471).
  // is_none() is false for `False`, so this branch always ran and opened a file
  // literally named "False" in the cwd on every extract that did not pass the
  // option. The FASTQ output was still correct, which is why 15 extract fixtures
  // never noticed — they compare the declared outputs, not the directory.
  // Fourth sighting of this trap: --expect-cells, --output-stats, --read2-out.
  //
  // Upstream opens read2_out ONLY inside the paired branch
  // (extract.py:471-472) but closes it UNCONDITIONALLY at :514, so single-end
  // plus --read2-out ends in
  //   UnboundLocalError: cannot access local variable 'read2_out'
  // AFTER every output record has been written — exit 1, and the read2 file is
  // never created. Opening it here regardless of --read2-in produced an EMPTY
  // read2 file and exit 0. MEASURED: oracle rc=1 with out.fastq at 530 bytes
  // and no r2 file; port rc=0 with the same 530 bytes and a 0-byte r2.
  //
  // --filtered-out2 has the same asymmetry in the source and is genuinely
  // unreachable: validateSamOptions rejects it without --read2-in first
  // (measured, both sides rc=1).
  const bool read2_out_given = options.get_bool("read2_out");
  if (read2_out_given && !options.is_none("read2_in"))
    read2_out = std::make_unique<Writer>(options.get_string("read2_out"), compresslevel);

  log.info("Starting barcode extraction");

  const bool has_reads_subset = // TRUTHINESS, not presence: --subset-reads=0 is falsy upstream, so NO
      // subsetting is applied and every read is emitted. Presence-gating made
      // the port stop after the first read and emit nothing.
      (!options.is_none("reads_subset") && options.get_int("reads_subset") != 0);
  const std::int64_t reads_subset = has_reads_subset ? options.get_int("reads_subset") : 0;
  const bool read2_stdout = options.get_bool("read2_stdout");
  std::int64_t prog_count = 0;

  if (options.is_none("read2_in")) {
    while (auto read = read1s.next()) {
      // extract.py increments progCount FIRST and logs on every 100,000th read,
      // in BOTH loops. Absent here, so any input of 100k+ reads produced a log
      // short one line per 100k. No fixture reaches 100k — the QUICK_START
      // tutorial (3.26M reads) is what surfaced it.
      ++prog_count;
      if (prog_count % 100000 == 0)
        log.info("Parsed " + std::to_string(prog_count) + " reads");
      auto new_read = extractor(*read);
      if (has_reads_subset && extractor.read_counts().get("Input Reads", 0) > reads_subset)
        break;
      if (!new_read) {
        if (filtered_out) filtered_out->write(read->str() + "\n");
        continue;
      }
      out.write(new_read->read1.str() + "\n");
    }
  } else {
    LineReader in2(options.get_string("read2_in"));
    FastqIterator read2s(in2, options.get_bool("ignore_suffix"));
    const bool strict = !options.get_bool("reconcile");

    joined_fastq_iterate(read1s, read2s, strict, [&](Record& r1, Record& r2) {
      // extract.py increments progCount FIRST and logs on every 100,000th read,
      // in BOTH loops. Absent here, so any input of 100k+ reads produced a log
      // short one line per 100k. No fixture reaches 100k — the QUICK_START
      // tutorial (3.26M reads) is what surfaced it.
      ++prog_count;
      if (prog_count % 100000 == 0)
        log.info("Parsed " + std::to_string(prog_count) + " reads");
      auto reads = extractor(r1, &r2);
      if (has_reads_subset && extractor.read_counts().get("Input Reads", 0) > reads_subset)
        return false;
      if (!reads) {
        if (filtered_out) filtered_out->write(r1.str() + "\n");
        if (filtered_out2) filtered_out2->write(r2.str() + "\n");
        return true;
      }
      if (read2_stdout) {
        out.write(reads->read2.str() + "\n");
      } else {
        out.write(reads->read1.str() + "\n");
        if (read2_out) read2_out->write(reads->read2.str() + "\n");
      }
      return true;
    });
  }

  // `if options.read2_out: read2_out.close()` — the guard tests the OPTION, not
  // the writer, so single-end reaches it with the name unbound.
  if (read2_out_given && !read2_out)
    throw std::invalid_argument(
        "cannot access local variable 'read2_out' where it is not associated "
        "with a value");
  if (read2_out) read2_out->close();
  if (filtered_out) filtered_out->close();
  if (filtered_out2) filtered_out2->close();

  for (const auto& [k, v] : extractor.get_read_counts_most_common())
    log.info(k + ": " + std::to_string(v));

  if (!options.is_none("umi_correct_log")) {
    Writer w(options.get_string("umi_correct_log"), compresslevel);
    w.write("umi\tcount_no_errors\tcount_errors\n");
    for (const auto& [umi, counts] : extractor.umi_whitelist_counts())
      w.write(umi + "\t" + std::to_string(counts.first) + "\t" +
              std::to_string(counts.second) + "\n");
    w.close();
  }

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
