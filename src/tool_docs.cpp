// GENERATED -- do not edit by hand.
//
// Every tool builds its parser with `description=globals()["__doc__"]`
// (dedup.py:190, group.py:127, count.py:79, extract.py:187, whitelist.py:251,
// count_tab.py:94, prepare_for_em.py:143). optparse's format_help emits
// usage + format_description(description) + option_help, so `--help-extended`
// (action='help') prints the module docstring in full. `-h` does not, because
// callbackShortHelp calls parser.set_description(None) first.
//
// Regenerate with the snippet in validation/parity_help_extended.py's docstring;
// the text is copied verbatim from the installed oracle so it can never drift
// into a hand-transcription.

#include <string_view>

#include "umi_tools/options.hpp"

namespace umi_tools {

std::string_view module_docstring(std::string_view tool) {
  if (tool == "dedup") return R"UMIDOC(
===========================================================
dedup - Deduplicate reads using UMI and mapping coordinates
===========================================================

*Deduplicate reads based on the mapping co-ordinate and the UMI attached to the read*

The identification of duplicate reads is performed in an error-aware
manner by building networks of related UMIs (see
``--method``). ``dedup`` can also handle cell barcoded input (see
``--per-cell``).

Usage::

    umi_tools dedup --stdin=INFILE --log=LOGFILE [OPTIONS] > OUTFILE

Selecting the representative read
---------------------------------
For every group of duplicate reads, a single representative read is
retained.The following criteria are applied to select the read that
will be retained from a group of duplicated reads:

1. The read with the lowest number of mapping coordinates (see
``--multimapping-detection-method`` option)

2. The read with the highest mapping quality. Note that this is not
the read sequencing quality and that if two reads have the same
mapping quality then one will be picked at random regardless of the
read quality.

Otherwise a read is chosen at random.


Dedup-specific options
----------------------
"""""""""""""""""""""""""""
``--output-stats=[PREFIX]``
"""""""""""""""""""""""""""
One can use the edit distance between UMIs at the same position as an
quality control for the deduplication process by comparing with
a null expectation of random sampling. For the random sampling, the
observed frequency of UMIs is used to more reasonably model the null
expectation.

Use this option to generate a stats outfile called:

[PREFIX]_edit_distance.tsv
  Reports the (binned) average edit distance between the UMIs at each
  position. Positions with a single UMI are reported seperately.  The
  edit distances are reported pre- and post-deduplication alongside
  the null expectation from random sampling of UMIs from the UMIs
  observed across all positions. Note that separate null
  distributions are reported since the null depends on the observed
  frequency of each UMI which is different pre- and
  post-deduplication. The post-duplication values should be closer to
  their respective null than the pre-deduplication vs null comparison

In addition, this option will trigger reporting of further summary
statistics for the UMIs which may be informative for selecting the
optimal deduplication method or debugging.

Each unique UMI sequence may be observed [0-many] times at multiple
positions in the BAM. The following files report the distribution for
the frequencies of each UMI.

[PREFIX]_per_umi_per_position.tsv
  The `_per_umi_per_position.tsv` file simply tabulates the
  counts for unique combinations of UMI and position. E.g if prior to
  deduplication, we have two positions in the BAM (POSa, POSb), at
  POSa we have observed 2*UMIa, 1*UMIb and at POSb: 1*UMIc, 3*UMId,
  then the stats file is populated thus:

  ====== =============
  counts instances_pre
  ------ -------------
  1      2
  2      1
  3      1
  ====== =============


  If post deduplication, UMIb is grouped with UMIa such that POSa:
  3*UMIa, then the `instances_post` column is populated thus:

  ====== ============= ==============
  counts instances_pre instances_post
  ------ ------------- --------------
  1      2             1
  2      1             0
  3      1             2
  ====== ============= ==============

[PREFIX]_per_umi.tsv
  The `_per_umi.tsv` table provides UMI-level summary
  statistics. Keeping in mind that each unique UMI sequence can be
  observed at [0-many] times across multiple positions in the BAM,

  :times_observed: How many positions the UMI was observed at
  :total_counts: The total number of times the UMI was observed across all positions
  :median_counts: The median for the distribution of how often the UMI was observed at                  each position (excluding zeros)

  Hence, whenever times_observed=1, total_counts==median_counts.



Extracting barcodes
-------------------

It is assumed that the FASTQ files were processed with `umi_tools
extract` before mapping and thus the UMI is the last word of the read
name. e.g::

    @HISEQ:87:00000000_AATT

where `AATT` is the UMI sequeuence.

If you have used an alternative method which does not separate the
read id and UMI with a "_", such as bcl2fastq which uses ":", you can
specify the separator with the option ``--umi-separator=<sep>``,
replacing <sep> with e.g ":".

Alternatively, if your UMIs are encoded in a tag, you can specify this
by setting the option --extract-umi-method=tag and set the tag name
with the --umi-tag option. For example, if your UMIs are encoded in
the 'UM' tag, provide the following options:
``--extract-umi-method=tag`` ``--umi-tag=UM``

Finally, if you have used umis to extract the UMI +/- cell barcode,
you can specify ``--extract-umi-method=umis``

The start position of a read is considered to be the start of its alignment
minus any soft clipped bases. A read aligned at position 500 with
cigar 2S98M will be assumed to start at position 498.

""""""""""""""""""""""""
``--extract-umi-method``
""""""""""""""""""""""""
      How are the barcodes encoded in the read?

      Options are:

      - read_id (default)
            Barcodes are contained at the end of the read separated as
            specified with ``--umi-separator`` option

      - tag
            Barcodes contained in a tag(s), see ``--umi-tag``/``--cell-tag``
            options

      - umis
            Barcodes were extracted using umis (https://github.com/vals/umis)

"""""""""""""""""""""""""""""""
``--umi-separator=[SEPARATOR]``
"""""""""""""""""""""""""""""""
      Separator between read id and UMI. See ``--extract-umi-method``
      above. Default=``_``

"""""""""""""""""""
``--umi-tag=[TAG]``
"""""""""""""""""""
      Tag which contains UMI. See ``--extract-umi-method`` above

"""""""""""""""""""""""""""
``--umi-tag-split=[SPLIT]``
"""""""""""""""""""""""""""
      Separate the UMI in tag by SPLIT and take the first element

"""""""""""""""""""""""""""""""""""
``--umi-tag-delimiter=[DELIMITER]``
"""""""""""""""""""""""""""""""""""
      Separate the UMI in by DELIMITER and concatenate the elements

""""""""""""""""""""
``--cell-tag=[TAG]``
""""""""""""""""""""
      Tag which contains cell barcode. See `--extract-umi-method` above

""""""""""""""""""""""""""""
``--cell-tag-split=[SPLIT]``
""""""""""""""""""""""""""""
      Separate the cell barcode in tag by SPLIT and take the first element

""""""""""""""""""""""""""""""""""""
``--cell-tag-delimiter=[DELIMITER]``
""""""""""""""""""""""""""""""""""""
      Separate the cell barcode in by DELIMITER and concatenate the elements


UMI grouping options
---------------------------

""""""""""""
``--method``
""""""""""""
    What method to use to identify group of reads with the same (or
    similar) UMI(s)?

    All methods start by identifying the reads with the same mapping position.

    The simplest methods, unique and percentile, group reads with
    the exact same UMI. The network-based methods, cluster, adjacency and
    directional, build networks where nodes are UMIs and edges connect UMIs
    with an edit distance <= threshold (usually 1). The groups of reads
    are then defined from the network in a method-specific manner. For all
    the network-based methods, each read group is equivalent to one read
    count for the gene.

      - unique
          Reads group share the exact same UMI

      - percentile
          Reads group share the exact same UMI. UMIs with counts < 1% of the
          median counts for UMIs at the same position are ignored.

      - cluster
          Identify clusters of connected UMIs (based on hamming distance
          threshold). Each network is a read group

      - adjacency
          Cluster UMIs as above. For each cluster, select the node (UMI)
          with the highest counts. Visit all nodes one edge away. If all
          nodes have been visited, stop. Otherwise, repeat with remaining
          nodes until all nodes have been visted. Each step
          defines a read group.

      - directional (default)
          Identify clusters of connected UMIs (based on hamming distance
          threshold) and umi A counts >= (2* umi B counts) - 1. Each
          network is a read group.

"""""""""""""""""""""""""""""
``--edit-distance-threshold``
"""""""""""""""""""""""""""""
       For the adjacency and cluster methods the threshold for the
       edit distance to connect two UMIs in the network can be
       increased. The default value of 1 works best unless the UMI is
       very long (>14bp).

"""""""""""""""""""""""
``--spliced-is-unique``
"""""""""""""""""""""""
       Causes two reads that start in the same position on the same
       strand and having the same UMI to be considered unique if one is spliced
       and the other is not. (Uses the 'N' cigar operation to test for
       splicing).

"""""""""""""""""""""""""
``--soft-clip-threshold``
"""""""""""""""""""""""""
       Mappers that soft clip will sometimes do so rather than mapping a
       spliced read if there is only a small overhang over the exon
       junction. By setting this option, you can treat reads with at least
       this many bases soft-clipped at the 3' end as spliced. Default=4.

""""""""""""""""""""""""""""""""""""""""""""""
``--multimapping-detection-method=[NH/X0/XT]``
""""""""""""""""""""""""""""""""""""""""""""""
      If the sam/bam contains tags to identify multimapping reads, you can
      specify for use when selecting the best read at a given loci.
      Supported tags are "NH", "X0" and "XT". If not specified, the read
      with the highest mapping quality will be selected.

"""""""""""""""""
``--read-length``
"""""""""""""""""
      Use the read length as a criteria when deduping, for e.g sRNA-Seq.


Single-cell RNA-Seq options
---------------------------

""""""""""""""
``--per-gene``
""""""""""""""
      Reads will be grouped together if they have the same gene.  This
      is useful if your library prep generates PCR duplicates with non
      identical alignment positions such as CEL-Seq. Note this option
      is hardcoded to be on with the count command. I.e counting is
      always performed per-gene. Must be combined with either
      ``--gene-tag`` or ``--per-contig`` option.

""""""""""""""
``--gene-tag``
""""""""""""""
      Deduplicate per gene. The gene information is encoded in the bam
      read tag specified

"""""""""""""""""""""""""
``--assigned-status-tag``
"""""""""""""""""""""""""
      BAM tag which describes whether a read is assigned to a
      gene. Defaults to the same value as given for ``--gene-tag``

"""""""""""""""""""""
``--skip-tags-regex``
"""""""""""""""""""""
      Use in conjunction with the ``--assigned-status-tag`` option to
      skip any reads where the tag matches this regex.  Default
      (``"^[__|Unassigned]"``) matches anything which starts with "__"
      or "Unassigned":

""""""""""""""""
``--per-contig``
""""""""""""""""
      Deduplicate per contig (field 3 in BAM; RNAME).
      All reads with the same contig will be considered to have the
      same alignment position. This is useful if you have aligned to a
      reference transcriptome with one transcript per gene. If you
      have aligned to a transcriptome with more than one transcript
      per gene, you can supply a map between transcripts and gene
      using the ``--gene-transcript-map`` option

"""""""""""""""""""""""""
``--gene-transcript-map``
"""""""""""""""""""""""""
      File mapping genes to transcripts (tab separated), e.g::

          gene1   transcript1
          gene1   transcript2
          gene2   transcript3

""""""""""""""
``--per-cell``
""""""""""""""
      Reads will only be grouped together if they have the same cell
      barcode. Can be combined with ``--per-gene``.

SAM/BAM Options
---------------

"""""""""""""""""""""
``--mapping-quality``
"""""""""""""""""""""
      Minimium mapping quality (MAPQ) for a read to be retained. Default is 0.

""""""""""""""""""""
``--unmapped-reads``
""""""""""""""""""""
     How should unmapped reads be handled. Options are:
      - discard (default)
          Discard all unmapped reads
      - use
          If read2 is unmapped, deduplicate using read1 and output read1 only. Note
          that if read1 is unmapped, read2 will always be descarded irrepsective of
          whether it is mapped. WARNING: May lead to unpaired reads in output. Requires
          ``--paired``
      - output
          Output unmapped reads/read pairs without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""""""
``--chimeric-pairs``
""""""""""""""""""""
     How should chimeric read pairs be handled. Options are:
      - discard
          Discard all chimeric read pairs
      - use (default)
          Deduplicate using read1 information only. Both read1 and read2 should 
          still be output, as long as Read2 is actaully found. Can lead to
          unpaired reads in output if read1 is marked as having a mapped mate,
          but read2 is never found.
      - output
          Output chimeric read pairs without UMI
          grouping/deduplication.  Only available in umi_tools group

""""""""""""""""""""
``--unpaired-reads``
""""""""""""""""""""
     How should unpaired reads be handled. Options are:
      - discard
          Discard all unpaired reads. Note: Can still lead to unpaired
          reads in the output if a read1 is marked as having a mapped
          mate, but the mate is never found. 
      - use (default)
          Deduplicate unpaired reads using read1 only. Note, unpaired read2s will still 
          be discarded. 
      - output
          Output unpaired reads without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""
``--ignore-umi``
""""""""""""""""
      Ignore the UMI and group reads using mapping coordinates only

""""""""""""
``--subset``
""""""""""""
      Only consider a fraction of the reads, chosen at random. This is useful
      for doing saturation analyses.

"""""""""""
``--chrom``
"""""""""""
      Only consider a single chromosome. This is useful for
      debugging/testing purposes

""""""""""""
``--paired``
""""""""""""
       BAM is paired end - output both read pairs. This will also
       force the use of the template length to determine reads with
       the same mapping coordinates.


Group/Dedup options
-------------------

""""""""""""""""""""
``--no-sort-output``
""""""""""""""""""""
       By default, output is sorted. This involves the
       use of a temporary unsorted file since reads are considered in
       the order of their start position which may not be the same
       as their alignment coordinate due to soft-clipping and reverse
       alignments. The temp file will be saved (in ``--temp-dir``) and deleted
       when it has been sorted to the outfile. Use this option to turn
       off sorting.


"""""""""""""""""""""""""
``--buffer-whole-contig``
"""""""""""""""""""""""""
      forces dedup to parse an entire contig before yielding any reads
      for deduplication. This is the only way to absolutely guarantee
      that all reads with the same start position are grouped together
      for deduplication since dedup uses the start position of the
      read, not the alignment coordinate on which the reads are
      sorted. However, by default, dedup reads for another 1000bp
      before outputting read groups which will avoid any reads being
      missed with short read sequencing (<1000bp).


Input/Output Format Options
---------------------

The following options deal with input and output format, and are useful for
outputting CRAM format. In general UMI-tools will attempt to guess the input
and output formats from the file names, but thing can be over-written using
the ``out-format`` and ``input-format`` options. The location of  CRAM 
reference files will be taken from the either the an input CRAM file 
(if present) or from the ``--reference-filename`` option. Otherwise
the reference will be embedded in the file. 

"""""""""""""""""""""""""    
``--in-format=IN_FORMAT``
"""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam

""""""""""""""""""""""""""""""""" 
``--input-options=INPUT_OPTIONS``
"""""""""""""""""""""""""""""""""

      Format string provided to htslib for reading. Mostly
      useful for CRAM formatted files. See samtools
      documentation

"""""""""""""""""""""""
``--in-sam``
"""""""""""""""""""""""
      [DEPRECATED] USE ``--in-format`` . By default, inputs are assumed to be 
      in BAM format. Use this option to specify the use of SAM format for
      input.

""""""""""""""""""""""""""""""""""""""""""" 
``--reference-filename=REFERENCE_FILENAME``
"""""""""""""""""""""""""""""""""""""""""""
      File path or URL to the genome reference to be used
      when reading or writing CRAM files. Can be a path or
      a URL. By default, when reading a CRAM file, the 
      reference recorded in the input file will be used
      unless this is specified. URL references cannot be read
      from input files, however. When writing, specifying a
      reference location is required unless specified in input.


""""""""""""""""""""""""""" 
``--out-format=OUT_FORMAT``
"""""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam


"""""""""""""""""""""""""""""""""""
``--output-options=OUTPUT_OPTIONS``
"""""""""""""""""""""""""""""""""""
      Format string provided to htslib for writing. Mostly
      useful for CRAM formatted files. See samtools
      documentation

)UMIDOC";
  if (tool == "group") return R"UMIDOC(
==============================================================
Group - Group reads based on their UMI and mapping coordinates
==============================================================

*Identify groups of reads based on their genomic coordinate and UMI*

The group command can be used to create two types of outfile: a tagged
BAM or a flatfile describing the read groups

To generate the tagged-BAM file, use the option ``--output-bam`` and
provide a filename with the ``--stdout``/``-S`` option. Alternatively,
if you do not provide a filename, the bam file will be outputted to
the stdout. If you have provided the ``--log``/``-L`` option to send
the logging output elsewhere, you can pipe the output from the group
command directly to e.g samtools view like so::

    umi_tools group -I inf.bam --group-out=grouped.tsv --output-bam
    --log=group.log --paired | samtools view - |less

The tagged-BAM file will have two tagged per read:

 - UG
   Unique_id. 0-indexed unique id number for each group of reads
   with the same genomic position and UMI or UMIs inferred to be
   from the same true UMI + errors
 - BX
   Final UMI. The inferred true UMI for the group

To generate the flatfile describing the read groups, include the
``--group-out=<filename>`` option. The columns of the read groups file are
below. The first five columns relate to the read. The final 3 columns
relate to the group.

  - read_id
      read identifier

  - contig
      alignment contig

  - position
      Alignment position. Note that this position is not the start
      position of the read in the BAM file but the start of the read
      taking into account the read strand and cigar

  - gene
      The gene assignment for the read. Note, this will be NA unless the
      --per-gene option is specified

  - umi
      The read UMI

  - umi_count
      The number of times this UMI is observed for reads at the same
      position

  - final_umi
      The inferred true UMI for the group

  - final_umi_count
      The total number of reads within the group

  - unique_id
      The unique id for the group


group-specific options
----------------------

"""""""""""
--group-out
"""""""""""
   Outfile name for file mapping read id to read group

"""""""""
--out-bam
"""""""""
   Output a bam file with read groups tagged using the UG tag

"""""""""""""""
--umi-group-tag
"""""""""""""""
   BAM tag for the error corrected UMI selected for the group. Default=BX




Extracting barcodes
-------------------

It is assumed that the FASTQ files were processed with `umi_tools
extract` before mapping and thus the UMI is the last word of the read
name. e.g::

    @HISEQ:87:00000000_AATT

where `AATT` is the UMI sequeuence.

If you have used an alternative method which does not separate the
read id and UMI with a "_", such as bcl2fastq which uses ":", you can
specify the separator with the option ``--umi-separator=<sep>``,
replacing <sep> with e.g ":".

Alternatively, if your UMIs are encoded in a tag, you can specify this
by setting the option --extract-umi-method=tag and set the tag name
with the --umi-tag option. For example, if your UMIs are encoded in
the 'UM' tag, provide the following options:
``--extract-umi-method=tag`` ``--umi-tag=UM``

Finally, if you have used umis to extract the UMI +/- cell barcode,
you can specify ``--extract-umi-method=umis``

The start position of a read is considered to be the start of its alignment
minus any soft clipped bases. A read aligned at position 500 with
cigar 2S98M will be assumed to start at position 498.

""""""""""""""""""""""""
``--extract-umi-method``
""""""""""""""""""""""""
      How are the barcodes encoded in the read?

      Options are:

      - read_id (default)
            Barcodes are contained at the end of the read separated as
            specified with ``--umi-separator`` option

      - tag
            Barcodes contained in a tag(s), see ``--umi-tag``/``--cell-tag``
            options

      - umis
            Barcodes were extracted using umis (https://github.com/vals/umis)

"""""""""""""""""""""""""""""""
``--umi-separator=[SEPARATOR]``
"""""""""""""""""""""""""""""""
      Separator between read id and UMI. See ``--extract-umi-method``
      above. Default=``_``

"""""""""""""""""""
``--umi-tag=[TAG]``
"""""""""""""""""""
      Tag which contains UMI. See ``--extract-umi-method`` above

"""""""""""""""""""""""""""
``--umi-tag-split=[SPLIT]``
"""""""""""""""""""""""""""
      Separate the UMI in tag by SPLIT and take the first element

"""""""""""""""""""""""""""""""""""
``--umi-tag-delimiter=[DELIMITER]``
"""""""""""""""""""""""""""""""""""
      Separate the UMI in by DELIMITER and concatenate the elements

""""""""""""""""""""
``--cell-tag=[TAG]``
""""""""""""""""""""
      Tag which contains cell barcode. See `--extract-umi-method` above

""""""""""""""""""""""""""""
``--cell-tag-split=[SPLIT]``
""""""""""""""""""""""""""""
      Separate the cell barcode in tag by SPLIT and take the first element

""""""""""""""""""""""""""""""""""""
``--cell-tag-delimiter=[DELIMITER]``
""""""""""""""""""""""""""""""""""""
      Separate the cell barcode in by DELIMITER and concatenate the elements


UMI grouping options
---------------------------

""""""""""""
``--method``
""""""""""""
    What method to use to identify group of reads with the same (or
    similar) UMI(s)?

    All methods start by identifying the reads with the same mapping position.

    The simplest methods, unique and percentile, group reads with
    the exact same UMI. The network-based methods, cluster, adjacency and
    directional, build networks where nodes are UMIs and edges connect UMIs
    with an edit distance <= threshold (usually 1). The groups of reads
    are then defined from the network in a method-specific manner. For all
    the network-based methods, each read group is equivalent to one read
    count for the gene.

      - unique
          Reads group share the exact same UMI

      - percentile
          Reads group share the exact same UMI. UMIs with counts < 1% of the
          median counts for UMIs at the same position are ignored.

      - cluster
          Identify clusters of connected UMIs (based on hamming distance
          threshold). Each network is a read group

      - adjacency
          Cluster UMIs as above. For each cluster, select the node (UMI)
          with the highest counts. Visit all nodes one edge away. If all
          nodes have been visited, stop. Otherwise, repeat with remaining
          nodes until all nodes have been visted. Each step
          defines a read group.

      - directional (default)
          Identify clusters of connected UMIs (based on hamming distance
          threshold) and umi A counts >= (2* umi B counts) - 1. Each
          network is a read group.

"""""""""""""""""""""""""""""
``--edit-distance-threshold``
"""""""""""""""""""""""""""""
       For the adjacency and cluster methods the threshold for the
       edit distance to connect two UMIs in the network can be
       increased. The default value of 1 works best unless the UMI is
       very long (>14bp).

"""""""""""""""""""""""
``--spliced-is-unique``
"""""""""""""""""""""""
       Causes two reads that start in the same position on the same
       strand and having the same UMI to be considered unique if one is spliced
       and the other is not. (Uses the 'N' cigar operation to test for
       splicing).

"""""""""""""""""""""""""
``--soft-clip-threshold``
"""""""""""""""""""""""""
       Mappers that soft clip will sometimes do so rather than mapping a
       spliced read if there is only a small overhang over the exon
       junction. By setting this option, you can treat reads with at least
       this many bases soft-clipped at the 3' end as spliced. Default=4.

""""""""""""""""""""""""""""""""""""""""""""""
``--multimapping-detection-method=[NH/X0/XT]``
""""""""""""""""""""""""""""""""""""""""""""""
      If the sam/bam contains tags to identify multimapping reads, you can
      specify for use when selecting the best read at a given loci.
      Supported tags are "NH", "X0" and "XT". If not specified, the read
      with the highest mapping quality will be selected.

"""""""""""""""""
``--read-length``
"""""""""""""""""
      Use the read length as a criteria when deduping, for e.g sRNA-Seq.


Single-cell RNA-Seq options
---------------------------

""""""""""""""
``--per-gene``
""""""""""""""
      Reads will be grouped together if they have the same gene.  This
      is useful if your library prep generates PCR duplicates with non
      identical alignment positions such as CEL-Seq. Note this option
      is hardcoded to be on with the count command. I.e counting is
      always performed per-gene. Must be combined with either
      ``--gene-tag`` or ``--per-contig`` option.

""""""""""""""
``--gene-tag``
""""""""""""""
      Deduplicate per gene. The gene information is encoded in the bam
      read tag specified

"""""""""""""""""""""""""
``--assigned-status-tag``
"""""""""""""""""""""""""
      BAM tag which describes whether a read is assigned to a
      gene. Defaults to the same value as given for ``--gene-tag``

"""""""""""""""""""""
``--skip-tags-regex``
"""""""""""""""""""""
      Use in conjunction with the ``--assigned-status-tag`` option to
      skip any reads where the tag matches this regex.  Default
      (``"^[__|Unassigned]"``) matches anything which starts with "__"
      or "Unassigned":

""""""""""""""""
``--per-contig``
""""""""""""""""
      Deduplicate per contig (field 3 in BAM; RNAME).
      All reads with the same contig will be considered to have the
      same alignment position. This is useful if you have aligned to a
      reference transcriptome with one transcript per gene. If you
      have aligned to a transcriptome with more than one transcript
      per gene, you can supply a map between transcripts and gene
      using the ``--gene-transcript-map`` option

"""""""""""""""""""""""""
``--gene-transcript-map``
"""""""""""""""""""""""""
      File mapping genes to transcripts (tab separated), e.g::

          gene1   transcript1
          gene1   transcript2
          gene2   transcript3

""""""""""""""
``--per-cell``
""""""""""""""
      Reads will only be grouped together if they have the same cell
      barcode. Can be combined with ``--per-gene``.

SAM/BAM Options
---------------

"""""""""""""""""""""
``--mapping-quality``
"""""""""""""""""""""
      Minimium mapping quality (MAPQ) for a read to be retained. Default is 0.

""""""""""""""""""""
``--unmapped-reads``
""""""""""""""""""""
     How should unmapped reads be handled. Options are:
      - discard (default)
          Discard all unmapped reads
      - use
          If read2 is unmapped, deduplicate using read1 and output read1 only. Note
          that if read1 is unmapped, read2 will always be descarded irrepsective of
          whether it is mapped. WARNING: May lead to unpaired reads in output. Requires
          ``--paired``
      - output
          Output unmapped reads/read pairs without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""""""
``--chimeric-pairs``
""""""""""""""""""""
     How should chimeric read pairs be handled. Options are:
      - discard
          Discard all chimeric read pairs
      - use (default)
          Deduplicate using read1 information only. Both read1 and read2 should 
          still be output, as long as Read2 is actaully found. Can lead to
          unpaired reads in output if read1 is marked as having a mapped mate,
          but read2 is never found.
      - output
          Output chimeric read pairs without UMI
          grouping/deduplication.  Only available in umi_tools group

""""""""""""""""""""
``--unpaired-reads``
""""""""""""""""""""
     How should unpaired reads be handled. Options are:
      - discard
          Discard all unpaired reads. Note: Can still lead to unpaired
          reads in the output if a read1 is marked as having a mapped
          mate, but the mate is never found. 
      - use (default)
          Deduplicate unpaired reads using read1 only. Note, unpaired read2s will still 
          be discarded. 
      - output
          Output unpaired reads without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""
``--ignore-umi``
""""""""""""""""
      Ignore the UMI and group reads using mapping coordinates only

""""""""""""
``--subset``
""""""""""""
      Only consider a fraction of the reads, chosen at random. This is useful
      for doing saturation analyses.

"""""""""""
``--chrom``
"""""""""""
      Only consider a single chromosome. This is useful for
      debugging/testing purposes

""""""""""""
``--paired``
""""""""""""
       BAM is paired end - output both read pairs. This will also
       force the use of the template length to determine reads with
       the same mapping coordinates.


Group/Dedup options
-------------------

""""""""""""""""""""
``--no-sort-output``
""""""""""""""""""""
       By default, output is sorted. This involves the
       use of a temporary unsorted file since reads are considered in
       the order of their start position which may not be the same
       as their alignment coordinate due to soft-clipping and reverse
       alignments. The temp file will be saved (in ``--temp-dir``) and deleted
       when it has been sorted to the outfile. Use this option to turn
       off sorting.


"""""""""""""""""""""""""
``--buffer-whole-contig``
"""""""""""""""""""""""""
      forces dedup to parse an entire contig before yielding any reads
      for deduplication. This is the only way to absolutely guarantee
      that all reads with the same start position are grouped together
      for deduplication since dedup uses the start position of the
      read, not the alignment coordinate on which the reads are
      sorted. However, by default, dedup reads for another 1000bp
      before outputting read groups which will avoid any reads being
      missed with short read sequencing (<1000bp).


Input/Output Format Options
---------------------

The following options deal with input and output format, and are useful for
outputting CRAM format. In general UMI-tools will attempt to guess the input
and output formats from the file names, but thing can be over-written using
the ``out-format`` and ``input-format`` options. The location of  CRAM 
reference files will be taken from the either the an input CRAM file 
(if present) or from the ``--reference-filename`` option. Otherwise
the reference will be embedded in the file. 

"""""""""""""""""""""""""    
``--in-format=IN_FORMAT``
"""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam

""""""""""""""""""""""""""""""""" 
``--input-options=INPUT_OPTIONS``
"""""""""""""""""""""""""""""""""

      Format string provided to htslib for reading. Mostly
      useful for CRAM formatted files. See samtools
      documentation

"""""""""""""""""""""""
``--in-sam``
"""""""""""""""""""""""
      [DEPRECATED] USE ``--in-format`` . By default, inputs are assumed to be 
      in BAM format. Use this option to specify the use of SAM format for
      input.

""""""""""""""""""""""""""""""""""""""""""" 
``--reference-filename=REFERENCE_FILENAME``
"""""""""""""""""""""""""""""""""""""""""""
      File path or URL to the genome reference to be used
      when reading or writing CRAM files. Can be a path or
      a URL. By default, when reading a CRAM file, the 
      reference recorded in the input file will be used
      unless this is specified. URL references cannot be read
      from input files, however. When writing, specifying a
      reference location is required unless specified in input.


""""""""""""""""""""""""""" 
``--out-format=OUT_FORMAT``
"""""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam


"""""""""""""""""""""""""""""""""""
``--output-options=OUTPUT_OPTIONS``
"""""""""""""""""""""""""""""""""""
      Format string provided to htslib for writing. Mostly
      useful for CRAM formatted files. See samtools
      documentation

)UMIDOC";
  if (tool == "count") return R"UMIDOC(
========================================================================
count - Count reads per gene from BAM using UMIs and mapping coordinates
========================================================================

*Count the number of reads per gene based on the mapping co-ordinate and the UMI attached to the read*

This tool is only designed to work with library preparation
methods where the fragmentation occurs after amplification, as per
most single cell RNA-Seq methods (e.g 10x, inDrop, Drop-seq, SCRB-seq
and CEL-seq2). Since the precise mapping co-ordinate is not longer
informative for such library preparations, it is simplified to the
gene. This is a reasonable approach providing the number of available
UMIs is sufficiently high and the sequencing depth is sufficiently low
that the probability of two reads from the same gene having the same
UMIs is acceptably low.

If you want to count reads per gene for library preparations which
fragment prior to amplification (e.g bulk RNA-Seq), please use
``umi_tools dedup`` to remove the duplicate reads as this will use the
full information from the mapping co-ordinate. Then use a read
counting tool such as FeatureCounts or HTSeq to count the reads per
gene.

In the rare case of bulk RNA-Seq using a library preparation method
with fragmentation after amplification, one can still use ``count`` but
note that it has not been tested on bulk RNA-Seq.

This tool deviates from group and dedup in that the ``--per-gene`` option
is hardcoded on.



Extracting barcodes
-------------------

It is assumed that the FASTQ files were processed with `umi_tools
extract` before mapping and thus the UMI is the last word of the read
name. e.g::

    @HISEQ:87:00000000_AATT

where `AATT` is the UMI sequeuence.

If you have used an alternative method which does not separate the
read id and UMI with a "_", such as bcl2fastq which uses ":", you can
specify the separator with the option ``--umi-separator=<sep>``,
replacing <sep> with e.g ":".

Alternatively, if your UMIs are encoded in a tag, you can specify this
by setting the option --extract-umi-method=tag and set the tag name
with the --umi-tag option. For example, if your UMIs are encoded in
the 'UM' tag, provide the following options:
``--extract-umi-method=tag`` ``--umi-tag=UM``

Finally, if you have used umis to extract the UMI +/- cell barcode,
you can specify ``--extract-umi-method=umis``

The start position of a read is considered to be the start of its alignment
minus any soft clipped bases. A read aligned at position 500 with
cigar 2S98M will be assumed to start at position 498.

""""""""""""""""""""""""
``--extract-umi-method``
""""""""""""""""""""""""
      How are the barcodes encoded in the read?

      Options are:

      - read_id (default)
            Barcodes are contained at the end of the read separated as
            specified with ``--umi-separator`` option

      - tag
            Barcodes contained in a tag(s), see ``--umi-tag``/``--cell-tag``
            options

      - umis
            Barcodes were extracted using umis (https://github.com/vals/umis)

"""""""""""""""""""""""""""""""
``--umi-separator=[SEPARATOR]``
"""""""""""""""""""""""""""""""
      Separator between read id and UMI. See ``--extract-umi-method``
      above. Default=``_``

"""""""""""""""""""
``--umi-tag=[TAG]``
"""""""""""""""""""
      Tag which contains UMI. See ``--extract-umi-method`` above

"""""""""""""""""""""""""""
``--umi-tag-split=[SPLIT]``
"""""""""""""""""""""""""""
      Separate the UMI in tag by SPLIT and take the first element

"""""""""""""""""""""""""""""""""""
``--umi-tag-delimiter=[DELIMITER]``
"""""""""""""""""""""""""""""""""""
      Separate the UMI in by DELIMITER and concatenate the elements

""""""""""""""""""""
``--cell-tag=[TAG]``
""""""""""""""""""""
      Tag which contains cell barcode. See `--extract-umi-method` above

""""""""""""""""""""""""""""
``--cell-tag-split=[SPLIT]``
""""""""""""""""""""""""""""
      Separate the cell barcode in tag by SPLIT and take the first element

""""""""""""""""""""""""""""""""""""
``--cell-tag-delimiter=[DELIMITER]``
""""""""""""""""""""""""""""""""""""
      Separate the cell barcode in by DELIMITER and concatenate the elements


UMI grouping options
---------------------------

""""""""""""
``--method``
""""""""""""
    What method to use to identify group of reads with the same (or
    similar) UMI(s)?

    All methods start by identifying the reads with the same mapping position.

    The simplest methods, unique and percentile, group reads with
    the exact same UMI. The network-based methods, cluster, adjacency and
    directional, build networks where nodes are UMIs and edges connect UMIs
    with an edit distance <= threshold (usually 1). The groups of reads
    are then defined from the network in a method-specific manner. For all
    the network-based methods, each read group is equivalent to one read
    count for the gene.

      - unique
          Reads group share the exact same UMI

      - percentile
          Reads group share the exact same UMI. UMIs with counts < 1% of the
          median counts for UMIs at the same position are ignored.

      - cluster
          Identify clusters of connected UMIs (based on hamming distance
          threshold). Each network is a read group

      - adjacency
          Cluster UMIs as above. For each cluster, select the node (UMI)
          with the highest counts. Visit all nodes one edge away. If all
          nodes have been visited, stop. Otherwise, repeat with remaining
          nodes until all nodes have been visted. Each step
          defines a read group.

      - directional (default)
          Identify clusters of connected UMIs (based on hamming distance
          threshold) and umi A counts >= (2* umi B counts) - 1. Each
          network is a read group.

"""""""""""""""""""""""""""""
``--edit-distance-threshold``
"""""""""""""""""""""""""""""
       For the adjacency and cluster methods the threshold for the
       edit distance to connect two UMIs in the network can be
       increased. The default value of 1 works best unless the UMI is
       very long (>14bp).

"""""""""""""""""""""""
``--spliced-is-unique``
"""""""""""""""""""""""
       Causes two reads that start in the same position on the same
       strand and having the same UMI to be considered unique if one is spliced
       and the other is not. (Uses the 'N' cigar operation to test for
       splicing).

"""""""""""""""""""""""""
``--soft-clip-threshold``
"""""""""""""""""""""""""
       Mappers that soft clip will sometimes do so rather than mapping a
       spliced read if there is only a small overhang over the exon
       junction. By setting this option, you can treat reads with at least
       this many bases soft-clipped at the 3' end as spliced. Default=4.

""""""""""""""""""""""""""""""""""""""""""""""
``--multimapping-detection-method=[NH/X0/XT]``
""""""""""""""""""""""""""""""""""""""""""""""
      If the sam/bam contains tags to identify multimapping reads, you can
      specify for use when selecting the best read at a given loci.
      Supported tags are "NH", "X0" and "XT". If not specified, the read
      with the highest mapping quality will be selected.

"""""""""""""""""
``--read-length``
"""""""""""""""""
      Use the read length as a criteria when deduping, for e.g sRNA-Seq.


Single-cell RNA-Seq options
---------------------------

""""""""""""""
``--per-gene``
""""""""""""""
      Reads will be grouped together if they have the same gene.  This
      is useful if your library prep generates PCR duplicates with non
      identical alignment positions such as CEL-Seq. Note this option
      is hardcoded to be on with the count command. I.e counting is
      always performed per-gene. Must be combined with either
      ``--gene-tag`` or ``--per-contig`` option.

""""""""""""""
``--gene-tag``
""""""""""""""
      Deduplicate per gene. The gene information is encoded in the bam
      read tag specified

"""""""""""""""""""""""""
``--assigned-status-tag``
"""""""""""""""""""""""""
      BAM tag which describes whether a read is assigned to a
      gene. Defaults to the same value as given for ``--gene-tag``

"""""""""""""""""""""
``--skip-tags-regex``
"""""""""""""""""""""
      Use in conjunction with the ``--assigned-status-tag`` option to
      skip any reads where the tag matches this regex.  Default
      (``"^[__|Unassigned]"``) matches anything which starts with "__"
      or "Unassigned":

""""""""""""""""
``--per-contig``
""""""""""""""""
      Deduplicate per contig (field 3 in BAM; RNAME).
      All reads with the same contig will be considered to have the
      same alignment position. This is useful if you have aligned to a
      reference transcriptome with one transcript per gene. If you
      have aligned to a transcriptome with more than one transcript
      per gene, you can supply a map between transcripts and gene
      using the ``--gene-transcript-map`` option

"""""""""""""""""""""""""
``--gene-transcript-map``
"""""""""""""""""""""""""
      File mapping genes to transcripts (tab separated), e.g::

          gene1   transcript1
          gene1   transcript2
          gene2   transcript3

""""""""""""""
``--per-cell``
""""""""""""""
      Reads will only be grouped together if they have the same cell
      barcode. Can be combined with ``--per-gene``.

SAM/BAM Options
---------------

"""""""""""""""""""""
``--mapping-quality``
"""""""""""""""""""""
      Minimium mapping quality (MAPQ) for a read to be retained. Default is 0.

""""""""""""""""""""
``--unmapped-reads``
""""""""""""""""""""
     How should unmapped reads be handled. Options are:
      - discard (default)
          Discard all unmapped reads
      - use
          If read2 is unmapped, deduplicate using read1 and output read1 only. Note
          that if read1 is unmapped, read2 will always be descarded irrepsective of
          whether it is mapped. WARNING: May lead to unpaired reads in output. Requires
          ``--paired``
      - output
          Output unmapped reads/read pairs without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""""""
``--chimeric-pairs``
""""""""""""""""""""
     How should chimeric read pairs be handled. Options are:
      - discard
          Discard all chimeric read pairs
      - use (default)
          Deduplicate using read1 information only. Both read1 and read2 should 
          still be output, as long as Read2 is actaully found. Can lead to
          unpaired reads in output if read1 is marked as having a mapped mate,
          but read2 is never found.
      - output
          Output chimeric read pairs without UMI
          grouping/deduplication.  Only available in umi_tools group

""""""""""""""""""""
``--unpaired-reads``
""""""""""""""""""""
     How should unpaired reads be handled. Options are:
      - discard
          Discard all unpaired reads. Note: Can still lead to unpaired
          reads in the output if a read1 is marked as having a mapped
          mate, but the mate is never found. 
      - use (default)
          Deduplicate unpaired reads using read1 only. Note, unpaired read2s will still 
          be discarded. 
      - output
          Output unpaired reads without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""
``--ignore-umi``
""""""""""""""""
      Ignore the UMI and group reads using mapping coordinates only

""""""""""""
``--subset``
""""""""""""
      Only consider a fraction of the reads, chosen at random. This is useful
      for doing saturation analyses.

"""""""""""
``--chrom``
"""""""""""
      Only consider a single chromosome. This is useful for
      debugging/testing purposes

""""""""""""
``--paired``
""""""""""""
       BAM is paired end - output both read pairs. This will also
       force the use of the template length to determine reads with
       the same mapping coordinates.


Input/Output Format Options
---------------------

The following options deal with input and output format, and are useful for
outputting CRAM format. In general UMI-tools will attempt to guess the input
and output formats from the file names, but thing can be over-written using
the ``out-format`` and ``input-format`` options. The location of  CRAM 
reference files will be taken from the either the an input CRAM file 
(if present) or from the ``--reference-filename`` option. Otherwise
the reference will be embedded in the file. 

"""""""""""""""""""""""""    
``--in-format=IN_FORMAT``
"""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam

""""""""""""""""""""""""""""""""" 
``--input-options=INPUT_OPTIONS``
"""""""""""""""""""""""""""""""""

      Format string provided to htslib for reading. Mostly
      useful for CRAM formatted files. See samtools
      documentation

"""""""""""""""""""""""
``--in-sam``
"""""""""""""""""""""""
      [DEPRECATED] USE ``--in-format`` . By default, inputs are assumed to be 
      in BAM format. Use this option to specify the use of SAM format for
      input.

""""""""""""""""""""""""""""""""""""""""""" 
``--reference-filename=REFERENCE_FILENAME``
"""""""""""""""""""""""""""""""""""""""""""
      File path or URL to the genome reference to be used
      when reading or writing CRAM files. Can be a path or
      a URL. By default, when reading a CRAM file, the 
      reference recorded in the input file will be used
      unless this is specified. URL references cannot be read
      from input files, however. When writing, specifying a
      reference location is required unless specified in input.

)UMIDOC";
  if (tool == "extract") return R"UMIDOC(
================================
extract - Extract UMI from fastq
================================

*Extract UMI barcode from a read and add it to the read name, leaving
any sample barcode in place*

Can deal with paired end reads and UMIs
split across the paired ends. Can also optionally extract cell
barcodes and append these to the read name also. See the section below
for an explanation for how to encode the barcode pattern(s) to
specficy the position of the UMI +/- cell barcode.

Usage:
------

For single ended reads, the following reads from stdin and outputs to
stdout::

        umi_tools extract --extract-method=string
        --bc-pattern=[PATTERN] -L extract.log [OPTIONS]

For paired end reads, the following reads end one from stdin and end
two from FASTQIN and outputs end one to stdout and end two to
FASTQOUT::

        umi_tools extract --extract-method=string
        --bc-pattern=[PATTERN] --bc-pattern2=[PATTERN]
        --read2-in=[FASTQIN] --read2-out=[FASTQOUT] -L extract.log [OPTIONS]

Using regex and filtering against a whitelist of cell barcodes::

        umi_tools extract --extract-method=regex
        --bc-pattern=[REGEX] --whitlist=[WHITELIST_TSV]
        -L extract.log [OPTIONS]


Filtering and correcting cell barcodes
--------------------------------------

umi_tools extract can optionally filter cell barcodes against a user-supplied
whitelist (``--whitelist``). If a whitelist is not available for your data, e.g
if you have performed droplet-based scRNA-Seq, you can use the
whitelist tool.

Cell barcodes which do not match the whitelist (user-generated or
automatically generated) can also be optionally corrected using the
``--error-correct-cell`` option.

""""""""""""""""""""""""
``--error-correct-cell``
""""""""""""""""""""""""
     Error correct cell barcodes to the whitelist (see ``--whitelist``)

"""""""""""""""
``--whitelist``
"""""""""""""""
     Whitelist of accepted cell barcodes. The whitelist should be in
     the following format (tab-separated)::

        AAAAAA    AGAAAA
        AAAATC
        AAACAT
        AAACTA    AAACTN,GAACTA
        AAATAC
        AAATCA    GAATCA
        AAATGT    AAAGGT,CAATGT

    Where column 1 is the whitelisted cell barcodes and column 2 is
    the list (comma-separated) of other cell barcodes which should be
    corrected to the barcode in column 1. If the ``--error-correct-cell``
    option is not used, this column will be ignored. Any additional columns
    in the whitelist input, such as the counts columns from the output of
    umi_tools whitelist, will be ignored.

"""""""""""""""
``--blacklist``
"""""""""""""""
    BlackWhitelist of cell barcodes to discard

""""""""""""""""""""""
``--subset-reads=[N]``
""""""""""""""""""""""
    Only parse the first N reads

""""""""""""""""""""""""""""""
``--quality-filter-threshold``
""""""""""""""""""""""""""""""
    Remove reads where any UMI base quality score falls below this threshold

"""""""""""""""""""""""""
``--quality-filter-mask``
"""""""""""""""""""""""""
    If a UMI base has a quality below this threshold, replace the base with 'N'

""""""""""""""""""""""
``--quality-encoding``
""""""""""""""""""""""
    Quality score encoding. Choose from:
     - 'phred33' [33-77]
     - 'phred64' [64-106]
     - 'solexa' [59-106]

"""""""""""""""""""""
``--reconcile-pairs``
"""""""""""""""""""""
    Allow read 2 infile to contain reads not in read 1 infile. This
    enables support for upstream protocols where read one contains
    cell barcodes, and the read pairs have been filtered and corrected
    without regard to the read2s



Experimental options
--------------------

.. note:: These options have not been extensively testing to ensure behaviour is as expected. If you have some suitable input files which we can use for testing, please `contact us <https://github.com/CGATOxford/UMI-tools/issues>`_.

If you have a library preparation method where the UMI may be in
either read, you can use the following options to search for the UMI
in either read::

       --either-read --extract-method --bc-pattern=[PATTERN1] --bc-pattern2=[PATTERN2]

Where both patterns match, the default behaviour is to discard both
reads. If you want to select the read with the UMI with highest
sequence quality, provide ``--either-read-resolve=quality.``





Barcode extraction
------------------

""""""""""""""""
``--bc-pattern``
""""""""""""""""
      Pattern for barcode(s) on read 1. See ``--extract-method``

"""""""""""""""""
``--bc-pattern2``
"""""""""""""""""
      Pattern for barcode(s) on read 2. See ``--extract-method``

""""""""""""""""""""
``--extract-method``
""""""""""""""""""""
      There are two methods enabled to extract the umi barcode (+/-
      cell barcode). For both methods, the patterns should be provided
      using the ``--bc-pattern`` and ``--bc-pattern2`` options.x

 - ``string``
       This should be used where the barcodes are always in the same
       place in the read.

       - N = UMI position (required)
       - C = cell barcode position (optional)
       - X = sample position (optional)

       Bases with Ns and Cs will be extracted and added to the read
       name. The corresponding sequence qualities will be removed from
       the read. Bases with an X will be reattached to the read.

       E.g. If the pattern is `NNNNCC`,
       Then the read::

           @HISEQ:87:00000000 read1
           AAGGTTGCTGATTGGATGGGCTAG
           +
           DA1AEBFGGCG01DFH00B1FF0B

       will become::

           @HISEQ:87:00000000_TT_AAGG read1
           GCTGATTGGATGGGCTAG
           +
           1AFGGCG01DFH00B1FF0B

       where 'TT' is the cell barcode and 'AAGG' is the UMI.

 - ``regex``
       This method allows for more flexible barcode extraction and
       should be used where the cell barcodes are variable in
       length. Alternatively, the regex option can also be used to
       filter out reads which do not contain an expected adapter
       sequence. UMI-tools uses the regex module rather than the more
       standard re module since the former also enables fuzzy matching

       The regex must contain groups to define how the barcodes are
       encoded in the read. The expected groups in the regex are:

       umi_n = UMI positions, where n can be any value (required)
       cell_n = cell barcode positions, where n can be any value (optional)
       discard_n = positions to discard, where n can be any value (optional)

       UMI positions and cell barcode positions will be extracted and
       added to the read name. The corresponding sequence qualities
       will be removed from the read.

       Discard bases and the corresponding quality scores will be
       removed from the read. All bases matched by other groups or
       components of the regex will be reattached to the read sequence

       For example, the following regex can be used to extract reads
       from the Klein et al inDrop data::

           (?P<cell_1>.{8,12})(?P<discard_1>GAGTGATTGCTTGTGACGCCTT)(?P<cell_2>.{8})(?P<umi_1>.{6})T{3}.*

       Where only reads with a 3' T-tail and `GAGTGATTGCTTGTGACGCCTT` in
       the correct position to yield two cell barcodes of 8-12 and 8bp
       respectively, and a 6bp UMI will be retained.

       You can also specify fuzzy matching to allow errors. For example if
       the discard group above was specified as below this would enable
       matches with up to 2 errors in the discard_1 group.

       ::

           (?P<discard_1>GAGTGATTGCTTGTGACGCCTT){s<=2}

       Note that all UMIs must be the same length for downstream
       processing with dedup, group or count commands


""""""""""""
``--3prime``
""""""""""""
       By default the barcode is assumed to be on the 5' end of the
       read, but use this option to sepecify that it is on the 3' end
       instead. This option only works with ``--extract-method=string``
       since 3' encoding can be specified explicitly with a regex, e.g
       ``.*(?P<umi_1>.{5})$``

""""""""""""""
``--read2-in``
""""""""""""""
        Filename for read pairs

""""""""""""""""""
``--filtered-out``
""""""""""""""""""
        Write out reads not matching regex pattern or cell barcode
        whitelist to this file

"""""""""""""""""""
``--filtered-out2``
"""""""""""""""""""
        Write out read pairs not matching regex pattern or cell barcode
        whitelist to this file

""""""""""""""
``--ignore-read-pair-suffixes``
""""""""""""""
       Ignore  and  read name suffixes. Note that this options is
       required if the suffixes are not whitespace separated from the
       rest of the read name
)UMIDOC";
  if (tool == "whitelist") return R"UMIDOC(==================================================
whitelist - Identify the likely true cell barcodes
==================================================

*Extract cell barcodes and identify the most likely true cell barcodes*

Usage:
------

For single ended reads, the following reads from stdin and outputs to stdout::

        umi_tools whitelist --bc-pattern=[PATTERN] -L extract.log
        [OPTIONS]

For paired end reads where the cell barcodes is split across the read
pairs, the following reads end one from stdin and end two from FASTQIN
and outputs to stdin::

        umi_tools whitelist --bc-pattern=[PATTERN]
        --bc-pattern2=[PATTERN] --read2-in=[FASTQIN] -L extract.log
        [OPTIONS]


Output:
-------

The whitelist is outputted as 4 tab-separated columns:

    1. whitelisted cell barcode
    2. Other cell barcode(s) (comma-separated) to correct to the
       whitelisted barcode
    3. Count for whitelisted cell barcodes
    4. Count(s) for the other cell barcode(s) (comma-separated)

example output::

    AAAAAA      AGAAAA          146	1
    AAAATC		        22
    AAACAT		        21
    AAACTA	AAACTN,GAACTA	27	1,1
    AAATAC		        72
    AAATCA	GAATCA	        37	3
    AAATGT	AAAGGT,CAATGT	41	1,1
    AAATTG	CAATTG	        36	1
    AACAAT		        18
    AACATA		        24

If ``--error-correct-threshold`` is set to 0, columns 2 and 4 will be empty.

Identifying the true cell barcodes
----------------------------------

In the absence of the ``--set-cell-number`` option, ``whitelist``
finds the knee in the curve for the cumulative read counts per CB or
unique UMIs per CB (``--method=[reads|umis]``). This point is referred
to as the 'knee'. Previously this point was identified using the
distribution of read counts per CB or unique UMIs per CB. The old
behaviour can be activated using ``--knee-method=density``

See this blog post for a more detailed exploration of the previous method:

https://cgatoxford.wordpress.com/2017/05/18/estimating-the-number-of-true-cell-barcodes-in-single-cell-rna-seq/

Counts per cell barcode can be performed using either read or unique
UMI counts. Use ``--method=[read|umis]`` to set the counting method.

The process of selecting the "best" local minima with
``--knee-method=density`` is not completely foolproof. We recommend
users always run whitelist with the ``--plot-prefix`` option to
visualise the set of thresholds considered for defining cell
barcodes. This option will also generate a table containing the
thresholds which were rejected if you want to manually adjust the
threshold. In addition, if you expect that a local minima will not be
found, you can use the ``--allow-threshold-error`` option to allow
``whitelist`` to proceed proceed past this stage. In addition, if you
have some prior expectation on the maximum number of cells which may
have been sequenced, you can provide this using the option
``--expect-cells`` (see below).

If you don't mind if ``whitelist --knee-method=density`` cannot
identify a suitable threshold as you intend to inspect the plots and
identify the threshold manually, provide the following options:
``--allow-threshold-error``, ``--plot-prefix=[PLOT_PREFIX]``

We expect that the default distance-based knee method should be more
robust than the density-based method. However, we haven't extensively
tested this method. If you have a dataset where you believe the
density-based method is better, please share this information with us:
https://github.com/CGATOxford/UMI-tools/issues

Finally, in some datasets there may be a risk that CBs above the
selected threshold are actually errors from another CB. We can detect
potential instances of this by looking for CBs within one error
(substition, insertion or deletion) of another CB with higher
counts. One can then either take a conservate approach (remove CB with
lower counts), or a more relaxed approach (correct CB with lower
counts to CB with higher counts). Note that correction is only
possible for substitutions since insertions & deletions may also affect
the UMI so these are always discarded. See
``--ed-above-threshold=[discard/correct]`` below. Of course, the risk
with the relaxed approach is that this may erroneously merge two truly
different CBs together and create an in-silico "doublet". The end of
the log file (--log) will detail the number of reads from CBs above
the threshold which may be errors. In most cases, we expect the number
of reads to be a very small fraction of the total reads and therefore
recommend taking the conservative approach. See
https://cgatoxford.wordpress.com/2017/05/23/estimating-the-number-of-true-cell-barcodes-in-single-cell-rna-seq-part-2/
for an analysis of errors in barcodes above the knee threshold.


whitelist-specific options
--------------------------

""""""""""""
``--method``
""""""""""""
       "reads" or "umis". Use either reads or unique UMI counts per cell

""""""""""""
``--knee-method``
""""""""""""
       "distance" or "density". Two methods are available to detect
       the 'knee' in the cell barcode count distributions. "distance"
       identifies the maximum distance between the cumulative
       distribution curve and a straight line between the first and
       last points on the cumulative distribution curve. "density"
       transforms the counts per UMI into a gaussian density and then
       finds the local minima which separates "real" from "error" cell
       barcodes. The gaussian method was the only method available
       prior to UMI-tools v1.0.0. "distance" is now the default
       method.

"""""""""""""""""""""
``--set-cell-number``
"""""""""""""""""""""
        Use this option to explicity set the number of cell barcodes
        which should be accepted. Note that the exact number of cell
        barcodes in the outputted whitelist may be slightly less than
        this if there are multiple cells observed with the same
        frequency at the threshold between accepted and rejected cell
        barcodes.

""""""""""""""""""
``--expect-cells``
""""""""""""""""""
        An upper limit estimate for the number of inputted cells. The
        knee method will now select the first threshold (order
        ascendingly) which results in the number of cell barcodes
        accepted being <= EXPECTED_CELLS and > EXPECTED_CELLS *
        0.1. Note: This is not compatible with the default
        ``--knee-method=distance`` since there is always as single
        solution using this method.

"""""""""""""""""""""""""""
``--allow-threshold-error``
"""""""""""""""""""""""""""
        This is useful if you what the command to exit with just a
        warning if a suitable threshold cannot be selected

"""""""""""""""""""""""""""""
``--error-correct-threshold``
"""""""""""""""""""""""""""""
       Hamming distance for correction of barcodes to whitelist
       barcodes. This value will also be used for error detection
       above the knee if required (``--ed-above-threshold``)

"""""""""""""""""
``--plot-prefix``
"""""""""""""""""
        Use this option to indicate the prefix for the plots and table
        describing the set of thresholds considered for defining cell barcodes

""""""""""""""""""""""""""""""""""""""""""
``--ed-above-threshold=[discard|correct]``
""""""""""""""""""""""""""""""""""""""""""
        Detect CBs above the threshold which may be sequence
        errors:
            - "discard"
                  Discard all putative error CBs.
            - "correct"
                  Correct putative substituion errors in CBs above the
                  threshold. Discard putative insertions/deletions. Note that
                  correction is only possible when the CB contains only
                  substituions since insertions and deletions may cause errors
                  in the UMI sequence too

        Where a CB could be corrected to two other CBs, correction is
        not possible. In these cases, the CB will be discarded
        regardless of which option is used.

""""""""""""""""""
``--subset-reads``
""""""""""""""""""
        Use the first N reads to automatically identify the true cell
        barcodes. If N is greater than the number of reads, all reads
        will be used. Default is 100000000 (100 Million).




Barcode extraction
------------------

""""""""""""""""
``--bc-pattern``
""""""""""""""""
      Pattern for barcode(s) on read 1. See ``--extract-method``

"""""""""""""""""
``--bc-pattern2``
"""""""""""""""""
      Pattern for barcode(s) on read 2. See ``--extract-method``

""""""""""""""""""""
``--extract-method``
""""""""""""""""""""
      There are two methods enabled to extract the umi barcode (+/-
      cell barcode). For both methods, the patterns should be provided
      using the ``--bc-pattern`` and ``--bc-pattern2`` options.x

 - ``string``
       This should be used where the barcodes are always in the same
       place in the read.

       - N = UMI position (required)
       - C = cell barcode position (optional)
       - X = sample position (optional)

       Bases with Ns and Cs will be extracted and added to the read
       name. The corresponding sequence qualities will be removed from
       the read. Bases with an X will be reattached to the read.

       E.g. If the pattern is `NNNNCC`,
       Then the read::

           @HISEQ:87:00000000 read1
           AAGGTTGCTGATTGGATGGGCTAG
           +
           DA1AEBFGGCG01DFH00B1FF0B

       will become::

           @HISEQ:87:00000000_TT_AAGG read1
           GCTGATTGGATGGGCTAG
           +
           1AFGGCG01DFH00B1FF0B

       where 'TT' is the cell barcode and 'AAGG' is the UMI.

 - ``regex``
       This method allows for more flexible barcode extraction and
       should be used where the cell barcodes are variable in
       length. Alternatively, the regex option can also be used to
       filter out reads which do not contain an expected adapter
       sequence. UMI-tools uses the regex module rather than the more
       standard re module since the former also enables fuzzy matching

       The regex must contain groups to define how the barcodes are
       encoded in the read. The expected groups in the regex are:

       umi_n = UMI positions, where n can be any value (required)
       cell_n = cell barcode positions, where n can be any value (optional)
       discard_n = positions to discard, where n can be any value (optional)

       UMI positions and cell barcode positions will be extracted and
       added to the read name. The corresponding sequence qualities
       will be removed from the read.

       Discard bases and the corresponding quality scores will be
       removed from the read. All bases matched by other groups or
       components of the regex will be reattached to the read sequence

       For example, the following regex can be used to extract reads
       from the Klein et al inDrop data::

           (?P<cell_1>.{8,12})(?P<discard_1>GAGTGATTGCTTGTGACGCCTT)(?P<cell_2>.{8})(?P<umi_1>.{6})T{3}.*

       Where only reads with a 3' T-tail and `GAGTGATTGCTTGTGACGCCTT` in
       the correct position to yield two cell barcodes of 8-12 and 8bp
       respectively, and a 6bp UMI will be retained.

       You can also specify fuzzy matching to allow errors. For example if
       the discard group above was specified as below this would enable
       matches with up to 2 errors in the discard_1 group.

       ::

           (?P<discard_1>GAGTGATTGCTTGTGACGCCTT){s<=2}

       Note that all UMIs must be the same length for downstream
       processing with dedup, group or count commands


""""""""""""
``--3prime``
""""""""""""
       By default the barcode is assumed to be on the 5' end of the
       read, but use this option to sepecify that it is on the 3' end
       instead. This option only works with ``--extract-method=string``
       since 3' encoding can be specified explicitly with a regex, e.g
       ``.*(?P<umi_1>.{5})$``

""""""""""""""
``--read2-in``
""""""""""""""
        Filename for read pairs

""""""""""""""""""
``--filtered-out``
""""""""""""""""""
        Write out reads not matching regex pattern or cell barcode
        whitelist to this file

"""""""""""""""""""
``--filtered-out2``
"""""""""""""""""""
        Write out read pairs not matching regex pattern or cell barcode
        whitelist to this file

""""""""""""""
``--ignore-read-pair-suffixes``
""""""""""""""
       Ignore  and  read name suffixes. Note that this options is
       required if the suffixes are not whitespace separated from the
       rest of the read name
)UMIDOC";
  if (tool == "count_tab") return R"UMIDOC(
count_tab - Count reads per gene from flatfile using UMIs
=================================================================

Purpose
-------

The purpose of this command is to count the number of reads per gene
based on the read's gene assignment and UMI. See the count command if
you want to perform per-cell counting using a BAM file input.

The input must be in the following format (tab separated), where the
first column is the read identifier (including UMI) and the second
column is the assigned gene. The input must be sorted by the gene
identifier.

Input template::

    read_id[SEP]_UMI    gene

Example::

    NS500668:144:H5FCJBGXY:2:22309:18356:15843_TCTAA     ENSG00000279457.3
    NS500668:144:H5FCJBGXY:3:23405:39715:19716_CGATG     ENSG00000225972.1

You can perform any required file transformation and pipe the output
directly to count_tab. For example to pipe output from featureCounts
with the '-R CORE' option you can do the following::

    awk '$2=="Assigned" {print $1"	"$4}' my.bam.featureCounts | sort -k2 |
    umi_tools count_tab -S gene_counts.tsv -L count.log

The tab file is assumed to contain each read id once only. For paired
end reads with featureCounts you must include the "-p" option so each
read id is included once only.

Per-cell counting can be enable with ``--per-cell``. For per-cell
counting, the input must be in the following format (tab separated),
where the first column is the read identifier (including UMI and Cell
Barcode) and the second column is the assigned gene. The input must be
sorted by the gene identifier:

Input template::

    read_id[SEP]_CB_UMI    gene

Example::

    NS500668:144:H5FCJBGXY:2:22309:18356:15843_AGTCGA_TCTAA     ENSG00000279457.3
    NS500668:144:H5FCJBGXY:3:23405:39715:19716_GGAGAA_CGATG     ENSG00000225972.1



Extracting barcodes
-------------------

It is assumed that the FASTQ files were processed with `umi_tools
extract` before mapping and thus the UMI is the last word of the read
name. e.g::

    @HISEQ:87:00000000_AATT

where `AATT` is the UMI sequeuence.

If you have used an alternative method which does not separate the
read id and UMI with a "_", such as bcl2fastq which uses ":", you can
specify the separator with the option ``--umi-separator=<sep>``,
replacing <sep> with e.g ":".

Alternatively, if your UMIs are encoded in a tag, you can specify this
by setting the option --extract-umi-method=tag and set the tag name
with the --umi-tag option. For example, if your UMIs are encoded in
the 'UM' tag, provide the following options:
``--extract-umi-method=tag`` ``--umi-tag=UM``

Finally, if you have used umis to extract the UMI +/- cell barcode,
you can specify ``--extract-umi-method=umis``

The start position of a read is considered to be the start of its alignment
minus any soft clipped bases. A read aligned at position 500 with
cigar 2S98M will be assumed to start at position 498.

""""""""""""""""""""""""
``--extract-umi-method``
""""""""""""""""""""""""
      How are the barcodes encoded in the read?

      Options are:

      - read_id (default)
            Barcodes are contained at the end of the read separated as
            specified with ``--umi-separator`` option

      - tag
            Barcodes contained in a tag(s), see ``--umi-tag``/``--cell-tag``
            options

      - umis
            Barcodes were extracted using umis (https://github.com/vals/umis)

"""""""""""""""""""""""""""""""
``--umi-separator=[SEPARATOR]``
"""""""""""""""""""""""""""""""
      Separator between read id and UMI. See ``--extract-umi-method``
      above. Default=``_``

"""""""""""""""""""
``--umi-tag=[TAG]``
"""""""""""""""""""
      Tag which contains UMI. See ``--extract-umi-method`` above

"""""""""""""""""""""""""""
``--umi-tag-split=[SPLIT]``
"""""""""""""""""""""""""""
      Separate the UMI in tag by SPLIT and take the first element

"""""""""""""""""""""""""""""""""""
``--umi-tag-delimiter=[DELIMITER]``
"""""""""""""""""""""""""""""""""""
      Separate the UMI in by DELIMITER and concatenate the elements

""""""""""""""""""""
``--cell-tag=[TAG]``
""""""""""""""""""""
      Tag which contains cell barcode. See `--extract-umi-method` above

""""""""""""""""""""""""""""
``--cell-tag-split=[SPLIT]``
""""""""""""""""""""""""""""
      Separate the cell barcode in tag by SPLIT and take the first element

""""""""""""""""""""""""""""""""""""
``--cell-tag-delimiter=[DELIMITER]``
""""""""""""""""""""""""""""""""""""
      Separate the cell barcode in by DELIMITER and concatenate the elements


UMI grouping options
---------------------------

""""""""""""
``--method``
""""""""""""
    What method to use to identify group of reads with the same (or
    similar) UMI(s)?

    All methods start by identifying the reads with the same mapping position.

    The simplest methods, unique and percentile, group reads with
    the exact same UMI. The network-based methods, cluster, adjacency and
    directional, build networks where nodes are UMIs and edges connect UMIs
    with an edit distance <= threshold (usually 1). The groups of reads
    are then defined from the network in a method-specific manner. For all
    the network-based methods, each read group is equivalent to one read
    count for the gene.

      - unique
          Reads group share the exact same UMI

      - percentile
          Reads group share the exact same UMI. UMIs with counts < 1% of the
          median counts for UMIs at the same position are ignored.

      - cluster
          Identify clusters of connected UMIs (based on hamming distance
          threshold). Each network is a read group

      - adjacency
          Cluster UMIs as above. For each cluster, select the node (UMI)
          with the highest counts. Visit all nodes one edge away. If all
          nodes have been visited, stop. Otherwise, repeat with remaining
          nodes until all nodes have been visted. Each step
          defines a read group.

      - directional (default)
          Identify clusters of connected UMIs (based on hamming distance
          threshold) and umi A counts >= (2* umi B counts) - 1. Each
          network is a read group.

"""""""""""""""""""""""""""""
``--edit-distance-threshold``
"""""""""""""""""""""""""""""
       For the adjacency and cluster methods the threshold for the
       edit distance to connect two UMIs in the network can be
       increased. The default value of 1 works best unless the UMI is
       very long (>14bp).

"""""""""""""""""""""""
``--spliced-is-unique``
"""""""""""""""""""""""
       Causes two reads that start in the same position on the same
       strand and having the same UMI to be considered unique if one is spliced
       and the other is not. (Uses the 'N' cigar operation to test for
       splicing).

"""""""""""""""""""""""""
``--soft-clip-threshold``
"""""""""""""""""""""""""
       Mappers that soft clip will sometimes do so rather than mapping a
       spliced read if there is only a small overhang over the exon
       junction. By setting this option, you can treat reads with at least
       this many bases soft-clipped at the 3' end as spliced. Default=4.

""""""""""""""""""""""""""""""""""""""""""""""
``--multimapping-detection-method=[NH/X0/XT]``
""""""""""""""""""""""""""""""""""""""""""""""
      If the sam/bam contains tags to identify multimapping reads, you can
      specify for use when selecting the best read at a given loci.
      Supported tags are "NH", "X0" and "XT". If not specified, the read
      with the highest mapping quality will be selected.

"""""""""""""""""
``--read-length``
"""""""""""""""""
      Use the read length as a criteria when deduping, for e.g sRNA-Seq.


Single-cell RNA-Seq options
---------------------------

""""""""""""""
``--per-gene``
""""""""""""""
      Reads will be grouped together if they have the same gene.  This
      is useful if your library prep generates PCR duplicates with non
      identical alignment positions such as CEL-Seq. Note this option
      is hardcoded to be on with the count command. I.e counting is
      always performed per-gene. Must be combined with either
      ``--gene-tag`` or ``--per-contig`` option.

""""""""""""""
``--gene-tag``
""""""""""""""
      Deduplicate per gene. The gene information is encoded in the bam
      read tag specified

"""""""""""""""""""""""""
``--assigned-status-tag``
"""""""""""""""""""""""""
      BAM tag which describes whether a read is assigned to a
      gene. Defaults to the same value as given for ``--gene-tag``

"""""""""""""""""""""
``--skip-tags-regex``
"""""""""""""""""""""
      Use in conjunction with the ``--assigned-status-tag`` option to
      skip any reads where the tag matches this regex.  Default
      (``"^[__|Unassigned]"``) matches anything which starts with "__"
      or "Unassigned":

""""""""""""""""
``--per-contig``
""""""""""""""""
      Deduplicate per contig (field 3 in BAM; RNAME).
      All reads with the same contig will be considered to have the
      same alignment position. This is useful if you have aligned to a
      reference transcriptome with one transcript per gene. If you
      have aligned to a transcriptome with more than one transcript
      per gene, you can supply a map between transcripts and gene
      using the ``--gene-transcript-map`` option

"""""""""""""""""""""""""
``--gene-transcript-map``
"""""""""""""""""""""""""
      File mapping genes to transcripts (tab separated), e.g::

          gene1   transcript1
          gene1   transcript2
          gene2   transcript3

""""""""""""""
``--per-cell``
""""""""""""""
      Reads will only be grouped together if they have the same cell
      barcode. Can be combined with ``--per-gene``.

SAM/BAM Options
---------------

"""""""""""""""""""""
``--mapping-quality``
"""""""""""""""""""""
      Minimium mapping quality (MAPQ) for a read to be retained. Default is 0.

""""""""""""""""""""
``--unmapped-reads``
""""""""""""""""""""
     How should unmapped reads be handled. Options are:
      - discard (default)
          Discard all unmapped reads
      - use
          If read2 is unmapped, deduplicate using read1 and output read1 only. Note
          that if read1 is unmapped, read2 will always be descarded irrepsective of
          whether it is mapped. WARNING: May lead to unpaired reads in output. Requires
          ``--paired``
      - output
          Output unmapped reads/read pairs without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""""""
``--chimeric-pairs``
""""""""""""""""""""
     How should chimeric read pairs be handled. Options are:
      - discard
          Discard all chimeric read pairs
      - use (default)
          Deduplicate using read1 information only. Both read1 and read2 should 
          still be output, as long as Read2 is actaully found. Can lead to
          unpaired reads in output if read1 is marked as having a mapped mate,
          but read2 is never found.
      - output
          Output chimeric read pairs without UMI
          grouping/deduplication.  Only available in umi_tools group

""""""""""""""""""""
``--unpaired-reads``
""""""""""""""""""""
     How should unpaired reads be handled. Options are:
      - discard
          Discard all unpaired reads. Note: Can still lead to unpaired
          reads in the output if a read1 is marked as having a mapped
          mate, but the mate is never found. 
      - use (default)
          Deduplicate unpaired reads using read1 only. Note, unpaired read2s will still 
          be discarded. 
      - output
          Output unpaired reads without UMI
          grouping/deduplication. Only available in umi_tools group

""""""""""""""""
``--ignore-umi``
""""""""""""""""
      Ignore the UMI and group reads using mapping coordinates only

""""""""""""
``--subset``
""""""""""""
      Only consider a fraction of the reads, chosen at random. This is useful
      for doing saturation analyses.

"""""""""""
``--chrom``
"""""""""""
      Only consider a single chromosome. This is useful for
      debugging/testing purposes

""""""""""""
``--paired``
""""""""""""
       BAM is paired end - output both read pairs. This will also
       force the use of the template length to determine reads with
       the same mapping coordinates.


Input/Output Format Options
---------------------

The following options deal with input and output format, and are useful for
outputting CRAM format. In general UMI-tools will attempt to guess the input
and output formats from the file names, but thing can be over-written using
the ``out-format`` and ``input-format`` options. The location of  CRAM 
reference files will be taken from the either the an input CRAM file 
(if present) or from the ``--reference-filename`` option. Otherwise
the reference will be embedded in the file. 

"""""""""""""""""""""""""    
``--in-format=IN_FORMAT``
"""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam

""""""""""""""""""""""""""""""""" 
``--input-options=INPUT_OPTIONS``
"""""""""""""""""""""""""""""""""

      Format string provided to htslib for reading. Mostly
      useful for CRAM formatted files. See samtools
      documentation

"""""""""""""""""""""""
``--in-sam``
"""""""""""""""""""""""
      [DEPRECATED] USE ``--in-format`` . By default, inputs are assumed to be 
      in BAM format. Use this option to specify the use of SAM format for
      input.

""""""""""""""""""""""""""""""""""""""""""" 
``--reference-filename=REFERENCE_FILENAME``
"""""""""""""""""""""""""""""""""""""""""""
      File path or URL to the genome reference to be used
      when reading or writing CRAM files. Can be a path or
      a URL. By default, when reading a CRAM file, the 
      reference recorded in the input file will be used
      unless this is specified. URL references cannot be read
      from input files, however. When writing, specifying a
      reference location is required unless specified in input.

)UMIDOC";
  if (tool == "prepare_for_em") return R"UMIDOC(
====================================================================
prepare_for_em - make the output from dedup compatible with EM tools
====================================================================

prepare_for_em - make output from dedup or group compatible with RSEM or 
                   Salmon

Usage::

    umi_tools prepare_for_em [OPTIONS] [--stdin=IN_BAM] [--stdout=OUT_BAM]

       note: If ``--stdout`` is ommited, standard out is output. To
             generate a valid BAM/SAM/CRAM file on standard out, please
             redirect log with ``--log=LOGFILE`` or ``--log2stderr``

The SAM format specification states that the mnext and mpos fields should point
to the primary alignment of a read's mate. However, not all aligners adhere to
this standard. 

In general (except in a few edge cases) UMI tools outputs only the read2 to that 
corresponds to the read specified in the mnext and mpos positions of a selected
read1, and only outputs this read once, even if multiple read1s point to it.

This makes UMI-tools outputs incompatible with some downstream tools, noteably 
RSEM and Salmon (although we recommend using Alevin if you want to quantify
single cell RNA-seq from protocols that Alevin supports). This script takes the output
from dedup or group and ensures that each read1 has exactly one read2 (and vice
versa), that read2 always appears directly after read1, and that pairs point to 
each other (note this is technically not valid SAM format). Copy any specified
tags from read1 to read2 if they are present (by default, UG and BX, the unique
group and correct UMI tags added by _group_)

In order for this to work correctly, your input file must be sorted by read name. 
Generally the protocol would be:

1. Align reads to the transcriptome with your favourite aligner.

2. Position sort the resulting BAM file.

3. Run `dedup` on the position sorted name file.

4. Use `samtools sort -n` or `samtools collate` to sort by read name.

5. Use `prepare_for_rsem` to create a file that has exactly one mate
   per read and that pairs are adjecent in the output.

6. Run your downstream tools - RSEM/Salmon/Kalisto on the output. 

prepare_for_em specific options
-------------------------------
"""""""""""""""""""""""""
``--tags =TAG[,TAG....]``
"""""""""""""""""""""""""
List of SAM tags that are transfered from read1 to read2. The default
is UG and BX, which is the numeric UMI group, and the infered true UMI
respectively. 



Input/Output Format Options
---------------------

The following options deal with input and output format, and are useful for
outputting CRAM format. In general UMI-tools will attempt to guess the input
and output formats from the file names, but thing can be over-written using
the ``out-format`` and ``input-format`` options. The location of  CRAM 
reference files will be taken from the either the an input CRAM file 
(if present) or from the ``--reference-filename`` option. Otherwise
the reference will be embedded in the file. 

"""""""""""""""""""""""""    
``--in-format=IN_FORMAT``
"""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam

""""""""""""""""""""""""""""""""" 
``--input-options=INPUT_OPTIONS``
"""""""""""""""""""""""""""""""""

      Format string provided to htslib for reading. Mostly
      useful for CRAM formatted files. See samtools
      documentation

"""""""""""""""""""""""
``--in-sam``
"""""""""""""""""""""""
      [DEPRECATED] USE ``--in-format`` . By default, inputs are assumed to be 
      in BAM format. Use this option to specify the use of SAM format for
      input.

""""""""""""""""""""""""""""""""""""""""""" 
``--reference-filename=REFERENCE_FILENAME``
"""""""""""""""""""""""""""""""""""""""""""
      File path or URL to the genome reference to be used
      when reading or writing CRAM files. Can be a path or
      a URL. By default, when reading a CRAM file, the 
      reference recorded in the input file will be used
      unless this is specified. URL references cannot be read
      from input files, however. When writing, specifying a
      reference location is required unless specified in input.


""""""""""""""""""""""""""" 
``--out-format=OUT_FORMAT``
"""""""""""""""""""""""""""
      File format of the input file. Format is usually
      implied from the extension of the filename, but maybe
      overridden with this option. Default=bam


"""""""""""""""""""""""""""""""""""
``--output-options=OUTPUT_OPTIONS``
"""""""""""""""""""""""""""""""""""
      Format string provided to htslib for writing. Mostly
      useful for CRAM formatted files. See samtools
      documentation

)UMIDOC";
  return {};
}

}  // namespace umi_tools
