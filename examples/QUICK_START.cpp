// QUICK_START.cpp — doc/QUICK_START.md reproduced through the C++ LIBRARY.
//
// This is deliberately NOT a wrapper that shells out to the `umi_tools` binary.
// An example that ran system("umi_tools extract ...") would demonstrate nothing
// about the library and, for step 9, would put no new line of ported code under
// the sanitizer. Everything here goes through umi_tools_core directly, which is
// also the honest answer to "how do I use this as a library?".
//
// The tutorial's pipeline is:
//     extract UMI from raw reads -> map reads -> deduplicate reads on UMIs
// Step 4 is `bowtie`, which is not part of the port; the tutorial itself ships a
// pre-mapped BAM so readers can skip it, and so does this.
//
// Usage:
//     example_QUICK_START <example.fastq.gz> <example.bam> <out-dir>
//
// Writes the same two artifacts the tutorial does — processed.fastq.gz and
// deduplicated.bam — so validation/tutorial_QUICK_START.py can diff them against
// the live Python.
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/dedup_stats.hpp"
#include "umi_tools/extract_methods.hpp"
#include "umi_tools/fastq.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/sam_methods.hpp"

using namespace umi_tools;

namespace {

// Step 3 of the tutorial:
//   umi_tools extract --stdin=example.fastq.gz --bc-pattern=NNNNNNNNN
//                     --stdout processed.fastq.gz
//
// `NNNNNNNNN` is nine Ns: the whole barcode is random, so the UMI is the first
// nine bases and nothing is left behind as a library barcode.
std::int64_t step3_extract(const std::string& in_fastq, const std::string& out_fastq) {
  ExtractFilterOptions opt;
  opt.method = ExtractMethod::String;
  opt.extract_cell = false;          // no Cs in the pattern
  ExtractFilterAndUpdate extractor(opt, "NNNNNNNNN", "");

  LineReader in(in_fastq);
  FastqIterator reads(in, /*ignore_suffix=*/false);
  Writer out(out_fastq);

  std::int64_t n = 0;
  while (auto read = reads.next()) {
    // operator() returns nullopt for a read the filters reject; with no quality
    // or whitelist filtering configured, that only happens for a short read.
    if (auto extracted = extractor(*read)) {
      out.write(extracted->read1.str() + "\n");
      ++n;
    }
  }
  out.close();
  return n;
}

// Step 5:
//   umi_tools dedup -I example.bam -S deduplicated.bam
//
// The library equivalent is get_bundles + UMIClusterer: bundle the reads by
// position/strand/UMI, cluster the UMIs of each bundle, and keep one read per
// cluster. The seed matters — dedup's tie-break draws from the Python `random`
// stream, and without a seed the OUTPUT IS NOT REPRODUCIBLE, not even
// oracle-against-oracle (measured; see the tutorial script).
std::int64_t step5_dedup(const std::string& in_bam, const std::string& out_bam,
                         const std::string& stats_prefix, std::int64_t seed) {
  AlignmentReader reader(in_bam);

  BundleOptions bo;
  bo.get_umi_method = UmiMethod::ReadId;   // the UMI is in the read name
  bo.umi_sep = "_";

  PyRandom& rng = global_random();
  rng.seed(seed);

  UMIClusterer clusterer(ClusterMethod::Directional);

  // dedup sorts its output, so it writes to a temp file and sorts afterwards,
  // exactly as Utilities.output_names_and_formats arranges.
  // --output-stats. The tutorial does not treat this as optional decoration —
  // it PRINTS deduplicated_edit_distance.tsv and spends a page explaining it, so
  // an example that emits only the BAM does not reproduce the narrative.
  //
  // The null columns of that table come from random_read_generator, which draws
  // from NUMPY's stream — a different generator from the stdlib one the
  // tie-break uses, seeded separately, exactly as dedup.py does it.
  DedupStats stats;
  PyRandom numpy_rng;
  numpy_rng.seed_numpy(seed);
  RandomReadGenerator read_gn(in_bam, /*reference=*/"", /*chrom=*/"", bo, numpy_rng);

  const std::string tmp = get_temp_filename("");
  std::int64_t n_in = 0, n_out = 0;
  {
    AlignmentWriter writer(tmp, "bam", reader.header());
    BundleReadEvents events;
    for_each_bundle(
        reader, bo, /*only_count_reads=*/false, /*all_reads=*/false,
        /*return_read2=*/false, /*return_unmapped=*/false, rng,
        /*subset=*/std::nullopt, events,
        [&](const Bundle& bundle, const BundlePos&, const PositionalKey&) {
          UmiCounts counts;
          std::vector<Bytes> bundle_umis;
          for (const auto& [umi, entry] : bundle) {
            counts[umi] = entry.count;
            bundle_umis.push_back(umi);
            n_in += entry.count;
            stats.pre[umi].push_back(entry.count);
          }
          const auto clusters = clusterer(counts, /*threshold=*/1);

          std::vector<Bytes> post_umis;
          for (const auto& cluster : clusters) {
            const Bytes& top = cluster[0];
            writer.write(bundle.at(top).reads.at(0));
            ++n_out;
            std::int64_t cluster_count = 0;
            for (const Bytes& u : cluster) cluster_count += counts.at(u);
            stats.post[top].push_back(cluster_count);
            post_umis.push_back(top);
          }

          // Observed vs null average edit distance, per bundle — the two pairs
          // of columns in the table the tutorial reproduces.
          stats.pre_cluster.push_back(get_average_umi_distance(bundle_umis));
          stats.pre_cluster_null.push_back(get_average_umi_distance(
              read_gn.get_umis(static_cast<std::int64_t>(bundle.size()))));
          stats.post_cluster.push_back(get_average_umi_distance(post_umis));
          stats.post_cluster_null.push_back(get_average_umi_distance(
              read_gn.get_umis(static_cast<std::int64_t>(post_umis.size()))));
        },
        [](BamRecord&) {});
  }
  // <prefix>_per_umi_per_position.tsv, _per_umi.tsv and _edit_distance.tsv.
  write_stats(stats, stats_prefix, "directional");
  sort_output(tmp, out_bam, "bam", "");
  std::cout << "  bundled " << n_in << " reads\n";
  return n_out;
}

// Common variations -> "Read grouping":
//   umi_tools group -I mapped.bam --group-out=groups.tsv --output-bam
//                   -S mapped_grouped.bam
//
// Marks duplicates while RETAINING every read — for consensus calling, where
// throwing the duplicates away is exactly what you must not do. The output BAM
// carries UG (group id) and BX (group UMI); the flatfile has the nine columns
// the tutorial lists.
std::int64_t variation_group(const std::string& in_bam, const std::string& out_bam,
                             const std::string& groups_tsv, std::int64_t seed) {
  AlignmentReader reader(in_bam);

  BundleOptions bo;
  bo.get_umi_method = UmiMethod::ReadId;
  bo.umi_sep = "_";

  PyRandom& rng = global_random();
  rng.seed(seed);
  UMIClusterer clusterer(ClusterMethod::Directional);

  Writer tsv(groups_tsv);
  tsv.write("read_id\tcontig\tposition\tgene\tumi\tumi_count\t"
            "final_umi\tfinal_umi_count\tunique_id\n");

  const std::string tmp = get_temp_filename("");
  std::int64_t unique_id = 0, n_out = 0;
  {
    AlignmentWriter writer(tmp, "bam", reader.header());
    BundleReadEvents events;
    for_each_bundle(
        reader, bo, /*only_count_reads=*/false, /*all_reads=*/true,
        /*return_read2=*/true, /*return_unmapped=*/false, rng,
        /*subset=*/std::nullopt, events,
        [&](const Bundle& bundle, const BundlePos&, const PositionalKey&) {
          UmiCounts counts;
          for (const auto& [umi, entry] : bundle) counts[umi] = entry.count;

          for (const auto& cluster : clusterer(counts, /*threshold=*/1)) {
            const Bytes& top = cluster[0];
            std::int64_t group_count = 0;
            for (const Bytes& u : cluster) group_count += counts.at(u);

            for (const Bytes& umi : cluster) {
              // all_reads=true, so every read of the umi is retained.
              for (const BamRecord& cr : bundle.at(umi).reads) {
                BamRecord& read = const_cast<BamRecord&>(cr);
                read.set_tag_int("UG", unique_id);
                read.set_tag_str("BX", top);
                writer.write(read);
                tsv.write(std::string(read.query_name()) + "\t" +
                          std::string(reader.target_name(read.tid())) + "\t" +
                          std::to_string(get_read_position(read, 4).pos) + "\tNA\t" +
                          umi + "\t" + std::to_string(counts.at(umi)) + "\t" +
                          top + "\t" + std::to_string(group_count) + "\t" +
                          std::to_string(unique_id) + "\n");
                ++n_out;
              }
            }
            ++unique_id;
          }
        },
        [&](BamRecord& r) { writer.write(r); ++n_out; });
  }
  tsv.close();
  sort_output(tmp, out_bam, "bam", "");
  return n_out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: " << argv[0]
              << " <example.fastq.gz> <example.bam> <out-dir>\n";
    return 2;
  }
  const std::string fastq = argv[1], bam = argv[2];
  const std::filesystem::path out = argv[3];
  std::filesystem::create_directories(out);

  try {
    std::cout << "QUICK_START step 3: extract\n";
    const std::int64_t n3 = step3_extract(fastq, (out / "processed.fastq.gz").string());
    std::cout << "  wrote " << n3 << " reads to processed.fastq.gz\n";

    std::cout << "QUICK_START step 5: dedup\n";
    const std::int64_t n5 =
        step5_dedup(bam, (out / "deduplicated.bam").string(),
                    (out / "deduplicated").string(), 123456789);
    std::cout << "  wrote " << n5 << " deduplicated reads to deduplicated.bam\n"
              << "  wrote deduplicated_{per_umi_per_position,per_umi,edit_distance}.tsv\n";

    // --- Common variations: read grouping ---------------------------------
    // The tutorial's own "Read grouping" section, which documents the nine
    // columns of the groups flatfile and the two tags added to the BAM. It is
    // part of the narrative, not an appendix, so the example produces it.
    std::cout << "QUICK_START variation: group\n";
    const std::int64_t ng =
        variation_group(bam, (out / "mapped_grouped.bam").string(),
                        (out / "groups.tsv").string(), 123456789);
    std::cout << "  wrote " << ng << " grouped reads, plus groups.tsv\n";

    // The tutorial's other variation, paired-end, is CONDITIONAL guidance
    // ("If paired-end sequencing has been performed") and this dataset is
    // single-end iCLIP. Demonstrating it here would need a different file, so
    // it is named rather than faked; the paired path itself is covered by the
    // shipped dedup_paired_* / group_paired_* fixtures.
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
