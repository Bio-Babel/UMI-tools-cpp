// group — port of umi_tools/group.py::main.
//
// The first subcommand that takes get_bundles' POSITIONAL path and the first
// that WRITES an alignment file, so it exercises three things nothing before it
// did: the 4-tuple bundle key, AlignmentWriter, and sort_output.
//
// The output plumbing is Utilities.output_names_and_formats' indirection, which
// is easy to get subtly wrong: unless --no-sort-output is given, reads are
// written to a TEMP file first and only then sorted into the real destination.
// The temp file's format is "sam" only when the eventual format is sam, else
// "bam" — it is NOT simply the eventual format, because a CRAM temp would be
// re-compressed for nothing.
#include <algorithm>
#include <cstdint>
#include <cstdio>
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

int tool_group(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("group");
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
    out.write("group version: $Id$\n");
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
    std::vector<std::string> full_argv{"group"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  // U.validateSamOptions(options, group=True). Several of its checks test the
  // RAW COMMAND STRING rather than the parsed value ("--chimeric-pairs" in
  // command), so the joined argv is rebuilt here the same way.
  // `sys.argv` AFTER umi_tools.py's `del sys.argv[0]`, so it starts at "group".
  std::string command = "group";
  for (const std::string& a : argv) command += " " + a;
  SamOptionsResult sam = validate_sam_options(options, /*group=*/true, command);
  if (sam.error) { raise_value_error(*sam.error); return 1; }

  // `if options.stdin != sys.stdin` — did argv REPLACE the default stream?
  // is_none() answered a different question and was always false here, so this
  // guard never fired: the only way to reach it was a non-empty argv without
  // --stdin (`dedup --paired`), and that fell through to htslib's
  // "cannot read header: -" instead of upstream's message.
  if (!options.was_given("stdin"))
    throw std::invalid_argument("Input on standard in not currently supported");
  const std::string in_name = options.get_string("stdin");

  const bool output_bam = options.get_bool("output_bam");
  const std::string eventual_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");
  if (eventual_name != "-" && !output_bam)
    raise_value_error("To output a bam you must include --output-bam option");

  const std::string reference =
      options.is_none("reference_filename") ? "" : options.get_string("reference_filename");
  const std::string in_format_opt =
      options.is_none("in_format") ? "" : options.get_string("in_format");
  // --input-options was parsed and dropped; it reaches htslib now.
  const std::string in_format_options =
      options.is_none("input_options") ? std::string() : options.get_string("input_options");
  // group.py:169 — see the note in tool_dedup.cpp: consulted only by pysam's
  // header error messages, which interpolate the mode.
  const std::string in_mode = input_mode_for_format(
      determine_format(in_name, options.get_bool("in_sam"), in_format_opt));
  AlignmentReader reader(in_name, reference, in_format_options, in_mode);

  // Declared here so it outlives the for_each_bundle call below; it is FILLED
  // in after the BundleOptions exist (see the metacontig block further down).
  MetaContigMap metacontig;

  // Utilities.output_names_and_formats.
  const bool no_sort = options.get_bool("no_sort_output");
  std::string out_name, out_format, sorted_out_name, sorted_out_format;
  bool have_outfile = output_bam || !in_format_opt.empty();
  if (have_outfile) {
    const std::string eventual_format = determine_format(
        eventual_name, options.get_bool("out_sam"),
        options.is_none("out_format") ? "" : options.get_string("out_format"));
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
    // Python prints the split-and-encoded list, or None. --output-options is
    // used by 0 of the 69 shipped fixtures, and this is a debug-level line, so
    // it is not reachable at the default loglevel either way.
    const std::string fmt_opts_repr =
        options.is_none("output_options") ? "None" : options.get_string("output_options");
    log.debug("Opening " + out_name + ", format " + out_format +
              ", for output with options " + fmt_opts_repr);
  }

  std::optional<AlignmentWriter> outfile;
  if (have_outfile)
    outfile.emplace(out_name, out_format, reader.header(), reference,
                    options.is_none("output_options") ? std::string()
                                                      : options.get_string("output_options"));

  const std::string tsv_name = options.is_none("tsv") ? "" : options.get_string("tsv");
  std::optional<Writer> mapping_outfile;
  if (!tsv_name.empty()) {
    mapping_outfile.emplace(tsv_name);
    mapping_outfile->write(
        "read_id\tcontig\tposition\tgene\tumi\tumi_count\tfinal_umi\t"
        "final_umi_count\tunique_id\n");
  }

  std::int64_t n_input = 0, n_output = 0, unique_id = 0;
  std::int64_t input_reads = 0, output_reads = 0;

  const bool per_gene = options.get_bool("per_gene");
  const bool per_contig = options.get_bool("per_contig");
  const std::string umi_group_tag = options.get_string("umi_group_tag");
  // Upstream touches --umi-group-tag ONLY inside `if outfile:`
  // (group.py:266), so with --group-out and no --output-bam the name is never
  // validated and the run completes. Validating unconditionally here made
  // `group --group-out=out.tsv --umi-group-tag=ABC` exit 1 with no output where
  // upstream exits 0 with a full TSV (measured: 55,198 lines vs 1).
  //
  // `have_outfile` is computed above, so the check happens here — at the point
  // the tag is actually used — rather than unconditionally.
  if (have_outfile) check_tag_name(umi_group_tag, "--umi-group-tag");
  const double soft_clip_threshold = options.get_float("soft_clip_threshold");

  // group.py: output_unmapped drives BOTH return_unmapped AND fetch's until_eof.
  const bool output_unmapped =
      sam.unmapped_reads == "use" || sam.unmapped_reads == "output";

  BundleOptions bo = make_bundle_options(options, sam);

  // group.py:200-211 — unlike dedup and count, group's bare read is
  // `fetch(until_eof=output_unmapped)`, and `until_eof=True` needs NO index. So
  // the index is required for every branch EXCEPT that one: --chrom is
  // `fetch(reference=...)`, the metafetcher is `fetch(contig)` per contig, and
  // the bare form needs an index only when output_unmapped is False.
  //
  // MEASURED on a copy of chr19_gene_tags.bam made without its .bai:
  // `group --output-bam` exits 1 upstream, `group --output-bam
  // --unmapped-reads=output` exits 0. See the longer note in tool_dedup.cpp.
  if (!output_unmapped || !options.is_none("chrom") ||
      (options.get_bool("per_gene") && !options.is_none("gene_transcript_map")))
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
  if (options.is_none("chrom") && options.get_bool("per_gene") && !options.is_none("gene_transcript_map")) {
    metacontig = get_meta_contig_to_contig(reader, options.get_string("gene_transcript_map"));
    bo.metacontig = &metacontig;
    bo.gene_tag = "MC";
  }


  const auto method = parse_cluster_method(options.get_string("method"));
  if (!method) raise_value_error("unknown --method: " + options.get_string("method"));
  UMIClusterer processor(*method);
  const std::int64_t threshold = options.get_int("threshold");

  // `Utilities.Start` seeds the stdlib RNG when --random-seed is not None; the
  // same stream serves --subset. group never reaches the reservoir tie-break
  // (all_reads=True keeps every read), so --subset is its only consumer.
  // Utilities.Start seeds it with `is not None`, NOT truthiness — so
  // --random-seed=0 does seed here, unlike count.py's own `if options.random_seed:`.
  PyRandom& rng = global_random();
  if (!options.is_none("random_seed")) rng.seed(options.get_int("random_seed"));
  std::optional<double> subset;
  if (!options.is_none("subset")) subset = options.get_float("subset");

  BundleReadEvents events;

  for_each_bundle(
      reader, bo, /*only_count_reads=*/false, /*all_reads=*/true,
      /*return_read2=*/true, /*return_unmapped=*/output_unmapped, rng, subset, events,
      // --- status == "bundle" ---
      [&](const Bundle& bundle, const BundlePos& bpos, const PositionalKey& key) {
        (void)bpos;
        (void)key;   // group.py binds `key` and never uses it
        UmiCounts counts;
        for (const auto& [umi, entry] : bundle) counts[umi] = entry.count;
        for (const auto& [umi, entry] : bundle) { (void)umi; n_input += entry.count; }

        while (n_output >= output_reads + 10000) {
          output_reads += 10000;
          log.info("Written out " + std::to_string(output_reads) + " reads");
        }
        while (n_input >= input_reads + 1000000) {
          input_reads += 1000000;
          log.info("Parsed " + std::to_string(input_reads) + " input reads");
        }

        const auto groups = processor(counts, threshold);
        for (const auto& umi_group : groups) {
          const Bytes& top_umi = umi_group[0];
          std::int64_t group_count = 0;
          for (const Bytes& u : umi_group) group_count += counts.at(u);

          for (const Bytes& umi : umi_group) {
            // all_reads=True, so this is the full read list for the umi.
            for (const BamRecord& cr : bundle.at(umi).reads) {
              BamRecord& read = const_cast<BamRecord&>(cr);
              if (outfile) {
                read.set_tag_int("UG", unique_id);
                read.set_tag_str(umi_group_tag.c_str(), top_umi);
                outfile->write(read);
              }
              if (mapping_outfile) {
                // group.py:282 does
                // `umi.decode()`, but the --ignore-umi branch sets `umi = ""`
                // -- a str, not bytes (sam_methods.py:407-411). So
                // `group --ignore-umi --group-out=...` ABORTS upstream on the
                // first record it tries to write, for ANY input, leaving the
                // file with only its header row. MEASURED: oracle rc=1 with
                // `AttributeError: 'str' object has no attribute 'decode'` and
                // a 1-line group-out; the port wrote all 55,198 rows at rc=0.
                //
                // Without --group-out the write is unreachable and upstream
                // instead exits 1 on the --output-bam assertion, which the port
                // already matched.
                if (bo.ignore_umi)
                  throw std::invalid_argument(
                      "'str' object has no attribute 'decode'");
                std::string gene = "NA";
                if (per_gene) {
                  if (per_contig) {
                    gene = std::string(reader.target_name(read.tid()));
                  } else {
                    auto g = read.get_tag_str(bo.gene_tag.c_str());
                    if (g) gene = *g;
                  }
                }
                const std::int64_t pos =
                    get_read_position(read, soft_clip_threshold).pos;
                mapping_outfile->write(
                    std::string(read.query_name()) + "\t" +
                    std::string(reader.target_name(read.tid())) + "\t" +
                    std::to_string(pos) + "\t" + gene + "\t" + umi + "\t" +
                    std::to_string(counts.at(umi)) + "\t" + top_umi + "\t" +
                    std::to_string(group_count) + "\t" +
                    std::to_string(unique_id) + "\n");
              }
              n_output += 1;
            }
          }
          unique_id += 1;
        }
      },
      // --- status == "single_read": written straight out, never grouped ---
      [&](BamRecord& read) {
        n_input += 1;
        if (outfile) outfile->write(read);
        n_output += 1;
      });

  if (outfile) {
    outfile->close();
    if (!no_sort)
      sort_output(out_name, sorted_out_name, sorted_out_format, reference,
                  /*remove_input=*/true,
                  options.is_none("output_options") ? std::string()
                                                    : options.get_string("output_options"));
  }
  if (mapping_outfile) mapping_outfile->close();

  // "Reads: a: 1, b: 2" in Counter.most_common() order — sorted by count
  // DESCENDING, ties keeping insertion order (Python's sort is stable).
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
  log.info("Number of reads out: " + std::to_string(n_output) +
           ", Number of groups: " + std::to_string(unique_id));
  log.info("Total number of positions deduplicated: " +
           std::to_string(processor.positions()));
  if (processor.positions() > 0) {
    log.info("Mean number of unique UMIs per position: " +
             format_fixed(static_cast<double>(processor.total_umis_per_position()) /
                              static_cast<double>(processor.positions()), 2));
    log.info("Max. number of unique UMIs per position: " +
             std::to_string(processor.max_umis_per_position()));
  } else {
    log.warn("The BAM did not contain any valid reads/read pairs for deduplication");
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
  log.close();
  return 0;
}

}  // namespace umi_tools
