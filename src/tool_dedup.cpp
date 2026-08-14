// dedup — port of umi_tools/dedup.py::main.
//
// Unlike `group`, dedup calls get_bundles with ALL DEFAULTS: all_reads=False, so
// each bundle keeps ONE "best" read per UMI. That is the branch of update_dicts
// with the MAPQ ladder and the RESERVOIR TIE-BREAK, which means dedup is the
// first subcommand whose output depends on the Python `random` stream's CALL
// ORDER (--subset draws from the same stream, interleaved per read).
//
// `inreads = infile.fetch()` — until_eof defaults to False here, so unplaced
// (tid == -1) records are excluded; see L22 for the measurement behind that.
#include <algorithm>
#include <cstdint>
#include <string>
#include <set>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/dedup_stats.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/tools.hpp"
#include "umi_tools/whitelist_methods.hpp"

namespace umi_tools {

int tool_dedup(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("dedup");
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
    out.write("dedup version: $Id$\n");
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
    std::vector<std::string> full_argv{"dedup"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  // `sys.argv` after the dispatcher's `del sys.argv[0]`. group=FALSE here, which
  // is what makes --unmapped-reads=output and friends an error for dedup.
  std::string command = "dedup";
  for (const std::string& a : argv) command += " " + a;
  SamOptionsResult sam = validate_sam_options(options, /*group=*/false, command);
  if (sam.error) { raise_value_error(*sam.error); return 1; }

  // `if options.random_seed:` seeds NUMPY here — truthiness, so 0 does not seed.
  // Utilities.Start already seeded the stdlib stream with `is not None`; that is
  // the one get_bundles draws from, and the one that matters for parity.
  PyRandom& rng = global_random();
  if (!options.is_none("random_seed")) rng.seed(options.get_int("random_seed"));

  // `if options.stdin != sys.stdin` — did argv REPLACE the default stream?
  // is_none() answered a different question and was always false here, so this
  // guard never fired: the only way to reach it was a non-empty argv without
  // --stdin (`dedup --paired`), and that fell through to htslib's
  // "cannot read header: -" instead of upstream's message.
  if (!options.was_given("stdin"))
    throw std::invalid_argument("Input on standard in not currently supported");
  const std::string in_name = options.get_string("stdin");

  // `--output-stats` DEFAULTS TO PYTHON `False`, not None, and every use of it
  // is a truthiness test (`if options.stats:`). is_none() is false for `False`,
  // so testing that instead stringifies the default to "False" and makes every
  // --ignore-umi run abort. Same trap as --expect-cells in slice 4.
  const bool want_stats = options.get_bool("stats");
  const std::string stats_prefix = want_stats ? options.get_string("stats") : std::string();
  const bool ignore_umi = options.get_bool("ignore_umi");
  if (want_stats && ignore_umi)
    raise_value_error("'--output-stats' and '--ignore-umi' options cannot be used together");

  const std::string reference =
      options.is_none("reference_filename") ? "" : options.get_string("reference_filename");

  // Utilities.output_names_and_formats — unconditional for dedup (there is no
  // `if options.output_bam` guard as in group.py: dedup always writes reads).
  const std::string eventual_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");
  const bool no_sort = options.get_bool("no_sort_output");
  const std::string eventual_format = determine_format(
      eventual_name, options.get_bool("out_sam"),
      options.is_none("out_format") ? "" : options.get_string("out_format"));
  std::string out_name, out_format, sorted_out_name, sorted_out_format;
  if (!no_sort) {
    sorted_out_format = eventual_format;
    sorted_out_name = eventual_name;
    out_name = get_temp_filename(
        options.is_none("tmpdir") ? "" : options.get_string("tmpdir"));
    out_format = eventual_format == "sam" ? "sam" : "bam";
  } else {
    out_format = eventual_format;
    out_name = eventual_name;
  }

  // --input-options was parsed and dropped; it reaches htslib now.
  const std::string in_format_options =
      options.is_none("input_options") ? std::string() : options.get_string("input_options");
  // dedup.py:219 — the INPUT format, from --in-sam / --in-format / the
  // extension. It selects no parser (htslib sniffs) and exists only so a
  // malformed input reports pysam's message with pysam's mode in it.
  const std::string in_mode = input_mode_for_format(determine_format(
      in_name, options.get_bool("in_sam"),
      options.is_none("in_format") ? "" : options.get_string("in_format")));
  AlignmentReader reader(in_name, reference, in_format_options, in_mode);
  // U.open_output_alignments logs this at DEBUG. Invisible at the default
  // verbosity, which is why every fixture missed it.
  log.debug("Opening " + out_name + ", format " + out_format +
            ", for output with options " +
            (options.is_none("output_options") ? std::string("None")
                                               : options.get_string("output_options")));
  const std::string format_options =
      options.is_none("output_options") ? std::string() : options.get_string("output_options");

  const bool paired = options.get_bool("paired");

  std::optional<AlignmentWriter> outfile;
  outfile.emplace(out_name, out_format, reader.header(), reference, format_options);

  // `if options.paired: outfile = TwoPassPairWriter(infile, outfile)` — the
  // writer WRAPS the alignment file and takes over closing it.
  std::optional<TwoPassPairWriter> pair_writer;
  if (paired) pair_writer.emplace(in_name, reference, *outfile);
  auto emit = [&](const BamRecord& r) {
    if (pair_writer) pair_writer->write(r);
    else outfile->write(r);
  };

  std::int64_t n_input = 0, n_output = 0, input_reads = 0, output_reads = 0;

  // detect_bam_features: only reached with --multimapping-detection-method.
  if (!options.is_none("detection_method")) {
    const std::string method = options.get_string("detection_method");
    const auto features = detect_bam_features(in_name);
    if (features.at(method) == 0) {
      std::int64_t total = 0;
      for (const auto& [k, v] : features) { (void)k; total += v; }
      if (total == 0) {
        raise_value_error("There are no bam tags available to detect multimapping. "
                   "Do not set --multimapping-detection-method");
      } else {
        std::string avail;
        for (const auto& [k, v] : features)
          if (v) { if (!avail.empty()) avail += ","; avail += k; }
        raise_value_error("The chosen method of detection for multimapping (" + method +
                   ") will not work with this bam. Multimapping can be detected"
                   " for this bam using any of the following: " + avail);
      }
      return 1;
    }
  }

  BundleOptions bo = make_bundle_options(options, sam);

  // dedup.py:263-268. This was MISSING entirely: `dedup --per-gene --per-contig
  // --gene-transcript-map=...` silently degraded to a linear pass with no MC
  // tag, bundles keyed by transcript and the non-metacontig flush rule — a
  // different set of bundles, different dedup groups, different output order.
  // count and group both wired it; only dedup did not.
  //
  // dedup's guard is `per_contig and map` (not per_gene, as group uses), and the
  // whole block is inside the `else` of `if options.chrom:`.
  // dedup.py:260-271 — EVERY branch of the read path is an index-backed fetch:
  // `fetch(reference=chrom)`, `metafetcher` (which is `fetch(contig)` per
  // contig), and the bare `fetch()`. pysam refuses all three without an index,
  // so `dedup` on an unindexed BAM exits 1 upstream before reading a record.
  //
  // The port models those iterators as a LINEAR PASS WITH A FILTER — measured
  // equivalent on the record stream, and deliberate (sam_methods_pos.cpp:233) —
  // which means it never opens the index and never noticed its absence. It read
  // the file happily and exited 0. Every shipped fixture ships a .bai, so no
  // fixture could show this; it took a copy made WITHOUT the .bai.
  //
  // `detect_bam_features` (dedup.py:145) is the one read that does not: it opens
  // the file by name with `until_eof=True`. It is a separate handle upstream, so
  // it does not gate this one.
  reader.require_index();

  MetaContigMap metacontig;
  if (options.is_none("chrom") && options.get_bool("per_contig") &&
      !options.is_none("gene_transcript_map")) {
    metacontig = get_meta_contig_to_contig(reader, options.get_string("gene_transcript_map"));
    bo.metacontig = &metacontig;
    bo.gene_tag = "MC";
  }

  const auto method = parse_cluster_method(options.get_string("method"));
  if (!method) raise_value_error("unknown --method: " + options.get_string("method"));
  UMIClusterer clusterer(*method);
  const std::int64_t threshold = options.get_int("threshold");

  // ReadDeduplicator's whitelist half.
  const bool filter_umi = options.get_bool("filter_umi");
  std::set<std::string> umi_whitelist;
  std::int64_t non_whitelist_count = 0;
  if (filter_umi) {
    // getUserDefinedBarcodes(..., deriveErrorCorrection=False)[0] — the set only.
    umi_whitelist = get_user_defined_barcodes(
        options.get_string("umi_whitelist"),
        options.is_none("umi_whitelist_paired") ? std::string()
                                                : options.get_string("umi_whitelist_paired")).first;
    log.info("Length of UMI whitelist: " + std::to_string(umi_whitelist.size()));
  }
  // network.py:422 is `if self.umi_whitelist:` — the loaded SET, not the flag.
  // An empty or comment-only whitelist file is FALSY, so Python does NO
  // filtering and emits every deduplicated read. Branching on filter_umi with an
  // empty set made every cluster fail the test, so `final_umis` was empty for
  // every bundle and the whole run produced nothing.
  const bool use_whitelist = filter_umi && !umi_whitelist.empty();
  auto in_whitelist = [&umi_whitelist](const Bytes& u) {
    return umi_whitelist.count(u) != 0;   // `cluster[0].decode() in self.umi_whitelist`
  };

  std::optional<double> subset;
  if (!options.is_none("subset")) subset = options.get_float("subset");

  BundleReadEvents events;

  // --output-stats. The generator draws from NUMPY's stream, which dedup.py
  // seeds separately (`if options.random_seed: np.random.seed(...)` —
  // truthiness, so --random-seed=0 leaves it unseeded) and which is a DIFFERENT
  // sequence from the stdlib one get_bundles uses.
  DedupStats stats;
  std::optional<PyRandom> numpy_rng;
  std::optional<RandomReadGenerator> read_gn;
  if (want_stats) {
    numpy_rng.emplace();
    if (options.get_int_truthy("random_seed"))
      numpy_rng->seed_numpy(options.get_int("random_seed"));
    read_gn.emplace(in_name, reference,
                    options.is_none("chrom") ? std::string() : options.get_string("chrom"),
                    bo, *numpy_rng);
  }

  for_each_bundle(
      reader, bo, /*only_count_reads=*/false, /*all_reads=*/false,
      /*return_read2=*/false, /*return_unmapped=*/false, rng, subset, events,
      [&](const Bundle& bundle, const BundlePos& bpos, const PositionalKey& key) {
        (void)bpos;
        (void)key;
        for (const auto& [umi, entry] : bundle) { (void)umi; n_input += entry.count; }

        while (n_output >= output_reads + 100000) {
          output_reads += 100000;
          log.info("Written out " + std::to_string(output_reads) + " reads");
        }
        while (n_input >= input_reads + 1000000) {
          input_reads += 1000000;
          log.info("Parsed " + std::to_string(input_reads) + " input reads");
        }

        if (ignore_umi) {
          for (const auto& [umi, entry] : bundle) {
            (void)umi;
            n_output += 1;
            emit(entry.reads.at(0));
          }
          return;
        }

        // --- ReadDeduplicator.__call__ ---
        UmiCounts counts;
        for (const auto& [umi, entry] : bundle) counts[umi] = entry.count;
        const auto clusters = clusterer(counts, threshold);

        std::vector<Bytes> final_umis;
        for (const auto& cluster : clusters) {
          std::int64_t cluster_count = 0;
          for (const Bytes& u : cluster) cluster_count += counts.at(u);
          if (use_whitelist) {
            // The whole group is kept or discarded on its TOP umi alone.
            if (in_whitelist(cluster[0])) {
              final_umis.push_back(cluster[0]);
            } else {
              non_whitelist_count += cluster_count;
            }
          } else {
            final_umis.push_back(cluster[0]);
          }
        }

        // `if len(reads) == 0: continue` — with --filter-umi a bundle can empty.
        if (final_umis.empty()) return;

        for (const Bytes& u : final_umis) {
          emit(bundle.at(u).reads.at(0));
          n_output += 1;
        }

        if (want_stats) {
          // PRE: every UMI in the bundle, with its count.
          std::vector<Bytes> bundle_umis;
          for (const auto& [umi, entry] : bundle) {
            bundle_umis.push_back(umi);
            stats.pre[umi].push_back(entry.count);
          }
          stats.pre_cluster.push_back(get_average_umi_distance(bundle_umis));
          stats.pre_cluster_null.push_back(get_average_umi_distance(
              read_gn->get_umis(static_cast<std::int64_t>(bundle.size()))));

          // POST: the surviving UMIs, with their summed cluster counts.
          std::vector<Bytes> post_umis;
          for (const auto& cluster : clusters) {
            const Bytes& top = cluster[0];
            if (use_whitelist && !in_whitelist(top)) continue;
            std::int64_t cluster_count = 0;
            for (const Bytes& x : cluster) cluster_count += counts.at(x);
            stats.post[top].push_back(cluster_count);
          }
          // `post_cluster_umis = [barcode_getter(x)[0] for x in reads]` — read
          // back off the RETAINED READS, not off `umis`. For --per-cell these
          // differ: the bundle key carries the cell, the read's barcode does not.
          for (const Bytes& u : final_umis) {
            const auto cu = barcode_for_read(bundle.at(u).reads.at(0), bo);
            post_umis.push_back(cu ? cu->umi : Bytes());
          }
          stats.post_cluster.push_back(get_average_umi_distance(post_umis));
          stats.post_cluster_null.push_back(get_average_umi_distance(
              read_gn->get_umis(static_cast<std::int64_t>(post_umis.size()))));
        }
      },
      [](BamRecord&) {});   // return_read2=False, return_unmapped=False: never called

  // TwoPassPairWriter.close() dumps the outstanding mates and then closes the
  // underlying file itself, so it must not be closed twice.
  if (pair_writer) pair_writer->close();
  else outfile->close();
  if (!no_sort)
    sort_output(out_name, sorted_out_name, sorted_out_format, reference,
                /*remove_input=*/true, format_options);

  if (want_stats) write_stats(stats, stats_prefix, options.get_string("method"));

  // Counter.most_common() — count descending, ties in insertion order.
  std::vector<std::pair<std::string, std::int64_t>> ev;
  for (const auto& [k, n] : events.counts) ev.emplace_back(k, n);
  std::stable_sort(ev.begin(), ev.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string reads_line;
  for (std::size_t i = 0; i < ev.size(); ++i) {
    if (i) reads_line += ", ";
    reads_line += ev[i].first + ": " + std::to_string(ev[i].second);
  }
  log.info("Reads: " + reads_line);
  log.info("Number of reads out: " + std::to_string(n_output));

  if (!ignore_umi) {
    log.info("Total number of positions deduplicated: " +
             std::to_string(clusterer.positions()));
    if (clusterer.positions() > 0) {
      log.info("Mean number of unique UMIs per position: " +
               format_fixed(static_cast<double>(clusterer.total_umis_per_position()) /
                                static_cast<double>(clusterer.positions()), 2));
      log.info("Max. number of unique UMIs per position: " +
               std::to_string(clusterer.max_umis_per_position()));
    } else {
      log.warn("The BAM did not contain any valid reads/read pairs for deduplication");
    }
  }
  if (filter_umi)
    log.info(std::to_string(non_whitelist_count) +
             " UMIs were in a group where the top UMI was not a whitelist UMI and "
             "were therefore discarded");

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
  log.close();
  return 0;
}

}  // namespace umi_tools
