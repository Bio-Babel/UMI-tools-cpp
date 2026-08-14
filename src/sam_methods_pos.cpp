// The POSITIONAL half of sam_methods: find_splice, get_read_position, and
// get_bundles' non-per-gene branch. Slice 6.
#include "umi_tools/sam_methods.hpp"

#include <algorithm>
#include <stdexcept>

#include "umi_tools/logging.hpp"
#include "umi_tools/py_random.hpp"

#include <map>
#include <vector>
#include <optional>
#include <functional>

namespace umi_tools {
namespace {
// htslib CIGAR op codes, same numbering pysam exposes.
constexpr std::uint32_t kM = 0, kI = 1, kD = 2, kN = 3, kS = 4, kH = 5, kP = 6,
                        kEq = 7, kX = 8;
}  // namespace

std::int64_t find_splice(const std::vector<CigarOp>& cigar_in) {
  // Python:
  //   offset = 0
  //   if cigar[0][0] == 4: offset = cigar[0][1]; cigar = cigar[1:]
  //   for op, bases in cigar:
  //       if op in (3, 4):        return offset      # N or S: the splice
  //       elif op in (0,2,7,8):   offset += bases    # M D = X: ref-consuming
  //       elif op in (1,5,6):     continue           # I H P: not ref-consuming
  //       else: raise ValueError("Bad Cigar operation: %i" % op)
  //   return False
  //
  // A LEADING soft clip is taken as an offset, not as a splice; a soft clip
  // anywhere else IS the splice. Returning `offset` when that offset is 0 is
  // indistinguishable from the final `False` — deliberately (D5).
  std::vector<CigarOp> cigar = cigar_in;
  if (cigar.empty()) return 0;                 // Python would IndexError; see below
  std::int64_t offset = 0;
  if (cigar.front().op == kS) {
    offset = cigar.front().len;
    cigar.erase(cigar.begin());
  }
  for (const CigarOp& c : cigar) {
    if (c.op == kN || c.op == kS) return offset;
    if (c.op == kM || c.op == kD || c.op == kEq || c.op == kX) {
      offset += c.len;
    } else if (c.op == kI || c.op == kH || c.op == kP) {
      continue;
    } else {
      throw std::invalid_argument("Bad Cigar operation: " + std::to_string(c.op));
    }
  }
  return 0;   // Python's `False`
}

ReadPosition get_read_position(const BamRecord& read, double soft_clip_threshold) {
  const std::vector<CigarOp> cigar = read.cigar();
  if (cigar.empty())
    throw std::invalid_argument(
        "get_read_position: read has no CIGAR (the Python raises IndexError on "
        "read.cigar[0])");

  ReadPosition out;
  const std::string cigarstring = read.cigarstring();
  const bool has_n = cigarstring.find('N') != std::string::npos;

  if (read.is_reverse()) {
    out.pos = read.reference_end();                       // read.aend
    if (cigar.back().op == kS) out.pos += cigar.back().len;
    out.start = read.pos();
    if (has_n || (cigar.front().op == kS &&
                  static_cast<double>(cigar.front().len) > soft_clip_threshold)) {
      std::vector<CigarOp> reversed(cigar.rbegin(), cigar.rend());
      out.is_spliced = find_splice(reversed);
    }
  } else {
    out.pos = read.pos();
    // THE NEGATIVE CASE: a 5'-soft-clipped forward read near a contig start
    // drives pos below zero. Signed throughout (D5).
    if (cigar.front().op == kS) out.pos -= cigar.front().len;
    out.start = out.pos;
    if (has_n || (cigar.back().op == kS &&
                  static_cast<double>(cigar.back().len) > soft_clip_threshold)) {
      out.is_spliced = find_splice(cigar);
    }
  }
  return out;
}



// Which tag pysam would name in its KeyError, in UPSTREAM'S ORDER:
// get_barcode_tag does `umi = read.get_tag(umi_tag)` and only then
// `cell = read.get_tag(cell_tag)` (sam_methods.py:51-56), so when both are
// absent the message names the UMI tag. Only the Tag method can return nullopt;
// the read-id and umis getters raise ValueError from inside instead.
std::string missing_barcode_tag_message(const BamRecord& read,
                                        const BundleOptions& options) {
  if (options.get_umi_method == UmiMethod::Tag) {
    const auto& o = options.tag_options;
    if (!read.get_tag_str(o.umi_tag.c_str()))
      return "tag '" + o.umi_tag + "' not present";
    if (options.per_cell && !o.cell_tag.empty() &&
        !read.get_tag_str(o.cell_tag.c_str()))
      return "tag '" + o.cell_tag + "' not present";
  }
  return "Could not extract UMI +/- cell barcode from the read";
}

std::optional<CellUmi> barcode_for_read(const BamRecord& read, const BundleOptions& options) {
  switch (options.get_umi_method) {
    case UmiMethod::ReadId:
      return get_barcode_read_id(read, options.per_cell, options.umi_sep);
    case UmiMethod::Umis:
      return get_barcode_umis(read, options.per_cell);
    case UmiMethod::Tag:
      return get_barcode_tag(read, options.per_cell, options.tag_options);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// get_bundles.__call__ — the POSITIONAL branch (slice 6), used by `group` and
// `dedup`. The per-gene branch lives in sam_methods_bam.cpp.
// ---------------------------------------------------------------------------
namespace {

// `sorted(reads_dict.keys())` at the outer level is a numeric sort over
// positions that CAN BE NEGATIVE (D5), which is why std::map<int64_t,...> is
// used rather than anything unsigned: a size_t key would wrap a soft-clipped
// read at a contig start to ~2^64 and drag it to the end of every bundle flush.
using PosMap = std::map<BundlePos, std::map<PositionalKey, Bundle>>;
using CountMap = std::map<BundlePos, std::map<PositionalKey, OrderedMap<Bytes, std::int64_t>>>;

}  // namespace

void for_each_bundle(
    AlignmentReader& reader, const BundleOptions& options, bool only_count_reads,
    bool all_reads, bool return_read2, bool return_unmapped, PyRandom& rng,
    std::optional<double> subset, BundleReadEvents& events,
    const std::function<void(const Bundle&, const BundlePos&, const PositionalKey&)>& on_bundle,
    const std::function<void(BamRecord&)>& on_single_read) {
  const SkipRegex skip(options.skip_regex);
  PosMap reads_dict;
  CountMap read_counts;

  // get_bundles.__init__: contig -> its metacontig, plus the per-gene record of
  // which transcripts have actually been seen.
  OrderedMap<std::string, std::string> contig_metacontig;
  OrderedMap<std::string, OrderedSet<std::string>> observed_contigs;
  if (options.metacontig != nullptr)
    for (const auto& [metacontig, contigs] : *options.metacontig)
      for (const std::string& contig : contigs) contig_metacontig[contig] = metacontig;

  std::optional<std::string> last_chr;   // Python's None
  std::string current_chr;
  std::string last_gene;              // self.last_pos, when pos IS the gene
  std::int64_t last_pos = 0;
  std::int64_t start = 0;

  auto bump = [&events](const char* k) { events.counts[k] += 1; };

  // `if self.options.subset:` — TRUTHINESS. A --subset of 0.0 is falsy and so
  // disables subsetting entirely rather than dropping every read.
  const bool use_subset = subset.has_value() && *subset != 0.0;

  // Emit `reads_dict[p][k]` for each p in out_keys, in sorted key order, then
  // delete p. Mirrors the generator's yield-then-del so memory is released on
  // the same schedule.
  // `drop_counts` is false for the PER-GENE flush: Python deletes
  // reads_dict[p] alone there (sam_methods.py:473-477) and deletes read_counts
  // only in the positional branch (:494-496). Erasing both restarted the
  // reservoir counter at 0 (prob 1.0, the challenger always wins) where Python
  // resumes at its stale value — a different read retained, and the RNG stream
  // diverging from there on.
  auto flush = [&](const std::vector<BundlePos>& out_keys, bool drop_counts) {
    for (const BundlePos& p : out_keys) {
      auto it = reads_dict.find(p);
      if (it == reads_dict.end()) continue;
      for (const auto& [k, bundle] : it->second) on_bundle(bundle, p, k);
      reads_dict.erase(it);
      if (drop_counts) read_counts.erase(p);
    }
  };

  // sam_methods.metafetcher, when --gene-transcript-map is in play: instead of
  // one linear pass, fetch each metacontig's contigs in turn and tag every read
  // "MC" with the gene. Upstream iterates a Python SET of contigs, so ITS order
  // varies with PYTHONHASHSEED (L33); OrderedSet gives the port the map file's
  // order, deterministically.
  // The pair is (gene, contig), NOT just the contig. metafetcher's tag
  // is `metacontig` — the gene whose contig list is CURRENTLY being fetched
  // (sam_methods.py:665-669) — so a contig listed under two genes is fetched
  // once per gene and its reads carry a DIFFERENT MC on each pass.
  //
  // This looked the gene up in contig_metacontig, a reverse contig->gene map
  // built last-gene-wins (:150-154), so both passes wrote the last gene.
  // MEASURED with a map putting chr1 under GENE_A and GENE_B, via
  // `group --output-bam --per-gene --per-contig` (which emits every read, unlike
  // dedup, whose survivor happened to agree):
  //   oracle  chr1/GENE_A 100, chr1/GENE_B 100, chr10/GENE_A 100, chr12/GENE_B 100
  //   port    chr1/GENE_B 200,                  chr10/GENE_A 100, chr12/GENE_B 100
  // The reverse map is still correct for the BUNDLE KEY at :345-354, where
  // upstream does the same `self.contig_metacontig[transcript]` lookup.
  std::vector<std::pair<std::string, std::string>> meta_contigs;  // (gene, contig)
  if (options.metacontig != nullptr)
    for (const auto& [metacontig, contigs] : *options.metacontig)
      for (const std::string& c : contigs) meta_contigs.emplace_back(metacontig, c);
  std::size_t meta_ix = 0;
  bool meta_region_open = false;
  std::string meta_current_gene;

  BamRecord read;
  auto next_read = [&](BamRecord& into) -> bool {
    if (options.metacontig == nullptr) return reader.next(into);
    while (true) {
      if (meta_region_open && reader.next(into)) {
        // metafetcher's `read.set_tag(metatag, metacontig)` — the gene of the
        // pass we are in, not a lookup keyed on the read's contig.
        into.set_tag_str("MC", meta_current_gene);
        return true;
      }
      if (meta_ix >= meta_contigs.size()) return false;
      meta_current_gene = meta_contigs[meta_ix].first;
      reader.set_region(meta_contigs[meta_ix].second);
      ++meta_ix;
      meta_region_open = true;
    }
  };

  while (next_read(read)) {
    // fetch(until_eof=output_unmapped). MEASURED on unmapped.bam: until_eof=False
    // returns exactly the records with tid >= 0 (9968 of 10000) and otherwise
    // preserves file order, so the index-backed iteration reduces to this filter.
    if (!return_unmapped && read.tid() < 0) continue;
    if (!options.chrom.empty()) {
      if (read.tid() < 0 || reader.target_name(read.tid()) != options.chrom) continue;
    }

    if (read.is_read2()) {
      if (return_read2) {
        if (!read.is_unmapped() || return_unmapped) on_single_read(read);
      }
      continue;
    }
    bump("Input Reads");

    // --- only read1s from here ---
    if (options.paired) {
      if (read.is_paired()) {
        bump("Read pairs");
      } else {
        bump("Unpaired reads");
        if (options.unpaired_reads == "discard") continue;
        if (options.unpaired_reads == "output") on_single_read(read);
        // "use": fall through, TLEN will be 0.
      }
    }

    if (read.is_unmapped()) {
      if (options.paired) {
        if (read.mate_is_unmapped()) bump("Both unmapped");
        else bump("Read 1 unmapped");
      } else {
        bump("Single end unmapped");
      }
      if (return_unmapped) {
        // Upstream counts this read as an input TWICE. Reproduced, not tidied:
        // the number appears in `group`'s "Reads:" log line.
        bump("Input Reads");
        on_single_read(read);
      }
      continue;
    }

    if (options.paired && read.mate_is_unmapped()) {
      if (!read.is_unmapped()) bump("Read 2 unmapped");
      if (options.unmapped_reads != "use") {
        if (return_unmapped) on_single_read(read);
        continue;
      }
    }

    // `read.reference_name != read.next_reference_name`. Both are None when the
    // tid is -1, so comparing the tids is the same predicate.
    if (read.is_paired() && read.tid() != read.mate_tid()) {
      bump("Chimeric read pair");
      if (options.chimeric_pairs == "discard") continue;
      if (options.chimeric_pairs == "output") { on_single_read(read); continue; }
      // "use": fall through.
    }

    if (use_subset) {
      if (rng.random() >= *subset) { bump("Randomly excluded"); continue; }
    }

    if (options.mapping_quality != 0 && read.mapq() < options.mapping_quality) {
      bump("< MAPQ threshold");
      continue;
    }

    // --- the barcode ---
    Bytes umi;
    std::optional<Bytes> cell;
    if (options.ignore_umi) {
      if (options.per_cell) {
        // Python still extracts, to get the cell, then blanks the umi.
        //
        // This call is OUTSIDE the try/except (sam_methods.py:407-410);
        // the `except KeyError: ... continue` guard at :414-430 wraps only the
        // else branch. So a read missing the tag ABORTS the run here, where in
        // the else branch it is skipped with a warning. Treating nullopt as
        // "no cell" instead bundled the read under a DIFFERENT cell key and
        // recorded no event, so the divergence was silent in both the output
        // and the log. MEASURED on validation/fixtures/cell_tag_missing.bam:
        //   dedup / count  oracle rc=1 KeyError: "tag 'CB' not present"
        //                  port   rc=0
        // With every CB present both sides are rc=0, so it is the missing tag
        // and not the option combination.
        const auto cu = barcode_for_read(read, options);
        if (!cu) throw std::invalid_argument(missing_barcode_tag_message(read, options));
        cell = cu->cell;
      }
      umi.clear();
    } else {
      const auto cu = barcode_for_read(read, options);
      if (!cu) {
        const char* msg = "Read skipped, missing umi and/or cell tag";
        if (events.counts.get(msg, 0) == 0)
          // `U.warn("... : %s" % formatted_read)` where
          // `formatted_read = read.to_string()` (sam_methods.py:422-428). That is
          // pysam's sam_format1 — the WHOLE SAM record — not the query name, so
          // the port's warning was a strict subset of upstream's on every
          // affected run.
          Log::instance().warn(std::string("At least one read is missing UMI and/or cell tag(s): ") +
                               read.to_string(reader.header()));
        events.counts[msg] += 1;
        continue;
      }
      umi = cu->umi;
      cell = cu->cell;
    }

    current_chr = std::string(reader.target_name(read.tid()));

    BundlePos pos;
    PositionalKey key;
    std::string transcript;            // per_contig + metacontig: read.reference_name
    const bool chr_changed = !last_chr || *last_chr != current_chr;

    if (options.per_gene) {
      // --- the gene is the "position" ---
      std::string gene;
      if (options.per_contig) {
        if (options.metacontig != nullptr) {
          // `transcript = read.reference_name; gene = self.contig_metacontig[transcript]`
          // — a PLAIN dict lookup, so an unmapped contig raises. Keying by the
          // transcript instead (what this used to do) put the wrong name in the
          // first output column AND made observed_contigs contig-keyed, so the
          // completeness test never fired and nothing flushed at a boundary.
          transcript = current_chr;
          if (!contig_metacontig.contains(transcript))
            throw std::out_of_range("contig_metacontig: no gene for contig '" +
                                    transcript + "'");
          gene = contig_metacontig.at(transcript);
        } else {
          gene = current_chr;
        }
      } else {
        auto assigned = read.get_tag_str(options.assigned_tag.c_str());
        auto gene_opt = read.get_tag_str(options.gene_tag.c_str());
        if (!assigned || !gene_opt) { bump("Read skipped, no tag"); continue; }
        if (gene_opt->empty()) {
          const char* msg = "Read skipped - gene string is empty";
          if (events.counts.get(msg, 0) == 0)
            // `U.warn("Assigned gene is empty string. First such read:\n%s"
            //         % read.to_string())` (sam_methods.py:456).
            Log::instance().warn("Assigned gene is empty string. First such read:\n" +
                                 read.to_string(reader.header()));
          events.counts[msg] += 1;
          continue;
        }
        if (skip.search(*assigned)) {
          bump("Read skipped - assigned tag matches skip_regex");
          continue;
        }
        gene = *gene_opt;
      }
      pos.gene = gene;
      key.gene = gene;                 // `key = pos`
      // NOTE: observed_contigs is updated at the BOTTOM of the loop body, after
      // update_dicts, exactly where Python does it. Updating it here made the
      // first read of a gene's second transcript compare {T1,T2} against
      // {T1,T2}, flush the gene early, and then re-accumulate it — emitting the
      // gene twice with split counts.

      // check_output(), per-gene branch. With a metacontig map the rule is
      // NARROWER: flush only the LAST gene, and only once every transcript of
      // that gene has been observed — because a gene's reads are spread over
      // several contigs and it is not complete until the last of them is done.
      if (last_chr && chr_changed) {
        std::vector<BundlePos> out_keys;
        if (options.metacontig != nullptr) {
          const auto& expected = options.metacontig->contains(last_gene)
                                     ? options.metacontig->at(last_gene)
                                     : OrderedSet<std::string>{};
          const auto& seen = observed_contigs.contains(last_gene)
                                 ? observed_contigs.at(last_gene)
                                 : OrderedSet<std::string>{};
          // Python compares two SETS, so membership decides, not order.
          bool complete = seen.size() == expected.size();
          if (complete)
            for (const std::string& c : expected)
              if (!seen.contains(c)) { complete = false; break; }
          if (complete) out_keys.push_back(BundlePos{last_gene, 0});
        } else {
          for (const auto& [p, _] : reads_dict) { (void)_; out_keys.push_back(p); }
        }
        flush(out_keys, /*drop_counts=*/false);
      }
      last_chr = current_chr;
      last_gene = gene;
    } else {
      const ReadPosition rp = get_read_position(read, options.soft_clip_threshold);
      start = rp.start;
      pos.pos = rp.pos;

      // check_output(), positional branch. Called UNCONDITIONALLY here, unlike
      // the per-gene branch's `if self.last_chr:` guard.
      {
        bool do_output = false;
        std::vector<BundlePos> out_keys;
        if (options.whole_contig) {
          if (chr_changed) {
            do_output = true;
            for (const auto& [p, _] : reads_dict) { (void)_; out_keys.push_back(p); }
          }
        } else if (start > last_pos + 1000 || chr_changed) {
          do_output = true;
          for (const auto& [p, _] : reads_dict) { (void)_; out_keys.push_back(p); }
          if (!chr_changed) {
            // Only positions fully behind the 1000-base window are complete.
            const std::int64_t cutoff = start - 1000;
            std::vector<BundlePos> kept;
            for (const BundlePos& p : out_keys) if (p.pos <= cutoff) kept.push_back(p);
            out_keys.swap(kept);
          }
        }
        if (do_output) flush(out_keys, /*drop_counts=*/true);   // std::map iterates sorted
      }

      last_pos = start;
      last_chr = current_chr;

      const std::int64_t r_length = options.read_length ? read.query_length() : 0;
      key.is_reverse = read.is_reverse() ? 1 : 0;
    // `self.options.spliced and is_spliced` — Python's `and` yields the FIRST
    // operand when it is falsy, so this is `False` (0) unless --spliced-is-unique
    // is set, in which case it is find_splice's int/False result.
    key.spliced = options.spliced ? rp.is_spliced : 0;
    // `(not ignore_tlen) * paired * read.tlen` — bool*bool*int ARITHMETIC, which
    // is 0 whenever either flag is off. Written with the same multiplication so
    // the zeroing is visibly the same rule, not a re-derived conditional.
    key.tlen = static_cast<std::int64_t>(!options.ignore_tlen) *
               static_cast<std::int64_t>(options.paired) * read.tlen();
      key.read_length = r_length;
    }
    key.cell = cell;

    // --- update_dicts ---
    //
    // Upstream's update_dicts is a separate METHOD whose four early
    // exits are `return` (sam_methods.py:246,251,258,266). Control goes back to
    // __call__, which still runs the loop body's LAST statement — the
    // observed_contigs update at :513-515. Inlining it here turned those four
    // returns into `continue`, which jumps past that update, so the first read
    // of a gene's second transcript usually lost the MAPQ/NH/XT comparison and
    // its transcript was never recorded. Reachable via
    // `dedup --per-gene --per-contig --gene-transcript-map=...`, where all
    // reads of a gene share one slot.
    //
    // The lambda restores the method boundary: `return` inside it means "return
    // from update_dicts", and the observed_contigs update below always runs.
    auto& slot = reads_dict[pos][key];
    [&]() {
    // `try: d[umi]["count"] += 1 / except KeyError: <init> / else: <update>`.
    // The innermost Python container is a plain dict, so the first touch of a
    // umi raises KeyError; `contains` is that same test.
    const bool seen = slot.contains(umi);
    if (only_count_reads) {
      // The `elif self.only_count_reads:` branch — a count, no reads retained.
      BundleEntry& e = slot[umi];
      e.count = seen ? e.count + 1 : 1;
    } else if (all_reads) {
      if (!seen) {
        BundleEntry& e = slot[umi];
        e.count = 1;
        e.reads.push_back(read.clone());
      } else {
        BundleEntry& e = slot[umi];
        e.count += 1;
        e.reads.push_back(read.clone());
      }
    } else {
      if (!seen) {
        BundleEntry& e = slot[umi];
        e.count = 1;
        e.reads.push_back(read.clone());
        read_counts[pos][key][umi] = 0;
      } else {
        BundleEntry& e = slot[umi];
        e.count += 1;
        BamRecord& best = e.reads[0];
        if (best.mapq() > read.mapq()) return;
        if (best.mapq() < read.mapq()) {
          best = read.clone();
          read_counts[pos][key][umi] = 0;
          return;
        }
        // Upstream calls `read.opt(tag)` UNGUARDED (sam_methods.py:
        // 256-267) and pysam raises `KeyError: "tag 'NH' not present"` when the
        // tag is absent; update_dicts has no handler, so the KeyError escapes
        // the generator and the run dies. This treated an absent tag as "no
        // opinion" and fell through to the reservoir tie-break below — which is
        // worse than a wrong answer, because the tie-break DRAWS from the shared
        // random stream, so every subsequent reservoir decision in the run
        // shifted too.
        //
        // Reachable because detect_bam_features inspects only the first
        // n_entries+1 records, so a BAM whose first 1001 reads all carry NH but
        // whose later reads do not passes validation. MEASURED on exactly that
        // shape: oracle rc 1 with the KeyError and no output, port rc 0 with a
        // 1,097-byte BAM.
        auto opt_int = [](const BamRecord& r, const char* tag) -> std::int64_t {
          const auto v = r.get_tag_int(tag);
          if (!v)
            throw std::invalid_argument(std::string("tag '") + tag + "' not present");
          return *v;
        };
        auto opt_str = [](const BamRecord& r, const char* tag) -> std::string {
          const auto v = r.get_tag_str(tag);
          if (!v)
            throw std::invalid_argument(std::string("tag '") + tag + "' not present");
          return *v;
        };
        if (options.detection_method == "NH" || options.detection_method == "X0") {
          const char* tag = options.detection_method.c_str();
          const std::int64_t a = opt_int(best, tag);
          const std::int64_t b = opt_int(read, tag);
          if (a < b) return;
          if (a > b) { best = read.clone(); read_counts[pos][key][umi] = 0; }
        } else if (options.detection_method == "XT") {
          // Upstream reads best's XT first and only reads the incoming read's
          // when best's is not "U", so a missing tag on `read` is NOT reached
          // when best is unique. Order preserved.
          if (opt_str(best, "XT") == "U") return;
          if (opt_str(read, "XT") == "U") {
            best = read.clone();
            read_counts[pos][key][umi] = 0;
          }
        }
        // RESERVOIR TIE-BREAK. read_counts goes 0 -> 1 first, so prob is 1.0 on
        // the first tie and random() < 1.0 is ALWAYS true (random() is [0,1)):
        // the second equally-good read always wins. Draws from the same stream
        // as --subset, so the call order above is contractual.
        std::int64_t& n = read_counts[pos][key][umi];
        n += 1;
        const double prob = 1.0 / static_cast<double>(n);
        if (rng.random() < prob) best = read.clone();
      }
    }
    }();   // end of the inlined update_dicts

    // `if self.metacontig_contig: self.observed_contigs[gene].add(transcript)`
    // — the LAST statement of the loop body, reached however update_dicts
    // returned.
    if (options.metacontig != nullptr && !pos.gene.empty())
      observed_contigs[pos.gene].insert(transcript);
  }

  // Drain: `for p in sorted(self.reads_dict.keys())`.
  std::vector<BundlePos> rest;
  for (const auto& [p, _] : reads_dict) { (void)_; rest.push_back(p); }
  flush(rest, /*drop_counts=*/true);
}

}  // namespace umi_tools
