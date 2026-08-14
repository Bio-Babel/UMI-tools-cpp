// Slice 6 cases: the alignment OUTPUT layer — AlignmentWriter, sort_output,
// determine_format, get_temp_filename.
//
// validation/parity_output.py compares determine_format and
// output_names_and_formats against the live oracle over all 120 branch
// combinations, and the 16 group fixtures compare the written+sorted BAM/SAM
// byte-for-byte. These cases pin the parts NEITHER of those can see:
//
//   * sort_output DELETES its input (upstream's `os.unlink(infile)`). The temp
//     file lives in the system temp dir, so a fixture diff of the test
//     directory is blind to it leaking.
//   * the stable tie order, on a shuffled input built here — the shipped
//     corpus is already coordinate-sorted, so a fixture cannot distinguish
//     std::sort from std::stable_sort.
//   * unmapped (tid == -1) records sorting LAST.
#include "umi_tools/alignment.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"
#include "test_harness.hpp"

#include <htslib/sam.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace umi_tools;

namespace {

const char* kBam = UMI_TOOLS_FIXTURES_DIR "/chr19.bam";
// chr19_1mb.bam is the ONLY shipped input with no @HD line, and it is the input
// to all three CRAM fixtures.
const char* kBamNoHD = UMI_TOOLS_FIXTURES_DIR "/chr19_1mb.bam";
// The only shipped input that puts BOTH STRANDS on one coordinate often enough to
// pin samtools' sort key. chr19.bam has none in its first 400 records, so the
// sort tests below were blind to the is_reverse term until this was used.
const char* kBamMixedStrand = UMI_TOOLS_FIXTURES_DIR "/whitelist_umi_input.bam";

std::string header_text(const std::string& path) {
  AlignmentReader r(path);
  const char* s = sam_hdr_str(r.header());
  return s == nullptr ? std::string() : std::string(s);
}

std::string hd_line(const std::string& text) {
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t nl = text.find('\n', start);
    const std::string line = text.substr(start, nl == std::string::npos ? nl : nl - start);
    if (line.rfind("@HD", 0) == 0) return line;
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return std::string();
}

// Read the first `n` records of the shipped BAM and write them out in a
// deliberately scrambled order, so the tie behaviour is observable.
// An identity for a record that survives a BAM round trip.
std::string ident(const BamRecord& r) {
  return std::string(r.query_name()) + "/" + std::to_string(r.flag()) + "/" +
         std::to_string(r.pos());
}

// Read the first `n` records of the shipped BAM and write them out REVERSED, so
// every tie group is in the exact opposite of coordinate order. Returns the
// written order, which is what a stable sort must preserve within each group.
std::vector<std::string> make_shuffled_copy(const std::string& dest, int n,
                                            const char* source = kBam) {
  AlignmentReader in(source);
  std::vector<BamRecord> recs;
  BamRecord r;
  while (recs.size() < static_cast<std::size_t>(n) && in.next(r)) recs.push_back(r.clone());
  std::vector<std::string> order;
  {
    AlignmentWriter w(dest, "bam", in.header());
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
      w.write(*it);
      order.push_back(ident(*it));
    }
  }
  return order;
}

std::string temp_path(const char* stem) {
  return (std::filesystem::temp_directory_path() /
          ("umi_tools_test_" + std::string(stem) + ".bam")).string();
}

}  // namespace

UMI_TEST_CASE(sort_output_deletes_its_input) {
  const std::string src = temp_path("del_in");
  const std::string dst = temp_path("del_out");
  make_shuffled_copy(src, 50);
  CHECK(std::filesystem::exists(src));

  sort_output(src, dst, "bam", "");

  // `os.unlink(infile)  # delete the tempfile`
  CHECK(!std::filesystem::exists(src));
  CHECK(std::filesystem::exists(dst));
  std::filesystem::remove(dst);
}

UMI_TEST_CASE(sort_output_keeps_remove_input_false_when_asked) {
  const std::string src = temp_path("keep_in");
  const std::string dst = temp_path("keep_out");
  make_shuffled_copy(src, 20);

  sort_output(src, dst, "bam", "", /*remove_input=*/false);

  CHECK(std::filesystem::exists(src));
  std::filesystem::remove(src);
  std::filesystem::remove(dst);
}

UMI_TEST_CASE(sort_output_is_a_stable_sort_not_just_a_sort) {
  const std::string src = temp_path("stable_in");
  const std::string dst = temp_path("stable_out");
  // NOT chr19.bam: it has ZERO coordinates carrying both strands in its first
  // 400 records (measured), so it cannot distinguish a (tid, pos) comparator from
  // samtools' (tid, pos, is_reverse) one — the reason the missing strand term
  // matched every golden. whitelist_umi_input.bam at 2000 records carries 20 such
  // coordinates, and the CHECKs below fail if that number ever reaches zero.
  const std::vector<std::string> input_order =
      make_shuffled_copy(src, 2000, kBamMixedStrand);

  sort_output(src, dst, "bam", "");

  // Position of each record in the INPUT, so "did the tie group keep its input
  // order?" becomes "are the input indices increasing within the group?".
  //
  // Only records whose ident is UNIQUE in the input can answer that. This BAM
  // carries secondary alignments, so 215 of its 1785 idents occur more than once
  // (same query name, same flag, same pos) — and for two records that are
  // indistinguishable, "did they keep their order" has no observable answer, so
  // asserting one reports a failure the sort did not commit. Measured: including
  // them fails this check on a correct sort.
  std::map<std::string, int> occurrences;
  for (const std::string& id : input_order) ++occurrences[id];
  auto input_index = [&input_order](const std::string& id) -> long {
    for (std::size_t i = 0; i < input_order.size(); ++i)
      if (input_order[i] == id) return static_cast<long>(i);
    return -1;
  };

  AlignmentReader out(dst);
  BamRecord r;
  std::int64_t last_tid = -1, last_pos = -1;
  int last_rev = -1;
  long last_unique_index = -1;
  bool last_was_unique = false;
  std::size_t n = 0, ties_checked = 0, strand_ties = 0;
  while (out.next(r)) {
    const std::int64_t tid = r.tid(), pos = r.pos();
    const int rev = r.is_reverse() ? 1 : 0;
    // 1. globally ordered by (tid, pos, is_reverse) — samtools' key. The strand
    //    term is not a refinement of the tie-break, it is PART OF THE ORDER:
    //    forward precedes reverse at one coordinate whatever the input order was.
    //    This half needs no unique ident and so covers all 2000 records.
    CHECK(tid > last_tid ||
          (tid == last_tid && pos > last_pos) ||
          (tid == last_tid && pos == last_pos && rev >= last_rev));
    const std::string id = ident(r);
    const long idx = input_index(id);
    CHECK(idx >= 0);
    const bool unique = occurrences[id] == 1;
    // 2. WITHIN a full-key tie group, still in input order — this is the whole
    //    point: std::sort would be free to permute these and check 1 would still
    //    pass. Note the group is (tid, pos, is_reverse), not (tid, pos): two
    //    records at one coordinate on opposite strands are ORDERED, not tied, so
    //    requiring input order across them would assert something false.
    const bool same_group = (tid == last_tid && pos == last_pos && rev == last_rev);
    if (same_group && unique && last_was_unique) {
      CHECK(idx > last_unique_index);
      ++ties_checked;
    }
    if (tid == last_tid && pos == last_pos && rev != last_rev) ++strand_ties;
    if (!same_group) last_was_unique = false;
    if (unique) {
      last_unique_index = idx;
      last_was_unique = true;
    }
    last_tid = tid;
    last_pos = pos;
    last_rev = rev;
    ++n;
  }
  CHECK(n == 2000);
  // Both assertions are vacuous on a sample that never exercises them, so require
  // that each was reached: some equal-key ties (for stability) and some
  // same-coordinate strand transitions (for the strand term). The second is what
  // no shipped fixture provided, which is why a (tid, pos)-only comparator
  // matched every golden for thirty sessions.
  CHECK(ties_checked > 0);
  CHECK(strand_ties > 0);
  std::filesystem::remove(dst);
}

UMI_TEST_CASE(sort_output_adds_an_HD_line_when_the_input_has_none) {
  // MEASURED against the oracle: samtools sort CREATES `@HD VN:1.6 SO:coordinate`
  // when the input header has no @HD. htslib's sam_hdr_update_hd only UPDATES an
  // existing line and is a silent no-op otherwise — which produced a CRAM whose
  // records matched the oracle's exactly while the header was missing a line.
  // No `group` fixture catches this: every one of their inputs already has @HD.
  CHECK_EQ(hd_line(header_text(kBamNoHD)), "");   // the premise

  const std::string src = temp_path("hd_add_in");
  const std::string dst = temp_path("hd_add_out");
  {
    AlignmentReader in(kBamNoHD);
    AlignmentWriter w(src, "bam", in.header());
    BamRecord r;
    int n = 0;
    while (n++ < 20 && in.next(r)) w.write(r);
  }
  sort_output(src, dst, "bam", "");
  CHECK_EQ(hd_line(header_text(dst)), "@HD\tVN:1.6\tSO:coordinate");
  std::filesystem::remove(dst);
}

UMI_TEST_CASE(sort_output_preserves_an_existing_HD_version) {
  // chr19.bam is VN:1.0. samtools keeps the version and only sets SO — which is
  // why `group_directional` passes with a VN:1.0 header rather than VN:1.6.
  CHECK_EQ(hd_line(header_text(kBam)), "@HD\tVN:1.0\tSO:coordinate");

  const std::string src = temp_path("hd_keep_in");
  const std::string dst = temp_path("hd_keep_out");
  make_shuffled_copy(src, 20);
  sort_output(src, dst, "bam", "");
  CHECK_EQ(hd_line(header_text(dst)), "@HD\tVN:1.0\tSO:coordinate");
  std::filesystem::remove(dst);
}

UMI_TEST_CASE(determine_format_matches_the_measured_table) {
  // The rows below are the oracle's answers, recorded by
  // validation/parity_output.py (120/120 exact). Repeated here so a change to
  // the C++ fails a unit test and not only the Python harness.
  CHECK_EQ(determine_format("out.bam", false, ""), "bam");
  CHECK_EQ(determine_format("out.sam", false, ""), "sam");
  CHECK_EQ(determine_format("out.cram", false, ""), "cram");
  CHECK_EQ(determine_format("-", false, ""), "bam");
  // `filename.lower().endswith(...)` — the extension test is case-INSENSITIVE.
  CHECK_EQ(determine_format("out.SAM", false, ""), "sam");
  CHECK_EQ(determine_format("out.CRAM", false, ""), "cram");
  // sam outranks everything; out_format outranks the extension.
  CHECK_EQ(determine_format("out.cram", true, "bam"), "sam");
  CHECK_EQ(determine_format("out.cram", false, "bam"), "bam");
  // Only a real trailing extension counts.
  CHECK_EQ(determine_format("a.sam.bam", false, ""), "bam");
  CHECK_EQ(determine_format("weird.cram.txt", false, ""), "bam");
}

UMI_TEST_CASE(get_temp_filename_is_unique_and_created) {
  const std::string a = get_temp_filename("");
  const std::string b = get_temp_filename("");
  CHECK(a != b);
  // tempfile.NamedTemporaryFile(...).close() leaves the file on disk; upstream
  // then opens it by name for writing, so it must exist.
  CHECK(std::filesystem::exists(a));
  CHECK(std::filesystem::exists(b));
  std::filesystem::remove(a);
  std::filesystem::remove(b);
}

UMI_TEST_CASE(get_temp_file_uses_the_ctmp_prefix_and_survives_close) {
  // tempfile.NamedTemporaryFile(dir=..., delete=False, prefix="ctmp", suffix=...)
  // The prefix is not observable in any output, but delete=False IS: upstream
  // closes the handle and then reopens the file BY NAME, so it must still exist.
  const std::string a = get_temp_file("", "");
  const std::string b = get_temp_file("", ".bam");
  CHECK(a != b);
  CHECK(std::filesystem::exists(a));
  CHECK(std::filesystem::exists(b));
  CHECK(std::filesystem::path(a).filename().string().rfind("ctmp", 0) == 0);
  CHECK(b.size() > 4 && b.substr(b.size() - 4) == ".bam");
  std::filesystem::remove(a);
  std::filesystem::remove(b);
}

UMI_TEST_CASE(default_options_match_the_measured_python_values) {
  // Utilities.DefaultOptions, read off the live oracle:
  //   loglevel = 2, compresslevel = 6, and the four streams are the process's.
  CHECK_EQ(DefaultOptions::kLogLevel, 2);
  CHECK_EQ(DefaultOptions::kCompressLevel, 6);
  CHECK_EQ(std::string(DefaultOptions::kStdout), "-");
  CHECK_EQ(std::string(DefaultOptions::kStdlog), "-");
  CHECK_EQ(std::string(DefaultOptions::kStdin), "-");
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
