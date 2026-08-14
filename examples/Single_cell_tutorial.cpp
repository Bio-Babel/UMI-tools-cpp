// Single_cell_tutorial.cpp — doc/Single_cell_tutorial.md through the C++ LIBRARY.
//
// As with QUICK_START.cpp this drives umi_tools_core directly rather than
// shelling out, so it is both a usable library example and real ported code for
// the sanitizer to run over.
//
// The tutorial has six steps; three are umi_tools and are reproduced here:
//
//   Step 2  whitelist  — find the true cell barcodes (--set-cell-number=100)
//   Step 3  extract    — move CB+UMI into the read name, filtered by that list
//   Step 6  count      — UMIs per gene per cell
//
// Steps 4 and 5 are STAR alignment and featureCounts. They are not part of what
// is being ported and need a combined human+mouse index; 03_data_assets.md
// scoped them out before any of this was written. Step 6 therefore takes a
// gene-tagged BAM as an argument — the tutorial would hand it
// `assigned_sorted.bam`, and the validation script hands it a real BAM whose
// XF/XS tags have the shape featureCounts emits. THAT SUBSTITUTION IS STATED,
// here and in 10_validation.md, rather than being quietly swapped in.
//
// Usage:
//   example_Single_cell_tutorial <R1.fastq.gz> <R2.fastq.gz> <gene_tagged.bam> <out-dir>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/extract_methods.hpp"
#include "umi_tools/fastq.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/knee.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/sam_methods.hpp"
#include "umi_tools/whitelist_methods.hpp"

using namespace umi_tools;

namespace {

// 10x Chromium v2: 16 bases of cell barcode then 10 of UMI.
constexpr const char* kPattern = "CCCCCCCCCCCCCCCCNNNNNNNNNN";

// Step 2:
//   umi_tools whitelist --stdin R1 --bc-pattern=CCCC...NNNN --set-cell-number=100
//
// --set-cell-number bypasses the knee estimate entirely: the barcodes are ranked
// by count and the top N are taken.
std::vector<std::string> step2_whitelist(const std::string& r1,
                                         const std::string& out_path,
                                         std::int64_t cell_number) {
  ExtractFilterOptions opt;
  opt.method = ExtractMethod::String;
  opt.extract_cell = true;
  ExtractFilterAndUpdate extractor(opt, kPattern, "");

  LineReader in(r1);
  FastqIterator reads(in, /*ignore_suffix=*/false);

  // cell barcode -> observed count, in first-seen order (the order is
  // observable: it breaks ties in the ranking below).
  OrderedMap<std::string, std::int64_t> counts;
  std::int64_t n = 0;
  while (auto read = reads.next()) {
    // (cell, umi) for the whitelist pass; only the cell is counted here.
    if (auto bc = extractor.get_barcodes_for_whitelist(*read, nullptr))
      counts[bc->first] += 1;
    ++n;
  }
  std::cout << "  read " << n << " reads, " << counts.size()
            << " distinct cell barcodes\n";

  // knee_method is irrelevant when --set-cell-number is given: the estimator is
  // bypassed and the top N by count are taken.
  const auto cw = get_cell_whitelist(counts, "distance", /*expect_cells=*/std::nullopt,
                                     /*cell_number=*/cell_number,
                                     /*error_correct_threshold=*/1);
  if (!cw.whitelist) throw std::runtime_error("no cell whitelist was selected");
  // `for barcode in sorted(list(cell_whitelist))` — the FILE is sorted even
  // though the whitelist itself carries selection order.
  std::vector<std::string> whitelist(cw.whitelist->begin(), cw.whitelist->end());
  std::sort(whitelist.begin(), whitelist.end());

  // whitelist.txt is a FOUR-COLUMN TSV, not a bare list:
  //   barcode \t comma-joined error barcodes \t count \t comma-joined error counts
  // The first version of this example wrote only the barcode, which "worked"
  // but was not the file the tool produces — an example that documents a format
  // the library does not emit is worse than no example.
  Writer out(out_path);
  for (const std::string& bc : whitelist) {
    std::string errs, err_counts;
    if (cw.has_true_to_false) {
      auto it = cw.true_to_false_map.find(bc);
      if (it != cw.true_to_false_map.end()) {
        bool first = true;
        for (const std::string& fb : it->second) {   // std::set == Python's sorted()
          if (!first) { errs += ','; err_counts += ','; }
          first = false;
          errs += fb;
          err_counts += std::to_string(counts.get(fb, 0));
        }
      }
    }
    out.write(bc + "\t" + errs + "\t" + std::to_string(counts.get(bc, 0)) + "\t" +
              err_counts + "\n");
  }
  out.close();
  return whitelist;
}

// Step 3:
//   umi_tools extract --bc-pattern=... --stdin R1 --read2-in R2
//                     --whitelist=whitelist.txt
std::int64_t step3_extract(const std::string& r1, const std::string& r2,
                           const std::vector<std::string>& whitelist,
                           const std::string& out1, const std::string& out2) {
  ExtractFilterOptions opt;
  opt.method = ExtractMethod::String;
  opt.extract_cell = true;
  opt.filter_cell_barcode = true;
  ExtractFilterAndUpdate extractor(opt, kPattern, "");
  extractor.set_cell_whitelist(std::set<std::string>(whitelist.begin(), whitelist.end()));

  LineReader in1(r1), in2(r2);
  FastqIterator reads1(in1, false), reads2(in2, false);
  Writer w1(out1), w2(out2);

  std::int64_t kept = 0;
  joined_fastq_iterate(reads1, reads2, /*strict=*/true, [&](Record& a, Record& b) {
    if (auto out = extractor(a, &b)) {
      w1.write(out->read1.str() + "\n");
      w2.write(out->read2.str() + "\n");
      ++kept;
    }
    return true;
  });
  w1.close();
  w2.close();
  return kept;
}

// Step 6:
//   umi_tools count --per-gene --gene-tag=XF --assigned-status-tag=XS --per-cell
std::int64_t step6_count(const std::string& bam, const std::string& out_path) {
  AlignmentReader reader(bam);

  BundleOptions bo;
  bo.per_gene = true;
  bo.per_cell = true;
  bo.gene_tag = "XF";
  bo.assigned_tag = "XS";
  bo.get_umi_method = UmiMethod::ReadId;
  bo.umi_sep = "_";

  UMIClusterer clusterer(ClusterMethod::Directional);
  // gene -> cell -> count. Both levels sorted, as count.py's long form emits.
  std::map<std::string, std::map<std::string, std::int64_t>> table;
  BundleReadEvents events;

  for_each_bundle_per_gene(
      reader, bo, /*only_count_reads=*/true, events,
      [&](const Bundle& bundle, const BundleKey& key) {
        UmiCounts counts;
        for (const auto& [umi, entry] : bundle) counts[umi] = entry.count;
        table[key.pos][key.cell ? *key.cell : std::string()] =
            static_cast<std::int64_t>(clusterer(counts, /*threshold=*/1).size());
      });

  Writer out(out_path);
  out.write("gene\tcell\tcount\n");
  std::int64_t rows = 0;
  for (const auto& [gene, cells] : table)
    for (const auto& [cell, n] : cells) {
      out.write(gene + "\t" + cell + "\t" + std::to_string(n) + "\n");
      ++rows;
    }
  out.close();
  return rows;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: " << argv[0]
              << " <R1.fastq.gz> <R2.fastq.gz> <gene_tagged.bam> <out-dir>\n";
    return 2;
  }
  const std::string r1 = argv[1], r2 = argv[2], bam = argv[3];
  const std::filesystem::path out = argv[4];
  std::filesystem::create_directories(out);

  try {
    std::cout << "Single_cell step 2: whitelist\n";
    const auto wl = step2_whitelist(r1, (out / "whitelist.txt").string(), 100);
    std::cout << "  whitelisted " << wl.size() << " cell barcodes\n";

    std::cout << "Single_cell step 3: extract\n";
    const std::int64_t kept = step3_extract(
        r1, r2, wl, (out / "hgmm_100_R1_extracted.fastq.gz").string(),
        (out / "hgmm_100_R2_extracted.fastq.gz").string());
    std::cout << "  kept " << kept << " read pairs\n";

    std::cout << "Single_cell step 6: count (on the substituted gene-tagged BAM)\n";
    const std::int64_t rows = step6_count(bam, (out / "counts.tsv").string());
    std::cout << "  wrote " << rows << " gene/cell rows\n";
    return 0;
  } catch (const umi_tools::ExitRequest& e) {
  // ExitRequest is deliberately NOT a std::exception
  // (logging.hpp:160) so that only a deliberate handler exits on it, and it is
  // thrown rather than calling std::exit so unwinding closes the gzip/BGZF
  // writers first. Before this catch, main.cpp was the ONLY handler: an
  // error_exit reached from library code here escaped main uncaught ->
  // std::terminate -> SIGABRT (rc 134), and gcc does not unwind on the way
  // there, so the BGZF EOF block ExitRequest exists to guarantee was exactly
  // what got lost. MEASURED: `rand_reads <unindexed.bam> 1 5` through the
  // example gave rc=134.
    std::cerr << e.message;
    return e.code;
  } catch (const std::exception& e) {
    std::cerr << "failed: " << e.what() << "\n";
    return 1;
  }
}
