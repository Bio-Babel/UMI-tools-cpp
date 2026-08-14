// GENERATED FILE — do not edit by hand.
//
// Regenerate with:
//   $ORACLE_PY validation/extract_option_spec.py -o validation/option_spec.json
//   $ORACLE_PY validation/generate_option_tables.py validation/option_spec.json
//       -o src/option_tables.cpp
//
// The contents are the option table of umi_tools' own optparse parsers, read out
// of the LIVE oracle by executing each tool's main() with Utilities.Start
// intercepted (return_parser=True). Help strings, defaults, metavars, choices
// and — critically — the group ORDER and per-tool group INCLUSION are therefore
// exactly what the Python builds, not a hand transcription of 577 lines of
// conditional add_option calls.
//
// The FORMATTING and PARSING logic is hand-ported C++ in src/options.cpp; this
// file only supplies the table it operates on.
#include "umi_tools/options.hpp"

namespace umi_tools {
namespace {

// ---------- whitelist ----------
const ExtraDefault kExtra_whitelist[] = {
  {"blacklist_tsv", "none", true},
  {"whitelist_tsv", "none", true},
};

const OptionSpec kTop_whitelist[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ed-above-threshold"}, 1}, OptAction::Store, OptType::Choice, "ed_above_threshold", "none", true, "Detect CBs above the threshold which may be sequence errors from another CB and either 'discard' or 'correct'. Default=None (No correction)", nullptr, {{"discard", "correct"}, 2}, true},
};

const OptionSpec kG0_whitelist[] = {
  {{{}, 0}, {{"--plot-prefix"}, 1}, OptAction::Store, OptType::String, "plot_prefix", "none", true, "Prefix for plots to visualise the automated detection of the number of 'true' cell barcodes", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--subset-reads"}, 1}, OptAction::Store, OptType::Int, "subset_reads", "100000000", false, "Use the first N reads to automatically identify the true cell barcodes. If N is greater than the number of reads, all reads will be used. Default is 100,000,000", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--error-correct-threshold"}, 1}, OptAction::Store, OptType::Int, "error_correct_threshold", "1", false, "Hamming distance for correction of barcodes to whitelist barcodes. This value will also be used for error detection above the knee if required (--ed-above-threshold)", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--method"}, 1}, OptAction::Store, OptType::Choice, "method", "reads", false, "Use reads or unique umi counts per cell", nullptr, {{"reads", "umis"}, 2}, true},
  {{{}, 0}, {{"--knee-method"}, 1}, OptAction::Store, OptType::Choice, "knee_method", "distance", false, "Use distance or density methods for detection of knee", nullptr, {{"distance", "density"}, 2}, true},
  {{{}, 0}, {{"--expect-cells"}, 1}, OptAction::Store, OptType::Int, "expect_cells", "False", false, "Prior expectation on the upper limit on the number of cells sequenced", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--allow-threshold-error"}, 1}, OptAction::StoreTrue, OptType::None, "allow_threshold_error", "False", false, "Don't select a threshold. Will still output the plots if requested (--plot-prefix)", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--set-cell-number"}, 1}, OptAction::Store, OptType::Int, "cell_number", "False", false, "Specify the number of cell barcodes to accept", nullptr, {{}, 0}, true},
};
const OptionSpec kG1_whitelist[] = {
  {{{}, 0}, {{"--extract-method"}, 1}, OptAction::Store, OptType::Choice, "extract_method", "string", false, "How to extract the umi +/- cell barcodes, Choose from 'string' or 'regex'", nullptr, {{"string", "regex"}, 2}, true},
  {{{"-p"}, 1}, {{"--bc-pattern"}, 1}, OptAction::Store, OptType::String, "pattern", "none", true, "Barcode pattern", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--bc-pattern2"}, 1}, OptAction::Store, OptType::String, "pattern2", "none", true, "Barcode pattern for paired reads", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--3prime"}, 1}, OptAction::StoreTrue, OptType::None, "prime3", "none", true, "barcode is on 3' end of read.", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--read2-in"}, 1}, OptAction::Store, OptType::String, "read2_in", "none", true, "file name for read pairs", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read2-only"}, 1}, OptAction::StoreTrue, OptType::None, "read2_only", "none", true, "Only extract from read2", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--filtered-out"}, 1}, OptAction::Store, OptType::String, "filtered_out", "none", true, "Write out reads not matching regex pattern to this file", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filtered-out2"}, 1}, OptAction::Store, OptType::String, "filtered_out2", "none", true, "Write out paired reads not matching regex pattern to this file", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--ignore-read-pair-suffixes"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_suffix", "False", false, "Ignore '\\1' and '\\2' read name suffixes", nullptr, {{}, 0}, false},
};
const OptionSpec kG2_whitelist[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG3_whitelist[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG4_whitelist[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_whitelist[] = {
  {"whitelist-specific options", nullptr, {kG0_whitelist, 8}},
  {"fastq barcode extraction options", nullptr, {kG1_whitelist, 9}},
  {"Input/Output pipe options", nullptr, {kG2_whitelist, 7}},
  {"profiling options", nullptr, {kG3_whitelist, 3}},
  {"common options", nullptr, {kG4_whitelist, 4}},
};

// ---------- extract ----------
const ExtraDefault kExtra_extract[] = {
  {"filter_cell_barcodes", "False", false},
};

const OptionSpec kTop_extract[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
};

const OptionSpec kG0_extract[] = {
  {{{}, 0}, {{"--retain-umi"}, 1}, OptAction::StoreTrue, OptType::None, "retain_umi", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--read2-out"}, 1}, OptAction::Store, OptType::String, "read2_out", "False", false, "file to output processed paired read to", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read2-stdout"}, 1}, OptAction::StoreTrue, OptType::None, "read2_stdout", "False", false, "Paired reads, send read2 to stdout, discarding read1", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--quality-filter-threshold"}, 1}, OptAction::Store, OptType::Int, "quality_filter_threshold", "none", true, "Remove reads where any UMI base quality score falls below this threshold", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--quality-filter-mask"}, 1}, OptAction::Store, OptType::Int, "quality_filter_mask", "none", true, "If a UMI base has a quality below this threshold, replace the base with 'N'", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--quality-encoding"}, 1}, OptAction::Store, OptType::Choice, "quality_encoding", "none", true, "Quality score encoding. Choose from 'phred33'[33-77] 'phred64' [64-106] or 'solexa' [59-106]", nullptr, {{"phred33", "phred64", "solexa"}, 3}, true},
  {{{}, 0}, {{"--filter-cell-barcode"}, 1}, OptAction::StoreTrue, OptType::None, "filter_cell_barcode", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--error-correct-cell"}, 1}, OptAction::StoreTrue, OptType::None, "error_correct_cell", "False", false, "Correct errors in the cell barcode", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--whitelist"}, 1}, OptAction::Store, OptType::String, "whitelist", "none", true, "A whitelist of accepted cell barcodes", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--blacklist"}, 1}, OptAction::Store, OptType::String, "blacklist", "none", true, "A blacklist of rejected cell barcodes", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filter-umi"}, 1}, OptAction::StoreTrue, OptType::None, "filter_umi", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-whitelist"}, 1}, OptAction::Store, OptType::String, "umi_whitelist", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-whitelist-paired"}, 1}, OptAction::Store, OptType::String, "umi_whitelist_paired", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--correct-umi-threshold"}, 1}, OptAction::Store, OptType::Int, "correct_umi_threshold", "0", false, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-correct-log"}, 1}, OptAction::Store, OptType::String, "umi_correct_log", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--subset-reads", "--reads-subset"}, 2}, OptAction::Store, OptType::Int, "reads_subset", "none", true, "Only extract from the first N reads. If N is greater than the number of reads, all reads will be used", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--reconcile-pairs"}, 1}, OptAction::StoreTrue, OptType::None, "reconcile", "False", false, "Allow the presences of reads in read2 input that are not present in read1 input. This allows cell barcode filtering of read1s without considering read2s", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-separator"}, 1}, OptAction::Store, OptType::String, "umi_separator", "_", false, "Separator to use to add UMI to the read name. Default: _", nullptr, {{}, 0}, true},
};
const OptionSpec kG1_extract[] = {
  {{{}, 0}, {{"--either-read"}, 1}, OptAction::StoreTrue, OptType::None, "either_read", "False", false, "UMI may be on either read (see --either-read-resolve) for options to resolve cases whereUMI is on both reads", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--either-read-resolve"}, 1}, OptAction::Store, OptType::Choice, "either_read_resolve", "discard", false, "How to resolve instances where both reads contain a UMI but using --either-read.Choose from 'discard' or 'quality'(use highest quality). default=dicard", nullptr, {{"discard", "quality"}, 2}, true},
};
const OptionSpec kG2_extract[] = {
  {{{}, 0}, {{"--extract-method"}, 1}, OptAction::Store, OptType::Choice, "extract_method", "string", false, "How to extract the umi +/- cell barcodes, Choose from 'string' or 'regex'", nullptr, {{"string", "regex"}, 2}, true},
  {{{"-p"}, 1}, {{"--bc-pattern"}, 1}, OptAction::Store, OptType::String, "pattern", "none", true, "Barcode pattern", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--bc-pattern2"}, 1}, OptAction::Store, OptType::String, "pattern2", "none", true, "Barcode pattern for paired reads", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--3prime"}, 1}, OptAction::StoreTrue, OptType::None, "prime3", "none", true, "barcode is on 3' end of read.", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--read2-in"}, 1}, OptAction::Store, OptType::String, "read2_in", "none", true, "file name for read pairs", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read2-only"}, 1}, OptAction::StoreTrue, OptType::None, "read2_only", "False", false, "Only extract from read2", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--filtered-out"}, 1}, OptAction::Store, OptType::String, "filtered_out", "none", true, "Write out reads not matching regex pattern to this file", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filtered-out2"}, 1}, OptAction::Store, OptType::String, "filtered_out2", "none", true, "Write out paired reads not matching regex pattern to this file", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--ignore-read-pair-suffixes"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_suffix", "False", false, "Ignore '\\1' and '\\2' read name suffixes", nullptr, {{}, 0}, false},
};
const OptionSpec kG3_extract[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG4_extract[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG5_extract[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_extract[] = {
  {"extract-specific options", nullptr, {kG0_extract, 18}},
  {"[EXPERIMENTAl] barcode extraction options", nullptr, {kG1_extract, 2}},
  {"fastq barcode extraction options", nullptr, {kG2_extract, 9}},
  {"Input/Output pipe options", nullptr, {kG3_extract, 7}},
  {"profiling options", nullptr, {kG4_extract, 3}},
  {"common options", nullptr, {kG5_extract, 4}},
};

// ---------- group ----------
const ExtraDefault kExtra_group[] = {
  {nullptr, nullptr, true},  // none for this tool
};

const OptionSpec kTop_group[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-group-tag"}, 1}, OptAction::Store, OptType::String, "umi_group_tag", "BX", false, "tag for the outputted umi group", nullptr, {{}, 0}, true},
};

const OptionSpec kG0_group[] = {
  {{{}, 0}, {{"--group-out"}, 1}, OptAction::Store, OptType::String, "tsv", "none", true, "Outfile name for file mapping read id to read group", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--output-bam"}, 1}, OptAction::StoreTrue, OptType::None, "output_bam", "False", false, "output a bam file with read groups tagged using the UG tag[default=%default]", nullptr, {{}, 0}, false},
};
const OptionSpec kG1_group[] = {
  {{{}, 0}, {{"--extract-umi-method"}, 1}, OptAction::Store, OptType::Choice, "get_umi_method", "read_id", false, "how is the read UMI +/ cell barcode encoded? [default=%default]", nullptr, {{"read_id", "tag", "umis"}, 3}, true},
  {{{}, 0}, {{"--umi-separator"}, 1}, OptAction::Store, OptType::String, "umi_sep", "_", false, "separator between read id and UMI", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag"}, 1}, OptAction::Store, OptType::String, "umi_tag", "RX", false, "tag containing umi", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-split"}, 1}, OptAction::Store, OptType::String, "umi_tag_split", "none", true, "split UMI in tag and take the first element", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "umi_tag_delim", "none", true, "concatenate UMI in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag"}, 1}, OptAction::Store, OptType::String, "cell_tag", "none", true, "tag containing cell barcode", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-split"}, 1}, OptAction::Store, OptType::String, "cell_tag_split", "-", false, "split cell barcode in tag and take the firstelement for e.g 10X GEM tags", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "cell_tag_delim", "none", true, "concatenate cell barcode in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filter-umi"}, 1}, OptAction::StoreTrue, OptType::None, "filter_umi", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-whitelist"}, 1}, OptAction::Store, OptType::String, "umi_whitelist", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-whitelist-paired"}, 1}, OptAction::Store, OptType::String, "umi_whitelist_paired", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
};
const OptionSpec kG2_group[] = {
  {{{}, 0}, {{"--method"}, 1}, OptAction::Store, OptType::Choice, "method", "directional", false, "method to use for umi grouping [default=%default]", nullptr, {{"adjacency", "directional", "percentile", "unique", "cluster"}, 5}, true},
  {{{}, 0}, {{"--edit-distance-threshold"}, 1}, OptAction::Store, OptType::Int, "threshold", "1", false, "Edit distance theshold at which to join two UMIs when grouping UMIs. [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--spliced-is-unique"}, 1}, OptAction::StoreTrue, OptType::None, "spliced", "False", false, "Treat a spliced read as different to an unspliced one [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--soft-clip-threshold"}, 1}, OptAction::Store, OptType::Float, "soft_clip_threshold", "4", false, "number of bases clipped from 5' end before read is counted as spliced [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read-length"}, 1}, OptAction::StoreTrue, OptType::None, "read_length", "False", false, "use read length in addition to position and UMI to identify possible duplicates [default=%default]", nullptr, {{}, 0}, false},
};
const OptionSpec kG3_group[] = {
  {{{}, 0}, {{"--per-gene"}, 1}, OptAction::StoreTrue, OptType::None, "per_gene", "False", false, "Group/Dedup/Count per gene. Must combine with either --gene-tag or --per-contig", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-tag"}, 1}, OptAction::Store, OptType::String, "gene_tag", "none", true, "Gene is defined by this bam tag [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--assigned-status-tag"}, 1}, OptAction::Store, OptType::String, "assigned_tag", "none", true, "Bam tag describing whether read is assigned to a gene By defualt, this is set as the same tag as --gene-tag", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--skip-tags-regex"}, 1}, OptAction::Store, OptType::String, "skip_regex", "^(__|Unassigned)", false, "Used with --gene-tag. Ignore reads where the gene-tag matches this regex", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-contig"}, 1}, OptAction::StoreTrue, OptType::None, "per_contig", "False", false, "group/dedup/count UMIs per contig (field 3 in BAM; RNAME), e.g for transcriptome where contig = gene", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-transcript-map"}, 1}, OptAction::Store, OptType::String, "gene_transcript_map", "none", true, "File mapping transcripts to genes (tab separated)", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-cell"}, 1}, OptAction::StoreTrue, OptType::None, "per_cell", "False", false, "group/dedup/count per cell", nullptr, {{}, 0}, false},
};
const OptionSpec kG4_group[] = {
  {{{}, 0}, {{"--buffer-whole-contig"}, 1}, OptAction::StoreTrue, OptType::None, "whole_contig", "False", false, "Read whole contig before outputting bundles: guarantees that no reads are missed, but increases memory usage", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--whole-contig"}, 1}, OptAction::StoreTrue, OptType::None, "whole_contig", "False", false, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--multimapping-detection-method"}, 1}, OptAction::Store, OptType::Choice, "detection_method", "none", true, "Some aligners identify multimapping using bam tags. Setting this option to NH, X0 or XT will use these tags when selecting the best read amongst reads with the same position and umi [default=%default]", nullptr, {{"NH", "X0", "XT"}, 3}, true},
};
const OptionSpec kG5_group[] = {
  {{{}, 0}, {{"--in-format"}, 1}, OptAction::Store, OptType::Choice, "in_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--input-options"}, 1}, OptAction::Store, OptType::String, "input_options", "none", true, "Format string provided to htslib for reading. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-i"}, 1}, {{"--in-sam"}, 1}, OptAction::StoreTrue, OptType::None, "in_sam", "False", false, "[Deprecated] Input file is in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--out-format"}, 1}, OptAction::Store, OptType::Choice, "out_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--output-options"}, 1}, OptAction::Store, OptType::String, "output_options", "none", true, "Format string provided to htslib for writing. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-o"}, 1}, {{"--out-sam"}, 1}, OptAction::StoreTrue, OptType::None, "out_sam", "False", false, "[Deprecated] Output alignments in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--reference-filename"}, 1}, OptAction::Store, OptType::String, "reference_filename", "none", true, "File path or URL to the genome reference to be used when reading or writing CRAM files. By default, when reading a CRAM file, the reference recorded in the input file will be used unless this is specified. When writing, specifying a reference location is required.", nullptr, {{}, 0}, true},
};
const OptionSpec kG6_group[] = {
  {{{}, 0}, {{"--mapping-quality"}, 1}, OptAction::Store, OptType::Int, "mapping_quality", "0", false, "Minimum mapping quality for a read to be retained [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--output-unmapped"}, 1}, OptAction::StoreTrue, OptType::None, "output_unmapped", "False", false, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-umi"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_umi", "False", false, "Ignore UMI and dedup only on position", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-tlen"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_tlen", "False", false, "Option to dedup paired end reads based solely on read1, whether or not the template length is the same", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--chrom"}, 1}, OptAction::Store, OptType::String, "chrom", "none", true, "Restrict to one chromosome", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--subset"}, 1}, OptAction::Store, OptType::Float, "subset", "none", true, "Use only a fraction of reads, specified by subset", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--paired"}, 1}, OptAction::StoreTrue, OptType::None, "paired", "False", false, "paired input BAM. [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--no-sort-output"}, 1}, OptAction::StoreTrue, OptType::None, "no_sort_output", "False", false, "Don't Sort the output", nullptr, {{}, 0}, false},
};
const OptionSpec kG7_group[] = {
  {{{}, 0}, {{"--unmapped-reads"}, 1}, OptAction::Store, OptType::Choice, "unmapped_reads", "discard", false, "How to handle unmapped reads. Options are 'discard', 'use' or 'output' [default=%default]", nullptr, {{"discard", "use", "output"}, 3}, true},
  {{{}, 0}, {{"--chimeric-pairs"}, 1}, OptAction::Store, OptType::Choice, "chimeric_pairs", "use", false, "How to handle chimeric read pairs. Options are 'discard', 'use' or 'output' [default=%default]", nullptr, {{"discard", "use", "output"}, 3}, true},
  {{{}, 0}, {{"--unpaired-reads"}, 1}, OptAction::Store, OptType::Choice, "unpaired_reads", "use", false, "How to handle unpaired reads. Options are 'discard', 'use' or 'output' [default=%default]", nullptr, {{"discard", "use", "output"}, 3}, true},
};
const OptionSpec kG8_group[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG9_group[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG10_group[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_group[] = {
  {"group-specific options", nullptr, {kG0_group, 2}},
  {"Barcode extraction options", nullptr, {kG1_group, 11}},
  {"UMI grouping options", nullptr, {kG2_group, 5}},
  {"single-cell RNA-Seq options", nullptr, {kG3_group, 7}},
  {"group/dedup options", nullptr, {kG4_group, 3}},
  {"Input/Output format options", nullptr, {kG5_group, 7}},
  {"SAM/BAM filtering options", nullptr, {kG6_group, 8}},
  {"Group SAM/BAM options", nullptr, {kG7_group, 3}},
  {"Input/Output pipe options", nullptr, {kG8_group, 7}},
  {"profiling options", nullptr, {kG9_group, 3}},
  {"common options", nullptr, {kG10_group, 4}},
};

// ---------- dedup ----------
const ExtraDefault kExtra_dedup[] = {
  {nullptr, nullptr, true},  // none for this tool
};

const OptionSpec kTop_dedup[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
};

const OptionSpec kG0_dedup[] = {
  {{{}, 0}, {{"--output-stats"}, 1}, OptAction::Store, OptType::String, "stats", "False", false, "Specify location to output stats", nullptr, {{}, 0}, true},
};
const OptionSpec kG1_dedup[] = {
  {{{}, 0}, {{"--extract-umi-method"}, 1}, OptAction::Store, OptType::Choice, "get_umi_method", "read_id", false, "how is the read UMI +/ cell barcode encoded? [default=%default]", nullptr, {{"read_id", "tag", "umis"}, 3}, true},
  {{{}, 0}, {{"--umi-separator"}, 1}, OptAction::Store, OptType::String, "umi_sep", "_", false, "separator between read id and UMI", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag"}, 1}, OptAction::Store, OptType::String, "umi_tag", "RX", false, "tag containing umi", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-split"}, 1}, OptAction::Store, OptType::String, "umi_tag_split", "none", true, "split UMI in tag and take the first element", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "umi_tag_delim", "none", true, "concatenate UMI in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag"}, 1}, OptAction::Store, OptType::String, "cell_tag", "none", true, "tag containing cell barcode", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-split"}, 1}, OptAction::Store, OptType::String, "cell_tag_split", "-", false, "split cell barcode in tag and take the firstelement for e.g 10X GEM tags", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "cell_tag_delim", "none", true, "concatenate cell barcode in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filter-umi"}, 1}, OptAction::StoreTrue, OptType::None, "filter_umi", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-whitelist"}, 1}, OptAction::Store, OptType::String, "umi_whitelist", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-whitelist-paired"}, 1}, OptAction::Store, OptType::String, "umi_whitelist_paired", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
};
const OptionSpec kG2_dedup[] = {
  {{{}, 0}, {{"--method"}, 1}, OptAction::Store, OptType::Choice, "method", "directional", false, "method to use for umi grouping [default=%default]", nullptr, {{"adjacency", "directional", "percentile", "unique", "cluster"}, 5}, true},
  {{{}, 0}, {{"--edit-distance-threshold"}, 1}, OptAction::Store, OptType::Int, "threshold", "1", false, "Edit distance theshold at which to join two UMIs when grouping UMIs. [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--spliced-is-unique"}, 1}, OptAction::StoreTrue, OptType::None, "spliced", "False", false, "Treat a spliced read as different to an unspliced one [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--soft-clip-threshold"}, 1}, OptAction::Store, OptType::Float, "soft_clip_threshold", "4", false, "number of bases clipped from 5' end before read is counted as spliced [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read-length"}, 1}, OptAction::StoreTrue, OptType::None, "read_length", "False", false, "use read length in addition to position and UMI to identify possible duplicates [default=%default]", nullptr, {{}, 0}, false},
};
const OptionSpec kG3_dedup[] = {
  {{{}, 0}, {{"--per-gene"}, 1}, OptAction::StoreTrue, OptType::None, "per_gene", "False", false, "Group/Dedup/Count per gene. Must combine with either --gene-tag or --per-contig", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-tag"}, 1}, OptAction::Store, OptType::String, "gene_tag", "none", true, "Gene is defined by this bam tag [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--assigned-status-tag"}, 1}, OptAction::Store, OptType::String, "assigned_tag", "none", true, "Bam tag describing whether read is assigned to a gene By defualt, this is set as the same tag as --gene-tag", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--skip-tags-regex"}, 1}, OptAction::Store, OptType::String, "skip_regex", "^(__|Unassigned)", false, "Used with --gene-tag. Ignore reads where the gene-tag matches this regex", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-contig"}, 1}, OptAction::StoreTrue, OptType::None, "per_contig", "False", false, "group/dedup/count UMIs per contig (field 3 in BAM; RNAME), e.g for transcriptome where contig = gene", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-transcript-map"}, 1}, OptAction::Store, OptType::String, "gene_transcript_map", "none", true, "File mapping transcripts to genes (tab separated)", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-cell"}, 1}, OptAction::StoreTrue, OptType::None, "per_cell", "False", false, "group/dedup/count per cell", nullptr, {{}, 0}, false},
};
const OptionSpec kG4_dedup[] = {
  {{{}, 0}, {{"--buffer-whole-contig"}, 1}, OptAction::StoreTrue, OptType::None, "whole_contig", "False", false, "Read whole contig before outputting bundles: guarantees that no reads are missed, but increases memory usage", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--whole-contig"}, 1}, OptAction::StoreTrue, OptType::None, "whole_contig", "False", false, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--multimapping-detection-method"}, 1}, OptAction::Store, OptType::Choice, "detection_method", "none", true, "Some aligners identify multimapping using bam tags. Setting this option to NH, X0 or XT will use these tags when selecting the best read amongst reads with the same position and umi [default=%default]", nullptr, {{"NH", "X0", "XT"}, 3}, true},
};
const OptionSpec kG5_dedup[] = {
  {{{}, 0}, {{"--in-format"}, 1}, OptAction::Store, OptType::Choice, "in_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--input-options"}, 1}, OptAction::Store, OptType::String, "input_options", "none", true, "Format string provided to htslib for reading. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-i"}, 1}, {{"--in-sam"}, 1}, OptAction::StoreTrue, OptType::None, "in_sam", "False", false, "[Deprecated] Input file is in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--out-format"}, 1}, OptAction::Store, OptType::Choice, "out_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--output-options"}, 1}, OptAction::Store, OptType::String, "output_options", "none", true, "Format string provided to htslib for writing. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-o"}, 1}, {{"--out-sam"}, 1}, OptAction::StoreTrue, OptType::None, "out_sam", "False", false, "[Deprecated] Output alignments in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--reference-filename"}, 1}, OptAction::Store, OptType::String, "reference_filename", "none", true, "File path or URL to the genome reference to be used when reading or writing CRAM files. By default, when reading a CRAM file, the reference recorded in the input file will be used unless this is specified. When writing, specifying a reference location is required.", nullptr, {{}, 0}, true},
};
const OptionSpec kG6_dedup[] = {
  {{{}, 0}, {{"--mapping-quality"}, 1}, OptAction::Store, OptType::Int, "mapping_quality", "0", false, "Minimum mapping quality for a read to be retained [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--output-unmapped"}, 1}, OptAction::StoreTrue, OptType::None, "output_unmapped", "False", false, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-umi"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_umi", "False", false, "Ignore UMI and dedup only on position", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-tlen"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_tlen", "False", false, "Option to dedup paired end reads based solely on read1, whether or not the template length is the same", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--chrom"}, 1}, OptAction::Store, OptType::String, "chrom", "none", true, "Restrict to one chromosome", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--subset"}, 1}, OptAction::Store, OptType::Float, "subset", "none", true, "Use only a fraction of reads, specified by subset", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--paired"}, 1}, OptAction::StoreTrue, OptType::None, "paired", "False", false, "paired input BAM. [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--no-sort-output"}, 1}, OptAction::StoreTrue, OptType::None, "no_sort_output", "False", false, "Don't Sort the output", nullptr, {{}, 0}, false},
};
const OptionSpec kG7_dedup[] = {
  {{{}, 0}, {{"--unmapped-reads"}, 1}, OptAction::Store, OptType::Choice, "unmapped_reads", "discard", false, "How to handle unmapped reads. Options are 'discard' or 'use' [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
  {{{}, 0}, {{"--chimeric-pairs"}, 1}, OptAction::Store, OptType::Choice, "chimeric_pairs", "use", false, "How to handle chimeric read pairs. Options are 'discard' or 'use'  [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
  {{{}, 0}, {{"--unpaired-reads"}, 1}, OptAction::Store, OptType::Choice, "unpaired_reads", "use", false, "How to handle unpaired reads. Options are 'discard'or 'use' [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
};
const OptionSpec kG8_dedup[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG9_dedup[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG10_dedup[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_dedup[] = {
  {"dedup-specific options", nullptr, {kG0_dedup, 1}},
  {"Barcode extraction options", nullptr, {kG1_dedup, 11}},
  {"UMI grouping options", nullptr, {kG2_dedup, 5}},
  {"single-cell RNA-Seq options", nullptr, {kG3_dedup, 7}},
  {"group/dedup options", nullptr, {kG4_dedup, 3}},
  {"Input/Output format options", nullptr, {kG5_dedup, 7}},
  {"SAM/BAM filtering options", nullptr, {kG6_dedup, 8}},
  {"Dedup and Count SAM/BAM options", nullptr, {kG7_dedup, 3}},
  {"Input/Output pipe options", nullptr, {kG8_dedup, 7}},
  {"profiling options", nullptr, {kG9_dedup, 3}},
  {"common options", nullptr, {kG10_dedup, 4}},
};

// ---------- count ----------
const ExtraDefault kExtra_count[] = {
  {nullptr, nullptr, true},  // none for this tool
};

const OptionSpec kTop_count[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--wide-format-cell-counts"}, 1}, OptAction::StoreTrue, OptType::None, "wide_format_cell_counts", "False", false, "output the cell counts in a wide format (rows=genes, columns=cells)", nullptr, {{}, 0}, false},
};

const OptionSpec kG0_count[] = {  // EMPTY group
  {{{}, 0}, {{}, 0}, OptAction::Store, OptType::None, nullptr, nullptr, true, nullptr, nullptr, {{}, 0}, false},
};
const OptionSpec kG1_count[] = {
  {{{}, 0}, {{"--extract-umi-method"}, 1}, OptAction::Store, OptType::Choice, "get_umi_method", "read_id", false, "how is the read UMI +/ cell barcode encoded? [default=%default]", nullptr, {{"read_id", "tag", "umis"}, 3}, true},
  {{{}, 0}, {{"--umi-separator"}, 1}, OptAction::Store, OptType::String, "umi_sep", "_", false, "separator between read id and UMI", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag"}, 1}, OptAction::Store, OptType::String, "umi_tag", "RX", false, "tag containing umi", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-split"}, 1}, OptAction::Store, OptType::String, "umi_tag_split", "none", true, "split UMI in tag and take the first element", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "umi_tag_delim", "none", true, "concatenate UMI in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag"}, 1}, OptAction::Store, OptType::String, "cell_tag", "none", true, "tag containing cell barcode", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-split"}, 1}, OptAction::Store, OptType::String, "cell_tag_split", "-", false, "split cell barcode in tag and take the firstelement for e.g 10X GEM tags", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--cell-tag-delimiter"}, 1}, OptAction::Store, OptType::String, "cell_tag_delim", "none", true, "concatenate cell barcode in tag separated by delimiter", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--filter-umi"}, 1}, OptAction::StoreTrue, OptType::None, "filter_umi", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--umi-whitelist"}, 1}, OptAction::Store, OptType::String, "umi_whitelist", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--umi-whitelist-paired"}, 1}, OptAction::Store, OptType::String, "umi_whitelist_paired", "none", true, "SUPPRESSHELP", nullptr, {{}, 0}, true},
};
const OptionSpec kG2_count[] = {
  {{{}, 0}, {{"--method"}, 1}, OptAction::Store, OptType::Choice, "method", "directional", false, "method to use for umi grouping [default=%default]", nullptr, {{"adjacency", "directional", "percentile", "unique", "cluster"}, 5}, true},
  {{{}, 0}, {{"--edit-distance-threshold"}, 1}, OptAction::Store, OptType::Int, "threshold", "1", false, "Edit distance theshold at which to join two UMIs when grouping UMIs. [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--spliced-is-unique"}, 1}, OptAction::StoreTrue, OptType::None, "spliced", "False", false, "Treat a spliced read as different to an unspliced one [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--soft-clip-threshold"}, 1}, OptAction::Store, OptType::Float, "soft_clip_threshold", "4", false, "number of bases clipped from 5' end before read is counted as spliced [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read-length"}, 1}, OptAction::StoreTrue, OptType::None, "read_length", "False", false, "use read length in addition to position and UMI to identify possible duplicates [default=%default]", nullptr, {{}, 0}, false},
};
const OptionSpec kG3_count[] = {
  {{{}, 0}, {{"--per-gene"}, 1}, OptAction::StoreTrue, OptType::None, "per_gene", "False", false, "Group/Dedup/Count per gene. Must combine with either --gene-tag or --per-contig", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-tag"}, 1}, OptAction::Store, OptType::String, "gene_tag", "none", true, "Gene is defined by this bam tag [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--assigned-status-tag"}, 1}, OptAction::Store, OptType::String, "assigned_tag", "none", true, "Bam tag describing whether read is assigned to a gene By defualt, this is set as the same tag as --gene-tag", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--skip-tags-regex"}, 1}, OptAction::Store, OptType::String, "skip_regex", "^(__|Unassigned)", false, "Used with --gene-tag. Ignore reads where the gene-tag matches this regex", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-contig"}, 1}, OptAction::StoreTrue, OptType::None, "per_contig", "False", false, "group/dedup/count UMIs per contig (field 3 in BAM; RNAME), e.g for transcriptome where contig = gene", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--gene-transcript-map"}, 1}, OptAction::Store, OptType::String, "gene_transcript_map", "none", true, "File mapping transcripts to genes (tab separated)", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-cell"}, 1}, OptAction::StoreTrue, OptType::None, "per_cell", "False", false, "group/dedup/count per cell", nullptr, {{}, 0}, false},
};
const OptionSpec kG4_count[] = {
  {{{}, 0}, {{"--in-format"}, 1}, OptAction::Store, OptType::Choice, "in_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--input-options"}, 1}, OptAction::Store, OptType::String, "input_options", "none", true, "Format string provided to htslib for reading. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-i"}, 1}, {{"--in-sam"}, 1}, OptAction::StoreTrue, OptType::None, "in_sam", "False", false, "[Deprecated] Input file is in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--reference-filename"}, 1}, OptAction::Store, OptType::String, "reference_filename", "none", true, "File path or URL to the genome reference to be used when reading or writing CRAM files. By default, when reading a CRAM file, the reference recorded in the input file will be used unless this is specified. When writing, specifying a reference location is required.", nullptr, {{}, 0}, true},
};
const OptionSpec kG5_count[] = {
  {{{}, 0}, {{"--mapping-quality"}, 1}, OptAction::Store, OptType::Int, "mapping_quality", "0", false, "Minimum mapping quality for a read to be retained [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--output-unmapped"}, 1}, OptAction::StoreTrue, OptType::None, "output_unmapped", "False", false, "SUPPRESSHELP", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-umi"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_umi", "False", false, "Ignore UMI and dedup only on position", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--ignore-tlen"}, 1}, OptAction::StoreTrue, OptType::None, "ignore_tlen", "False", false, "Option to dedup paired end reads based solely on read1, whether or not the template length is the same", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--chrom"}, 1}, OptAction::Store, OptType::String, "chrom", "none", true, "Restrict to one chromosome", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--subset"}, 1}, OptAction::Store, OptType::Float, "subset", "none", true, "Use only a fraction of reads, specified by subset", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--paired"}, 1}, OptAction::StoreTrue, OptType::None, "paired", "False", false, "paired input BAM. [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--no-sort-output"}, 1}, OptAction::StoreTrue, OptType::None, "no_sort_output", "False", false, "Don't Sort the output", nullptr, {{}, 0}, false},
};
const OptionSpec kG6_count[] = {
  {{{}, 0}, {{"--unmapped-reads"}, 1}, OptAction::Store, OptType::Choice, "unmapped_reads", "discard", false, "How to handle unmapped reads. Options are 'discard' or 'use' [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
  {{{}, 0}, {{"--chimeric-pairs"}, 1}, OptAction::Store, OptType::Choice, "chimeric_pairs", "use", false, "How to handle chimeric read pairs. Options are 'discard' or 'use'  [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
  {{{}, 0}, {{"--unpaired-reads"}, 1}, OptAction::Store, OptType::Choice, "unpaired_reads", "use", false, "How to handle unpaired reads. Options are 'discard'or 'use' [default=%default]", nullptr, {{"discard", "use"}, 2}, true},
};
const OptionSpec kG7_count[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG8_count[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG9_count[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_count[] = {
  {"count-specific options", nullptr, {kG0_count, 0}},
  {"Barcode extraction options", nullptr, {kG1_count, 11}},
  {"UMI grouping options", nullptr, {kG2_count, 5}},
  {"single-cell RNA-Seq options", nullptr, {kG3_count, 7}},
  {"Input/Output format options", nullptr, {kG4_count, 4}},
  {"SAM/BAM filtering options", nullptr, {kG5_count, 8}},
  {"Dedup and Count SAM/BAM options", nullptr, {kG6_count, 3}},
  {"Input/Output pipe options", nullptr, {kG7_count, 7}},
  {"profiling options", nullptr, {kG8_count, 3}},
  {"common options", nullptr, {kG9_count, 4}},
};

// ---------- count_tab ----------
const ExtraDefault kExtra_count_tab[] = {
  {nullptr, nullptr, true},  // none for this tool
};

const OptionSpec kTop_count_tab[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
};

const OptionSpec kG0_count_tab[] = {
  {{{}, 0}, {{"--barcode-separator"}, 1}, OptAction::Store, OptType::String, "bc_sep", "_", false, "separator between read id and UMI  and (optionally) the cell barcode", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--per-cell"}, 1}, OptAction::StoreTrue, OptType::None, "per_cell", "none", true, "Readname includes cell barcode as well as UMI in format: read[sep]UMI[sep]CB", nullptr, {{}, 0}, false},
};
const OptionSpec kG1_count_tab[] = {
  {{{}, 0}, {{"--method"}, 1}, OptAction::Store, OptType::Choice, "method", "directional", false, "method to use for umi grouping [default=%default]", nullptr, {{"adjacency", "directional", "percentile", "unique", "cluster"}, 5}, true},
  {{{}, 0}, {{"--edit-distance-threshold"}, 1}, OptAction::Store, OptType::Int, "threshold", "1", false, "Edit distance theshold at which to join two UMIs when grouping UMIs. [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--spliced-is-unique"}, 1}, OptAction::StoreTrue, OptType::None, "spliced", "False", false, "Treat a spliced read as different to an unspliced one [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--soft-clip-threshold"}, 1}, OptAction::Store, OptType::Float, "soft_clip_threshold", "4", false, "number of bases clipped from 5' end before read is counted as spliced [default=%default]", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--read-length"}, 1}, OptAction::StoreTrue, OptType::None, "read_length", "False", false, "use read length in addition to position and UMI to identify possible duplicates [default=%default]", nullptr, {{}, 0}, false},
};
const OptionSpec kG2_count_tab[] = {
  {{{}, 0}, {{"--in-format"}, 1}, OptAction::Store, OptType::Choice, "in_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--input-options"}, 1}, OptAction::Store, OptType::String, "input_options", "none", true, "Format string provided to htslib for reading. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-i"}, 1}, {{"--in-sam"}, 1}, OptAction::StoreTrue, OptType::None, "in_sam", "False", false, "[Deprecated] Input file is in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--reference-filename"}, 1}, OptAction::Store, OptType::String, "reference_filename", "none", true, "File path or URL to the genome reference to be used when reading or writing CRAM files. By default, when reading a CRAM file, the reference recorded in the input file will be used unless this is specified. When writing, specifying a reference location is required.", nullptr, {{}, 0}, true},
};
const OptionSpec kG3_count_tab[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG4_count_tab[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG5_count_tab[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_count_tab[] = {
  {"count_tab-specific options", nullptr, {kG0_count_tab, 2}},
  {"UMI grouping options", nullptr, {kG1_count_tab, 5}},
  {"Input/Output format options", nullptr, {kG2_count_tab, 4}},
  {"Input/Output pipe options", nullptr, {kG3_count_tab, 7}},
  {"profiling options", nullptr, {kG4_count_tab, 3}},
  {"common options", nullptr, {kG5_count_tab, 4}},
};

// ---------- prepare_for_em ----------
const ExtraDefault kExtra_prepare_for_em[] = {
  {nullptr, nullptr, true},  // none for this tool
};

const OptionSpec kTop_prepare_for_em[] = {
  {{{}, 0}, {{"--version"}, 1}, OptAction::Version, OptType::None, nullptr, "none", true, "show program's version number and exit", nullptr, {{}, 0}, false},
};

const OptionSpec kG0_prepare_for_em[] = {
  {{{}, 0}, {{"--tags"}, 1}, OptAction::Store, OptType::String, "tags", "UG,BX", false, "Comma-seperated list of tags to transfer from read1 to read2", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--sam"}, 1}, OptAction::StoreTrue, OptType::None, "sam", "False", false, "input and output SAM rather than BAM", nullptr, {{}, 0}, false},
};
const OptionSpec kG1_prepare_for_em[] = {
  {{{}, 0}, {{"--in-format"}, 1}, OptAction::Store, OptType::Choice, "in_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--input-options"}, 1}, OptAction::Store, OptType::String, "input_options", "none", true, "Format string provided to htslib for reading. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-i"}, 1}, {{"--in-sam"}, 1}, OptAction::StoreTrue, OptType::None, "in_sam", "False", false, "[Deprecated] Input file is in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--out-format"}, 1}, OptAction::Store, OptType::Choice, "out_format", "none", true, "File format of the input file. Format is usually implied from the extension of the filename, but maybe overridden with this option. Default=bam", nullptr, {{"sam", "bam", "cram"}, 3}, true},
  {{{}, 0}, {{"--output-options"}, 1}, OptAction::Store, OptType::String, "output_options", "none", true, "Format string provided to htslib for writing. Mostly useful for CRAM formatted files. See samtools documentation", nullptr, {{}, 0}, true},
  {{{"-o"}, 1}, {{"--out-sam"}, 1}, OptAction::StoreTrue, OptType::None, "out_sam", "False", false, "[Deprecated] Output alignments in sam format [default=%default]", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--reference-filename"}, 1}, OptAction::Store, OptType::String, "reference_filename", "none", true, "File path or URL to the genome reference to be used when reading or writing CRAM files. By default, when reading a CRAM file, the reference recorded in the input file will be used unless this is specified. When writing, specifying a reference location is required.", nullptr, {{}, 0}, true},
};
const OptionSpec kG2_prepare_for_em[] = {
  {{{"-I"}, 1}, {{"--stdin"}, 1}, OptAction::Store, OptType::String, "stdin", "-", false, "file to read stdin from [default = stdin].", "FILE", {{}, 0}, true},
  {{{"-L"}, 1}, {{"--log"}, 1}, OptAction::Store, OptType::String, "stdlog", "-", false, "file with logging information [default = stdout].", "FILE", {{}, 0}, true},
  {{{"-E"}, 1}, {{"--error"}, 1}, OptAction::Store, OptType::String, "stderr", "-", false, "file with error information [default = stderr].", "FILE", {{}, 0}, true},
  {{{"-S"}, 1}, {{"--stdout"}, 1}, OptAction::Store, OptType::String, "stdout", "-", false, "file where output is to go [default = stdout].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--temp-dir"}, 1}, OptAction::Store, OptType::String, "tmpdir", "none", true, "Directory for temporary files. If not set, the bash environmental variable TMPDIR is used[default = None].", "FILE", {{}, 0}, true},
  {{{}, 0}, {{"--log2stderr"}, 1}, OptAction::StoreTrue, OptType::None, "log2stderr", "False", false, "send logging information to stderr [default = False].", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--compresslevel"}, 1}, OptAction::Store, OptType::Int, "compresslevel", "6", false, "Level of Gzip compression to use. Default (6) matchesGNU gzip rather than python gzip default (which is 9)", nullptr, {{}, 0}, true},
};
const OptionSpec kG3_prepare_for_em[] = {
  {{{}, 0}, {{"--timeit"}, 1}, OptAction::Store, OptType::String, "timeit_file", "none", true, "store timeing information in file [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-name"}, 1}, OptAction::Store, OptType::String, "timeit_name", "all", false, "name in timing file for this class of jobs [%default].", nullptr, {{}, 0}, true},
  {{{}, 0}, {{"--timeit-header"}, 1}, OptAction::StoreTrue, OptType::None, "timeit_header", "none", true, "add header for timing information [%default].", nullptr, {{}, 0}, false},
};
const OptionSpec kG4_prepare_for_em[] = {
  {{{"-v"}, 1}, {{"--verbose"}, 1}, OptAction::Store, OptType::Int, "loglevel", "1", false, "loglevel [%default]. The higher, the more output.", nullptr, {{}, 0}, true},
  {{{"-h"}, 1}, {{"--help"}, 1}, OptAction::Callback, OptType::None, "short_help", "none", true, "output short help (command line options only).", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--help-extended"}, 1}, OptAction::HelpAction, OptType::None, nullptr, "none", true, "Output full documentation", nullptr, {{}, 0}, false},
  {{{}, 0}, {{"--random-seed"}, 1}, OptAction::Store, OptType::Int, "random_seed", "none", true, "random seed to initialize number generator with [%default].", nullptr, {{}, 0}, true},
};

const OptionGroupSpec kGroups_prepare_for_em[] = {
  {"RSEM preparation specific options", nullptr, {kG0_prepare_for_em, 2}},
  {"Input/Output format options", nullptr, {kG1_prepare_for_em, 7}},
  {"Input/Output pipe options", nullptr, {kG2_prepare_for_em, 7}},
  {"profiling options", nullptr, {kG3_prepare_for_em, 3}},
  {"common options", nullptr, {kG4_prepare_for_em, 4}},
};

const ToolSpec kTools[] = {
  {"whitelist", "\nwhitelist - Generates a whitelist of accepted cell barcodes\n\nUsage:\n\n   Single-end:\n      umi_tools whitelist [OPTIONS] [-I IN_FASTQ[.gz]] [-S OUT_TSV[.gz]]\n\n   Paired end:\n      umi_tools whitelist [OPTIONS] [-I IN_FASTQ[.gz]] [-S OUT_TSV[.gz]] --read2-in=IN2_FASTQ[.gz]\n\n   note: If -I/-S are ommited standard in and standard out are used\n         for input and output.  Input/Output will be (de)compressed if a\n         filename provided to -S/-I/--read2-in ends in .gz\n", "%prog version: $Id$", {kExtra_whitelist, 2}, {kTop_whitelist, 2}, {kGroups_whitelist, 5}, 78, 24, 2, true},
  {"extract", "\nextract - Extract UMI from fastq\n\nUsage:\n\n   Single-end:\n      umi_tools extract [OPTIONS] -p PATTERN [-I IN_FASTQ[.gz]] [-S OUT_FASTQ[.gz]]\n\n   Paired end:\n      umi_tools extract [OPTIONS] -p PATTERN [-I IN_FASTQ[.gz]] [-S OUT_FASTQ[.gz]] --read2-in=IN2_FASTQ[.gz] --read2-out=OUT2_FASTQ[.gz]\n\n   note: If -I/-S are ommited standard in and standard out are used\n         for input and output.  To generate a valid BAM file on\n         standard out, please redirect log with --log=LOGFILE or\n         --log2stderr. Input/Output will be (de)compressed if a\n         filename provided to -S/-I/--read2-in/read2-out ends in .gz\n         ", "%prog version: $Id$", {kExtra_extract, 1}, {kTop_extract, 1}, {kGroups_extract, 6}, 78, 24, 2, true},
  {"group", "\ngroup - Group reads based on their UMI\n\nUsage: umi_tools group --output-bam [OPTIONS] [--stdin=INFILE.bam] [--stdout=OUTFILE.bam]\n\n       note: If --stdout is ommited, standard out is output. To\n             generate a valid BAM file on standard out, please\n             redirect log with --log=LOGFILE or --log2stderr ", "%prog version: $Id$", {kExtra_group, 0}, {kTop_group, 2}, {kGroups_group, 11}, 78, 24, 2, true},
  {"dedup", "\ndedup - Deduplicate reads using UMI and mapping coordinates\n\nUsage: umi_tools dedup [OPTIONS] [--stdin=IN_BAM] [--stdout=OUT_BAM]\n\n       note: If --stdout is ommited, standard out is output. To\n             generate a valid BAM file on standard out, please\n             redirect log with --log=LOGFILE or --log2stderr ", "%prog version: $Id$", {kExtra_dedup, 0}, {kTop_dedup, 1}, {kGroups_dedup, 11}, 78, 24, 2, true},
  {"count", "\ncount - Count reads per-gene using UMI and mapping coordinates\n\nUsage: umi_tools count [OPTIONS] --stdin=IN_BAM [--stdout=OUT_BAM]\n\n       note: If --stdout is ommited, standard out is output. To\n             generate a valid BAM file on standard out, please\n             redirect log with --log=LOGFILE or --log2stderr ", "%prog version: $Id$", {kExtra_count, 0}, {kTop_count, 2}, {kGroups_count, 10}, 78, 24, 2, true},
  {"count_tab", "\ncount_tab - Count reads per gene from flatfile using UMIs\n\nUsage: umi_tools count_tab [OPTIONS] [--stdin=IN_TSV[.gz]] [--stdout=OUT_TSV[.gz]]\n\n       note: If --stdin/--stdout are ommited standard in and standard\n             out are used for input and output. Input/Output will be\n             (de)compressed if a filename provided to --stdin/--stdout\n             ends in .gz ", "%prog version: $Id$", {kExtra_count_tab, 0}, {kTop_count_tab, 1}, {kGroups_count_tab, 6}, 78, 24, 2, true},
  {"prepare_for_em", "\nprepare_for_em - make output from dedup or group compatible with EM tools\n\nUsage: umi_tools prepare_for_em [OPTIONS] [--stdin=IN_BAM] [--stdout=OUT_BAM]\n\n       note: If --stdout is ommited, standard out is output. To\n             generate a valid BAM file on standard out, please\n             redirect log with --log=LOGFILE or --log2stderr ", "%prog version: $Id$", {kExtra_prepare_for_em, 0}, {kTop_prepare_for_em, 1}, {kGroups_prepare_for_em, 5}, 78, 24, 2, true},
};

}  // namespace

std::span<const ToolSpec> all_tool_specs() {
  return {kTools, sizeof(kTools) / sizeof(kTools[0])};
}

}  // namespace umi_tools
