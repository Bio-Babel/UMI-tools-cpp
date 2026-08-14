// prepare_for_em — port of umi_tools/prepare_for_em.py::main.
//
// The one subcommand with NO SHIPPED FIXTURES (00_baseline.md F3), so it is
// validated by a purpose-built differential (validation/parity_prepare_for_em.py)
// rather than by a fixture. The shipped `paired.bam` turns out to be an ideal
// input for it: 8,451 reads, all paired, with 5,601 SECONDARY alignments, so the
// secondary/primary mate-matching branches are genuinely exercised on real data.
//
// TWO UPSTREAM ASYMMETRIES ARE REPRODUCED, NOT REPAIRED:
//
//  1. The template index is keyed on `not read.is_secondary`, but every lookup
//     is keyed on `read.is_secondary`. So "look for a mate with the same
//     primary/secondary status" actually finds the OPPOSITE status. Changing it
//     would change which mate is emitted for every secondary alignment.
//  2. pick_mate's search has NO `break`: when several candidates point back at
//     the read, the LAST one wins, not the first.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/tools.hpp"

namespace umi_tools {
namespace {

// (reference_name, pos, flag) — the third element's meaning differs between
// insertion and lookup; see the header comment.
using TemplateKey = std::tuple<std::string, std::int64_t, bool>;

std::vector<std::string> split_commas(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const std::size_t c = s.find(',', start);
    out.push_back(s.substr(start, c == std::string::npos ? c : c - start));
    if (c == std::string::npos) break;
    start = c + 1;
  }
  return out;
}

}  // namespace

int tool_prepare_for_em(const std::vector<std::string>& argv) {
  const ToolSpec* spec = find_tool_spec("prepare_for_em");
  if (spec == nullptr) return 1;

  // --no-usage only EXISTS when it is already in argv (Utilities.py:456).
  const MaybeNoUsage no_usage(*spec, argv);
  spec = &no_usage.spec();

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
    out.write("prepare_for_em version: $Id$\n");
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
    std::vector<std::string> full_argv{"prepare_for_em"};
    full_argv.insert(full_argv.end(), argv.begin(), argv.end());
    log.write_raw(get_header(full_argv) + "\n");
    log.write_raw(get_params(options) + "\n");
  }


  // Unlike group/dedup, a missing --stdin is NOT an error here: it falls back to
  // "-" and reads standard input.
  const std::string in_name =
      options.is_none("stdin") ? std::string("-") : options.get_string("stdin");
  const std::string out_name =
      options.is_none("stdout") ? std::string("-") : options.get_string("stdout");
  const std::string reference =
      options.is_none("reference_filename") ? "" : options.get_string("reference_filename");

  // `--sam` selects the INPUT format here (prepare_for_em.py:170 passes
  // `options.sam`, not `options.in_sam` — this tool has no --in-sam). htslib
  // sniffs the format from the file, so it selects no parser; it is consulted
  // only because pysam interpolates the resulting mode into two of its header
  // error messages. It used to be `(void)sam`, which was true only for as long
  // as the port reported those errors in its own words.
  const bool sam = options.get_bool("sam");
  // prepare_for_em.py:179 passes options.OUT_SAM only — `--sam` is deliberately
  // absent from the OUTPUT determination, despite its help text claiming "input
  // and output SAM". An upstream quirk: ORing --sam in made
  // `prepare_for_em --sam -I in.bam -S out.bam` write SAM where Python writes BAM.
  const std::string out_format = determine_format(
      out_name, options.get_bool("out_sam"),
      options.is_none("out_format") ? "" : options.get_string("out_format"));

  const std::vector<std::string> tags =
      split_commas(options.is_none("tags") ? std::string("UG,BX")
                                           : options.get_string("tags"));

  // --input-options was parsed and dropped; it reaches htslib now.
  const std::string in_format_options =
      options.is_none("input_options") ? std::string() : options.get_string("input_options");
  const std::string in_mode = input_mode_for_format(determine_format(
      in_name, sam,
      options.is_none("in_format") ? "" : options.get_string("in_format")));
  AlignmentReader reader(in_name, reference, in_format_options, in_mode);
  AlignmentWriter outbam(out_name, out_format, reader.header(), reference,
                         options.is_none("output_options")
                             ? std::string()
                             : options.get_string("output_options"));

  std::int64_t pairs_output = 0, no_mate = 0, skipped_not_read12 = 0;

  auto contig_of = [&reader](std::int32_t tid) -> std::string {
    // pysam yields None for tid == -1; "" stands in, consistently on both sides
    // of every comparison (the mate-key compares below).
    return tid < 0 ? std::string() : std::string(reader.target_name(tid));
  };
  // ...but a WARNING is not a comparison. `"\t".join(map(str, [...]))` renders
  // None as the four characters "None", so upstream's line reads
  //   Alignment QNAME\t141\tNone\t-1 has no mate -- skipped
  // where the port left the field EMPTY. The "" stand-in is right for
  // the keys and wrong for the two log strings, so the log uses this instead.
  auto contig_for_log = [&contig_of](std::int32_t tid) -> std::string {
    return tid < 0 ? std::string("None") : contig_of(tid);
  };

  // chunk_bam: consecutive reads sharing a query_name form one template. The
  // final buffer is yielded UNCONDITIONALLY (prepare_for_em.py:95), so a file
  // with zero alignment records yields exactly one EMPTY template.
  //
  // That empty template then reaches
  //     assert len(set(r.query_name for r in template)) == 1
  // (prepare_for_em.py:186), which is `assert 0 == 1` — AssertionError,
  // traceback, exit 1, and NO summary line. The old comment here said an empty
  // template "simply produces no output", which was the wrong contract: the
  // port swallowed it, exited 0, wrote a header-only file and logged
  // "Total pairs output: 0, ...". MEASURED on a record-less BAM: oracle rc=1
  // with AssertionError and no summary; port rc=0 with a summary.
  std::vector<BamRecord> templ;
  std::string last_query_name;
  bool have_last = false;

  auto process_template = [&](std::vector<BamRecord>& reads) {
    if (reads.empty())
      throw std::invalid_argument("assert len(set(r.query_name for r in template)) == 1");

    // current_template[is_read1][key] -> the reads at that key.
    std::map<bool, std::map<TemplateKey, std::vector<std::size_t>>> current;
    current[true];
    current[false];
    for (std::size_t i = 0; i < reads.size(); ++i) {
      const BamRecord& r = reads[i];
      // NOTE `!is_secondary` on INSERT — deliberately mismatched with the
      // lookups below. Upstream's asymmetry, reproduced.
      current[r.is_read1()][TemplateKey{contig_of(r.tid()), r.pos(), !r.is_secondary()}]
          .push_back(i);
    }

    // pick_mate(read, template_dict, mate_key)
    auto pick_mate = [&](const BamRecord& read,
                         const TemplateKey& mate_key) -> std::int64_t {
      auto& side = current[!read.is_read1()];
      auto it = side.find(mate_key);
      if (it == side.end()) return -1;
      const std::vector<std::size_t>& candidates = it->second;
      std::int64_t mate = -1;
      for (std::size_t idx : candidates) {
        const BamRecord& c = reads[idx];
        // NO `break` upstream: the LAST match wins.
        if (contig_of(c.mate_tid()) == contig_of(read.tid()) &&
            c.next_reference_start() == read.pos())
          mate = static_cast<std::int64_t>(idx);
      }
      // "if no such read is found, then pick any old secondary alignment"
      if (mate < 0 && !candidates.empty())
        mate = static_cast<std::int64_t>(candidates.front());
      return mate;
    };

    std::set<std::string> output;

    for (const BamRecord& read : reads) {
      const TemplateKey same{contig_of(read.mate_tid()), read.next_reference_start(),
                             read.is_secondary()};
      std::int64_t mate_ix = pick_mate(read, same);
      if (mate_ix < 0) {
        const TemplateKey opposite{contig_of(read.mate_tid()),
                                   read.next_reference_start(), !read.is_secondary()};
        mate_ix = pick_mate(read, opposite);
      }
      if (mate_ix < 0) {
        no_mate += 1;
        log.warn("Alignment " + std::string(read.query_name()) + "\t" +
                 std::to_string(read.flag()) + "\t" + contig_for_log(read.tid()) + "\t" +
                 std::to_string(read.pos()) + " has no mate -- skipped");
        continue;
      }

      // Upstream COPIES both records before mutating them, so the originals stay
      // usable when the same read is reached again from the other side.
      //
      // The copy is a SAM-TEXT ROUND TRIP, not a byte copy, and that is
      // observable: it normalises every integer aux tag to its smallest subtype.
      // clone() (bam_copy1) kept the input's widths, so the emitted BAM bytes
      // differed from upstream's while the SAM text agreed exactly.
      BamRecord r = read.clone_via_sam_text(reader.header());
      // `mate = pysam.AlignedSegment().from_dict(mate.to_dict(), read.header)`
      // — the mate goes through the same round trip, with the SAME header.
      BamRecord m =
          reads[static_cast<std::size_t>(mate_ix)].clone_via_sam_text(reader.header());

      // "if our read is secondary, the mate is also secondary" — one-directional.
      if (r.is_secondary()) m.set_secondary(true);

      if (r.is_read1()) {
        for (const std::string& t : tags) m.copy_tag_from(r, t.c_str());
        const std::string key = r.to_string(reader.header()) + m.to_string(reader.header());
        if (output.insert(key).second) {
          outbam.write(r);
          outbam.write(m);
          pairs_output += 1;
        }
      } else if (r.is_read2()) {
        for (const std::string& t : tags) r.copy_tag_from(m, t.c_str());
        const std::string key = m.to_string(reader.header()) + r.to_string(reader.header());
        if (output.insert(key).second) {
          outbam.write(m);
          outbam.write(r);
          pairs_output += 1;
        }
      } else {
        skipped_not_read12 += 1;
        log.warn("Alignment " + std::string(read.query_name()) + "\t" +
                 std::to_string(read.flag()) + "\t" + contig_for_log(read.tid()) + "\t" +
                 std::to_string(read.pos()) + " is neither read1 nor read2 -- skipped");
        continue;
      }
    }
  };

  BamRecord read;
  while (reader.next(read)) {
    const std::string qname(read.query_name());
    if (have_last && last_query_name != qname) {
      process_template(templ);
      templ.clear();
    }
    last_query_name = qname;
    have_last = true;
    templ.push_back(read.clone());
  }
  process_template(templ);

  // `if not out_name == "-": outbam.close()` — a stdout destination is left
  // unclosed upstream. The RAII writer closes either way; the difference is not
  // observable in the bytes written.
  outbam.close();

  log.info("Total pairs output: " + std::to_string(pairs_output) +
           ", Pairs skipped - no mates: " + std::to_string(no_mate) +
           ", Pairs skipped - not read1 or 2: " + std::to_string(skipped_not_read12));

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
