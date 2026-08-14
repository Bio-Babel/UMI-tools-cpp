// Utilities.validateSamOptions and the small helpers that go with it.
//
// Extracted here rather than left inline in one tool because `count`, `group`
// and `dedup` all call it and its ORDER OF EFFECTS is observable: three paired
// warnings are emitted BEFORE the "command: ..." info line, and when
// --chimeric-pairs is present that same line is emitted a SECOND time. A
// re-implementation per tool would drift on exactly that kind of detail.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/pattern.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/sam_methods.hpp"

namespace umi_tools {
namespace {

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

// `if options.X:` on a string option — Python truthiness, so both None and ""
// are false.
std::string truthy_string(const Values& o, const char* key) {
  if (o.is_none(key)) return std::string();
  return o.get_string(key);
}

}  // namespace

PyRandom& global_random() {
  static PyRandom instance;
  return instance;
}

std::string get_temp_file(const std::string& dir, const std::string& suffix) {
  std::filesystem::path base =
      dir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(dir);
  // prefix "ctmp", as tempfile.NamedTemporaryFile is given upstream.
  std::string tmpl = (base / "ctmpXXXXXX").string() + suffix;
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  const int fd = ::mkstemps(buf.data(), static_cast<int>(suffix.size()));
  if (fd < 0) throw std::runtime_error("cannot create temp file in " + base.string());
  ::close(fd);
  return std::string(buf.data());
}

std::string get_temp_filename(const std::string& dir) {
  // getTempFilename == getTempFile(...).close(); return .name
  return get_temp_file(dir, "");
}

std::string format_fixed(double value, int precision) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
  return std::string(buf);
}

SamOptionsResult validate_sam_options(const Values& options, bool group,
                                      const std::string& command,
                                      bool per_gene_override) {
  SamOptionsResult r;
  Log& log = Log::instance();

  const bool per_gene = per_gene_override || options.get_bool("per_gene");
  const bool per_contig = options.get_bool("per_contig");
  const bool paired = options.get_bool("paired");
  const std::string gene_tag = truthy_string(options, "gene_tag");
  const std::string gene_transcript_map = truthy_string(options, "gene_transcript_map");

  r.unmapped_reads = options.has("unmapped_reads") ? options.get_string("unmapped_reads")
                                                   : std::string("discard");
  const std::string chimeric_pairs = options.has("chimeric_pairs")
                                         ? options.get_string("chimeric_pairs")
                                         : std::string("use");
  const std::string unpaired_reads = options.has("unpaired_reads")
                                         ? options.get_string("unpaired_reads")
                                         : std::string("use");

  auto fail = [&r](const char* msg) { if (!r.error) r.error = std::string(msg); };

  if (per_gene) {
    if (!gene_tag.empty() && per_contig)
      { fail("need to use either --per-contig OR --gene-tag, please do not provide both"); return r; }
    if (!per_contig && gene_tag.empty())
      { fail("for per-gene applications, must supply --per-contig or --gene-tag"); return r; }
  }
  if (per_contig && !per_gene) { fail("need to use --per-gene with --per-contig"); return r; }
  if (!gene_tag.empty() && !per_gene) { fail("need to use --per-gene with --gene_tag"); return r; }
  if (!gene_transcript_map.empty() && !per_contig)
    { fail("need to use --per-contig and --per-genewith --gene-transcript-map"); return r; }

  if (options.get_string("get_umi_method") == "tag") {
    if (options.is_none("umi_tag")) { fail("Need to supply the --umi-tag option"); return r; }
    if (options.get_bool("per_cell") && options.is_none("cell_tag"))
      { fail("Need to supply the --cell-tag option"); return r; }
  }

  // `if options.assigned_tag is None: options.assigned_tag = options.gene_tag`
  r.assigned_tag = options.is_none("assigned_tag") ? gene_tag
                                                   : options.get_string("assigned_tag");

  // `re.compile(options.skip_regex)` inside try/except re.error. The port's
  // SkipRegex covers the measured sublanguage and throws on anything outside it,
  // which is the same "this pattern is not usable" answer at the same point.
  const std::string skip_regex = truthy_string(options, "skip_regex");
  if (!skip_regex.empty()) {
    try {
      SkipRegex probe(skip_regex);
      (void)probe;
    } catch (const std::exception&) {
      r.error = "skip-regex '" + skip_regex + "' is not a valid regex";
      return r;
    }
  }

  if (!group) {
    if (r.unmapped_reads == "output")
      { fail("Cannot use --unmapped-reads=output. If you want to retain unmapped without "
             "deduplicating them, use the group command"); return r; }
    if (chimeric_pairs == "output")
      { fail("Cannot use --chimeric-pairs=output. If you want to retain chimeric read pairs "
             "without deduplicating them, use the group command"); return r; }
    if (unpaired_reads == "output")
      { fail("Cannot use --unpaired-reads=output. If you want to retain unmapped without "
             "deduplicating them, use the group command"); return r; }
  }

  if (paired) {
    if (chimeric_pairs == "use")
      log.warn("Chimeric read pairs are being used. Some read pair UMIs may be "
               "grouped/deduplicated using just the mapping coordinates from read1."
               "This may also increase the run time and memory usage. Consider "
               "--chimeric-pairs==discard to discard these reads or "
               "--chimeric-pairs==output (group command only) to output them without grouping");
    if (unpaired_reads == "use")
      log.warn("Unpaired read pairs are being used. Some read pair UMIs may be "
               "grouped/deduplicated using just the mapping coordinates from read1."
               "This may also increase the run time and memory usage. Consider "
               "--unpared-reads==discard to discard these reads or "
               "--unpared-reads==output (group command only) to output them without grouping");
    if (r.unmapped_reads == "use")
      log.warn("Unmapped read pairs are being used. Some read pair UMIs may be "
               "grouped/deduplicated using just the mapping coordinates from read1. "
               "This may also increase the run time and memory usage. Consider "
               "--unmapped_reads==discard to discard these reads or "
               "--unmapped_reads==output (group command only) to output them without grouping");
  }

  log.info("command: " + command);
  if (contains(command, "--umi-tag") || contains(command, "--cell-tag")) {
    if (options.get_string("get_umi_method") != "tag")
      { fail("--umi-tag and/or --cell-tag options provided. Need to set "
             "--extract-umi-method=tag"); return r; }
  }

  if (r.unmapped_reads == "use" && !paired)
    { fail("--unmapped-reads=use is only compatible with paired end reads (--paired)"); return r; }

  if (contains(command, "--chimeric-pairs")) {
    // Upstream logs the command line a SECOND time here. Reproduced.
    log.info("command: " + command);
    if (!paired)
      { fail("--chimeric-pairs is only compatible with paired end reads (--paired)"); return r; }
  }
  if (contains(command, "--unpaired-reads") && !paired)
    { fail("--unpaired-reads is only compatible with paired end reads (--paired)"); return r; }
  if (contains(command, "--ignore-tlen") && !paired)
    { fail("--ignore-tlen is only compatible with paired end reads (--paired)"); return r; }

  // Legacy --output-unmapped.
  if (options.get_bool("output_unmapped")) {
    log.warn("--output-unmapped will be removed in the near future. "
             "Use --unmapped-reads=output instead");
    // NOTE the underscores: upstream tests for "--unmapped_reads", which is not
    // the option's actual spelling ("--unmapped-reads"), so this guard never
    // fires in practice. Kept as-is; "fixing" it would change behaviour.
    if (contains(command, "--unmapped_reads"))
      { fail("Do not use --output-unmapped in combination with--unmapped-reads. "
             "Just use --unmapped-reads"); return r; }
    r.unmapped_reads = "output";
  }

  return r;
}

BundleOptions make_bundle_options(const Values& options, const SamOptionsResult& sam) {
  BundleOptions bo;
  bo.per_gene = options.get_bool("per_gene");
  bo.per_contig = options.get_bool("per_contig");
  bo.per_cell = options.get_bool("per_cell");
  bo.whole_contig = options.get_bool("whole_contig");
  bo.ignore_umi = options.get_bool("ignore_umi");
  bo.paired = options.get_bool("paired");
  bo.spliced = options.get_bool("spliced");
  bo.read_length = options.get_bool("read_length");
  bo.ignore_tlen = options.get_bool("ignore_tlen");
  bo.gene_tag = truthy_string(options, "gene_tag");
  bo.assigned_tag = sam.assigned_tag;
  if (!options.is_none("skip_regex")) bo.skip_regex = options.get_string("skip_regex");
  bo.mapping_quality = options.get_int("mapping_quality");
  bo.soft_clip_threshold = options.get_float("soft_clip_threshold");
  if (!options.is_none("detection_method"))
    bo.detection_method = options.get_string("detection_method");
  const std::string umi_method = options.get_string("get_umi_method");
  bo.get_umi_method = umi_method == "tag"    ? UmiMethod::Tag
                      : umi_method == "umis" ? UmiMethod::Umis
                                             : UmiMethod::ReadId;
  bo.umi_sep = options.is_none("umi_sep") ? "_" : options.get_string("umi_sep");
  if (!options.is_none("umi_tag")) bo.tag_options.umi_tag = options.get_string("umi_tag");
  if (!options.is_none("cell_tag")) bo.tag_options.cell_tag = options.get_string("cell_tag");
  if (!options.is_none("umi_tag_split"))
    bo.tag_options.umi_tag_split = options.get_string("umi_tag_split");
  if (!options.is_none("umi_tag_delim"))
    bo.tag_options.umi_tag_delim = options.get_string("umi_tag_delim");
  if (!options.is_none("cell_tag_split"))
    bo.tag_options.cell_tag_split = options.get_string("cell_tag_split");
  if (!options.is_none("cell_tag_delim"))
    bo.tag_options.cell_tag_delim = options.get_string("cell_tag_delim");
  bo.unmapped_reads = sam.unmapped_reads;
  if (options.has("chimeric_pairs")) bo.chimeric_pairs = options.get_string("chimeric_pairs");
  if (options.has("unpaired_reads")) bo.unpaired_reads = options.get_string("unpaired_reads");
  if (!options.is_none("chrom")) bo.chrom = options.get_string("chrom");
  return bo;
}

void touch_start_output(const std::string& name) {
  // Only the EMPTY string means "not supplied" — callers pass that when
  // was_given() is false. `-` is a real filename: `dedup -S -` writes its BAM to
  // stdout (pysam's own "-" convention) AND leaves a zero-length file named `-`
  // behind, because Start opened options.stdout by NAME. MEASURED: oracle
  // dashfile=0B with 62,476 bytes on stdout; the port left no `-` file.
  if (name.empty()) return;
  // mode 'w': create or TRUNCATE, exactly as Utilities.Start does. Failure is
  // silent here on purpose — upstream's open would raise, and every caller of
  // this reaches its own open of the same path moments later, which is where
  // the port already reports the error with the right message and status.
  if (std::FILE* f = std::fopen(name.c_str(), "w")) std::fclose(f);
}

}  // namespace umi_tools
