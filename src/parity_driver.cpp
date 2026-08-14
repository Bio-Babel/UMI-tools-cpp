// parity_driver — exposes ported library units to the parity harness.
//
// 10_parity says: for LIBRARY units, drive the built artifacts through the
// pybind11 module if bindings are kept, else through a small purpose-built driver
// executable. Bindings are OFF for this port (migration stage A, 06_design.md),
// so this is that driver.
//
// It reads a JSON-ish line protocol on stdin and writes results to stdout, so
// the Python side can feed identical inputs to both implementations without
// linking anything. Deliberately dumb: no parsing cleverness, because a bug in
// the driver would look like a parity failure.
//
// Protocol (one request per line):
//   cluster <method> <threshold> <umi>:<count> [<umi>:<count> ...]
//     -> one line per group: comma-separated UMIs, groups separated by ';'
//   edit_distance <a> <b>
//     -> the integer, or "ERROR" when the oracle would raise
//   substr_slices <umi_length> <idx_size>
//     -> "start-stop,start-stop,..."
//   median <int> [<int> ...]
//     -> the median, %.17g
#include "umi_tools/fastq.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/bytes.hpp"
#include "umi_tools/edit_distance.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/options.hpp"
#include "umi_tools/pattern.hpp"
#include "umi_tools/knee.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/dedup_stats.hpp"
#include "umi_tools/sam_methods.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace umi_tools;

namespace {

void handle_cluster(std::istringstream& is) {
  std::string method_name;
  std::int64_t threshold = 0;
  is >> method_name >> threshold;

  const auto method = parse_cluster_method(method_name);
  if (!method) {
    std::cout << "ERROR unknown method\n";
    return;
  }

  UmiCounts counts;
  std::string token;
  while (is >> token) {
    const auto pos = token.rfind(':');
    if (pos == std::string::npos) continue;
    // operator[] here mirrors Python's `counts[umi] = n` assignment.
    counts[Bytes(token.substr(0, pos))] = std::stoll(token.substr(pos + 1));
  }

  try {
    UMIClusterer clusterer(*method);
    const auto groups = clusterer(counts, threshold);
    std::string out;
    for (std::size_t g = 0; g < groups.size(); ++g) {
      if (g) out += ';';
      for (std::size_t i = 0; i < groups[g].size(); ++i) {
        if (i) out += ',';
        out += groups[g][i];
      }
    }
    std::cout << out << "\n";
  } catch (const std::exception& e) {
    std::cout << "ERROR " << e.what() << "\n";
  }
}

}  // namespace

int main() {
  // ExitRequest is deliberately NOT a std::exception
  // (logging.hpp:160) so that only a deliberate handler exits on it, and it is
  // thrown rather than calling std::exit so unwinding closes the gzip/BGZF
  // writers first. Before this catch, main.cpp was the ONLY handler: an
  // error_exit reached from library code here escaped main uncaught ->
  // std::terminate -> SIGABRT (rc 134), and gcc does not unwind on the way
  // there, so the BGZF EOF block ExitRequest exists to guarantee was exactly
  // what got lost. MEASURED: `rand_reads <unindexed.bam> 1 5` through the
  // driver gave rc=134.
  try {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    std::istringstream is(line);
    std::string cmd;
    is >> cmd;

    if (cmd == "metacontig_map") {
      // "metacontig_map <bam> <mapfile>" -> the map as sorted JSON, so it can be
      // compared to json.dumps(..., sort_keys=True) on the oracle side.
      std::string bam, mapfile;
      is >> bam >> mapfile;
      AlignmentReader rd(bam);
      const auto m = get_meta_contig_to_contig(rd, mapfile);
      std::vector<std::string> genes;
      for (const auto& [g, _] : m) { (void)_; genes.push_back(g); }
      std::sort(genes.begin(), genes.end());
      std::string out = "{";
      for (std::size_t i = 0; i < genes.size(); ++i) {
        if (i) out += ", ";
        std::vector<std::string> cs(m.at(genes[i]).begin(), m.at(genes[i]).end());
        std::sort(cs.begin(), cs.end());
        out += "\"" + genes[i] + "\": [";
        for (std::size_t j = 0; j < cs.size(); ++j) {
          if (j) out += ", ";
          out += "\"" + cs[j] + "\"";
        }
        out += "]";
      }
      out += "}";
      std::cout << out << "\n";
    } else if (cmd == "avg_umi_dist") {
      // "avg_umi_dist <umi> [<umi> ...]" -> the mean pairwise edit distance.
      // umi_methods.get_average_umi_distance. Returns the INT -1 for a single
      // UMI, which upstream then feeds into a float list; printed at 17
      // significant digits so the comparison is on the value, not on formatting.
      std::vector<Bytes> umis;
      std::string u;
      while (is >> u) umis.push_back(Bytes(u));
      std::printf("%.17g\n", get_average_umi_distance(umis));
    } else if (cmd == "rand_reads") {
      // "rand_reads <bam> <seed> <n1> <n2> ..." -> one line per draw batch,
      // comma-joined. random_read_generator draws from NUMPY's stream (not the
      // stdlib one dedup's tie-break uses), so the seeding path is
      // seed_numpy(); getting that wrong yields a different stream from the
      // same seed, which is the trap D6 pre-specified.
      std::string bam;
      std::int64_t seed = 0;
      is >> bam >> seed;
      BundleOptions bo;
      bo.get_umi_method = UmiMethod::ReadId;
      bo.umi_sep = "_";
      PyRandom nrng;
      nrng.seed_numpy(seed);
      RandomReadGenerator gen(bam, /*reference=*/"", /*chrom=*/"", bo, nrng);
      std::int64_t n = 0;
      while (is >> n) {
        const auto got = gen.get_umis(n);
        std::string line;
        for (std::size_t i = 0; i < got.size(); ++i) {
          if (i) line += ",";
          line += std::string(got[i]);
        }
        std::cout << line << "\n";
      }
    } else if (cmd == "metafetch") {
      // "metafetch <bam> <mapfile>" -> "qname\tcontig\tMC" per yielded read,
      // in the PORT's (deterministic) order.
      std::string bam, mapfile;
      is >> bam >> mapfile;
      AlignmentReader rd(bam);
      const auto m = get_meta_contig_to_contig(rd, mapfile);
      for (const auto& [gene, contigs] : m)
        for (const std::string& c : contigs) {
          rd.set_region(c);
          BamRecord rec;
          while (rd.next(rec))
            std::cout << rec.query_name() << "\t" << rd.target_name(rec.tid())
                      << "\t" << gene << "\n";
        }
    } else if (cmd == "logwrappers") {
      // "logwrappers <v> <logfile>" -> call every wrapper at umi_tools -v level
      // <v>. Utilities.error() exits, so it is covered by parity_logging.py
      // instead; the rest are pure threshold wrappers.
      std::int64_t v = 1; std::string path;
      is >> v >> path;
      Log& lg = Log::instance();
      lg.open(path, /*log2stderr=*/false, v, /*stdout_is_same_stream=*/false);
      lg.debug("D-message");
      lg.info("I-message");
      lg.warning("W-message");
      lg.warn("w-message");
      lg.critical("C-message");
      lg.log(10, "L10"); lg.log(20, "L20"); lg.log(30, "L30");
      lg.log(40, "L40"); lg.log(50, "L50"); lg.log(45, "L45"); lg.log(25, "L25");
      lg.close();
      std::cout << "ok\n";
    } else if (cmd == "guess_format") {
      // "guess_format <quals>" -> comma-joined formats, in RANGES order.
      std::string q;
      is >> q;
      if (q == "@") q.clear();
      const auto r = guess_format(q);
      std::string out;
      for (const auto& x : r) { if (!out.empty()) out += ","; out += x; }
      std::cout << (out.empty() ? "-" : out) << "\n";
    } else if (cmd == "barcode_read_id") {
      // "barcode_read_id <bam> <cell:0|1> <sep> <n>" -> "umi|cell" per read.
      std::string bam, sep; int cell = 0; std::int64_t n = 0;
      is >> bam >> cell >> sep >> n;
      if (sep == "@") sep = "_";
      AlignmentReader rd(bam);
      BamRecord rec;
      std::vector<std::string> lines;
      while (rd.next(rec)) {
        // Upstream's bare `except:` turns any failure into a ValueError the
        // callers treat as "skip this read"; the driver reports it per read
        // rather than dying, so one bad read cannot mask the other 199.
        try {
          const auto cu = get_barcode_read_id(rec, cell != 0, sep);
          lines.push_back(cu.umi + "|" + (cu.cell ? *cu.cell : std::string("@")));
        } catch (const std::exception&) {
          lines.push_back("KEYERROR");
        }
        if (n > 0 && static_cast<std::int64_t>(lines.size()) >= n) break;
      }
      std::cout << lines.size() << "\n";
      for (const auto& l : lines) std::cout << l << "\n";
    } else if (cmd == "barcode_tag") {
      // "barcode_tag <bam> <cell> <umi_tag> <cell_tag> <us> <ud> <cs> <cd> <n>"
      // with '@' meaning None on every optional field.
      std::string bam, umi_tag, cell_tag, us, ud, cs, cd;
      int cell = 0; std::int64_t n = 0;
      is >> bam >> cell >> umi_tag >> cell_tag >> us >> ud >> cs >> cd >> n;
      TagBarcodeOptions o;
      o.umi_tag = umi_tag;
      if (cell_tag != "@") o.cell_tag = cell_tag;
      if (us != "@") o.umi_tag_split = us;
      if (ud != "@") o.umi_tag_delim = ud;
      o.cell_tag_split = (cs == "@") ? std::string() : cs;
      if (cd != "@") o.cell_tag_delim = cd;
      AlignmentReader rd(bam);
      BamRecord rec;
      std::vector<std::string> lines;
      while (rd.next(rec)) {
        const auto cu = get_barcode_tag(rec, cell != 0, o);
        lines.push_back(cu ? (cu->umi + "|" + (cu->cell ? *cu->cell : std::string("@")))
                           : std::string("KEYERROR"));
        if (n > 0 && static_cast<std::int64_t>(lines.size()) >= n) break;
      }
      std::cout << lines.size() << "\n";
      for (const auto& l : lines) std::cout << l << "\n";
    } else if (cmd == "bam_features") {
      // "bam_features <bam> <n_entries>" -> "NH=<0|1>,X0=<0|1>,XT=<0|1>"
      std::string bam; std::int64_t n = 1000;
      is >> bam >> n;
      const auto f = detect_bam_features(bam, n);
      std::string out;
      for (const auto& [k, v] : f) {
        if (!out.empty()) out += ",";
        out += k + "=" + std::to_string(v);
      }
      std::cout << out << "\n";
    } else if (cmd == "determine_format") {
      // "determine_format <filename> <sam:0|1> <out_format|@>" -> the format.
      // '@' is the protocol's None marker (out_format is None by default).
      std::string fn, of; int sam = 0;
      is >> fn >> sam >> of;
      if (of == "@") of.clear();
      if (fn == "@") fn.clear();
      std::cout << determine_format(fn, sam != 0, of) << "\n";
    } else if (cmd == "output_names") {
      // "output_names <eventual_name> <sam> <outformat|@> <no_sort:0|1>" ->
      //   "<out_name_kind>,<out_format>,<sorted_name>,<sorted_format>"
      // The temp path itself is random on both sides, so it is reported as the
      // KIND ("tmp" vs the literal name) — the branch is what is being compared.
      std::string name, of; int sam = 0, no_sort = 0;
      is >> name >> sam >> of >> no_sort;
      if (of == "@") of.clear();
      const std::string eventual = determine_format(name, sam != 0, of);
      std::string out_name, out_format, sorted_name, sorted_format;
      if (!no_sort) {
        sorted_format = eventual;
        sorted_name = name;
        out_name = "tmp";
        out_format = eventual == "sam" ? "sam" : "bam";
      } else {
        out_format = eventual;
        out_name = name;
        sorted_name = "@";
        sorted_format = "@";
      }
      std::cout << out_name << "," << out_format << "," << sorted_name << ","
                << sorted_format << "\n";
    } else if (cmd == "twchunks" || cmd == "twwrap") {
      // Text is HEX-framed: leading and trailing spaces are exactly what the
      // drop_whitespace rules turn on, and a whitespace-delimited protocol
      // would silently eat them.
      auto unhex = [](const std::string& h) {
        std::string o;
        for (std::size_t i = 0; i + 1 < h.size(); i += 2)
          o.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
        return o;
      };
      auto tohex = [](const std::string& b) {
        static const char* d = "0123456789abcdef";
        std::string o;
        for (unsigned char c : b) { o.push_back(d[c >> 4]); o.push_back(d[c & 15]); }
        return o;
      };
      int width = 0;
      if (cmd == "twwrap") is >> width;
      std::string hex;
      is >> hex;
      const std::string text = unhex(hex);
      const auto out = cmd == "twwrap" ? textwrap_wrap(text, width)
                                       : textwrap_chunks(text);
      std::cout << out.size() << "\n";
      for (const auto& l : out) std::cout << tohex(l) << "\n";
    } else if (cmd == "readpos") {
      // "readpos <bam> <soft_clip_threshold> <n>" -> one line per read:
      //   qname,start,pos,is_spliced   (n = 0 means every read)
      std::string bam; double sct = 4; std::int64_t n = 0;
      is >> bam >> sct >> n;
      try {
        AlignmentReader rd(bam);
        BamRecord rec;
        std::vector<std::string> lines;
        while (rd.next(rec)) {
          if (rec.cigar().empty()) continue;   // Python IndexErrors; skipped both sides
          const ReadPosition rp = get_read_position(rec, sct);
          lines.push_back(std::string(rec.query_name()) + "," +
                          std::to_string(rp.start) + "," + std::to_string(rp.pos) + "," +
                          std::to_string(rp.is_spliced));
          if (n > 0 && static_cast<std::int64_t>(lines.size()) >= n) break;
        }
        std::cout << lines.size() << "\n";
        for (const auto& l : lines) std::cout << l << "\n";
      } catch (const std::exception& e) { std::cout << "0\nERROR " << e.what() << "\n"; }
    } else if (cmd == "pyrandom") {
      // "pyrandom <seed> <count>" -> "%.17g,%.17g,..." from random.random()
      std::int64_t s = 0, cnt = 0;
      is >> s >> cnt;
      PyRandom r(s);
      std::string out; char buf[64];
      for (std::int64_t i = 0; i < cnt; ++i) {
        if (i) out += ',';
        std::snprintf(buf, sizeof(buf), "%.17g", r.random()); out += buf;
      }
      std::cout << out << "\n";
    } else if (cmd == "pyrandbits") {
      // "pyrandbits <seed> <count>" -> raw generator words
      std::int64_t s = 0, cnt = 0;
      is >> s >> cnt;
      PyRandom r(s);
      std::string out;
      for (std::int64_t i = 0; i < cnt; ++i) {
        if (i) out += ',';
        out += std::to_string(r.getrandbits32());
      }
      std::cout << out << "\n";
    } else if (cmd == "kde") {
      // "kde <bw> <x1,x2,...> <p1,p2,...>" -> "%.17g,%.17g,..."
      double bw = 0.0;
      std::string data_s, pts_s;
      is >> bw >> data_s >> pts_s;
      auto nums = [](const std::string& s) {
        std::vector<double> v; std::istringstream ss(s); std::string t2;
        while (std::getline(ss, t2, ',')) if (!t2.empty()) v.push_back(std::stod(t2));
        return v;
      };
      try {
        const auto d = gaussian_kde(nums(data_s), nums(pts_s), bw);
        std::string out; char buf[64];
        for (std::size_t i = 0; i < d.size(); ++i) {
          if (i) out += ',';
          std::snprintf(buf, sizeof(buf), "%.17g", d[i]); out += buf;
        }
        std::cout << out << "\n";
      } catch (const std::exception& e) { std::cout << "ERROR " << e.what() << "\n"; }
    } else if (cmd == "argrelmin") {
      std::string ys;
      is >> ys;
      std::vector<double> y; { std::istringstream ss(ys); std::string t2;
        while (std::getline(ss, t2, ',')) if (!t2.empty()) y.push_back(std::stod(t2)); }
      std::string out;
      for (std::int64_t i : argrelextrema_less(y)) { if (!out.empty()) out += ','; out += std::to_string(i); }
      std::cout << out << "\n";
    } else if (cmd == "regex") {
      // "regex <pattern> <text>" -> "NOMATCH" | "end=<n>;name=start,stop,text;..."
      // Both fields are whitespace-free in every pattern and read sequence used.
      std::string pat, text;
      is >> pat >> text;
      try {
        Pattern p(pat);
        const auto m = p.match(text);
        if (!m) {
          std::cout << "NOMATCH\n";
        } else {
          std::string out = "end=" + std::to_string(m->end);
          for (const auto& [name, sp] : m->spans)
            out += ";" + name + "=" + std::to_string(sp.start) + "," +
                   std::to_string(sp.stop) + "," + m->groups.at(name);
          std::cout << out << "\n";
        }
      } catch (const std::exception& e) {
        std::cout << "ERROR " << e.what() << "\n";
      }
    } else if (cmd == "help") {
      // "help <tool>" -> line count, then that many lines of help output. A
      // length-prefixed reply keeps a multi-line payload unambiguous on the
      // same line-oriented protocol the other commands use.
      std::string tool;
      is >> tool;
      const ToolSpec* spec = find_tool_spec(tool);
      if (spec == nullptr) {
        std::cout << "0\n";
      } else {
        const std::string h = format_help(*spec);
        std::int64_t lines = 0;
        for (char c : h) if (c == '\n') ++lines;
        std::cout << lines << "\n" << h;
      }
    } else if (cmd == "parse") {
      // "parse <tool> [argv...]" -> "dest=value;dest=value;..." with <none> for
      // Python's None, so the harness can diff dest-by-dest against optparse.
      std::string tool;
      is >> tool;
      const ToolSpec* spec = find_tool_spec(tool);
      if (spec == nullptr) {
        std::cout << "ERROR unknown tool\n";
      } else {
        std::vector<std::string> argv;
        std::string tok;
        while (is >> tok) argv.push_back(tok);
        try {
          const ParseResult r = parse_args(*spec, argv);
          std::string out;
          for (const auto& [dest, val] : r.values.raw()) {
            if (!out.empty()) out += ';';
            out += dest + "=" + (val.second ? "<none>" : val.first);
          }
          std::cout << out << "\n";
        } catch (const std::exception& e) {
          std::cout << "ERROR " << e.what() << "\n";
        }
      }
    } else if (cmd == "cluster") {
      handle_cluster(is);
    } else if (cmd == "edit_distance") {
      std::string a, b;
      is >> a >> b;
      if (a == "@") a.clear();  // the protocol's empty-string marker
      if (b == "@") b.clear();
      try {
        std::cout << edit_distance(a, b) << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "substr_slices") {
      std::int64_t umi_length = 0, idx_size = 0;
      is >> umi_length >> idx_size;
      try {
        std::string out;
        for (const auto& [a, b] : get_substr_slices(umi_length, idx_size)) {
          if (!out.empty()) out += ',';
          out += std::to_string(a) + "-" + std::to_string(b);
        }
        std::cout << out << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "recsearch") {
      // recsearch <start> <node>=<nbr>,...|... -> sorted component members.
      // recursive_search has NO in-port caller (breadth_first_search_recursive
      // uses the iterative BFS), so without this command the exported unit had
      // no differential of its own — which is how the unguarded recursion
      // survived. Same graph encoding as `bfs`.
      std::string start, graph_spec;
      is >> start >> graph_spec;
      AdjList adj;
      std::istringstream gs(graph_spec);
      std::string entry;
      while (std::getline(gs, entry, '|')) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        const Bytes node = entry.substr(0, eq);
        std::vector<Bytes> nbrs;
        std::istringstream ns(entry.substr(eq + 1));
        std::string n;
        while (std::getline(ns, n, ',')) if (!n.empty()) nbrs.push_back(n);
        adj[node] = nbrs;
      }
      try {
        OrderedSet<Bytes> component;
        component.insert(start);              // Python: component = set((node,))
        auto comp = recursive_search(start, adj, component).as_vector();
        std::sort(comp.begin(), comp.end());
        std::string out;
        for (const auto& c : comp) { if (!out.empty()) out += ','; out += c; }
        std::cout << (out.empty() ? "-" : out) << "\n";
      } catch (const std::exception& e) {
        std::cout << "ERROR " << e.what() << "\n";
      }
    } else if (cmd == "bfs") {
      // bfs <start> <node>=<nbr>,<nbr>|<node>=... -> sorted component members
      std::string start, graph_spec;
      is >> start >> graph_spec;
      AdjList adj;
      std::istringstream gs(graph_spec);
      std::string entry;
      while (std::getline(gs, entry, '|')) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        const Bytes node = entry.substr(0, eq);
        std::vector<Bytes> nbrs;
        std::istringstream ns(entry.substr(eq + 1));
        std::string n;
        while (std::getline(ns, n, ',')) if (!n.empty()) nbrs.push_back(n);
        adj[node] = nbrs;
      }
      try {
        auto comp = breadth_first_search(start, adj).as_vector();
        std::sort(comp.begin(), comp.end());
        std::string out;
        for (const auto& c : comp) { if (!out.empty()) out += ','; out += c; }
        std::cout << out << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "remove_umis") {
      // remove_umis <cluster,csv> <nodes,csv> <graph> -> sorted remaining
      std::string cluster_spec, nodes_spec, graph_spec;
      is >> cluster_spec >> nodes_spec >> graph_spec;
      auto csv = [](const std::string& s) {
        std::vector<Bytes> v;
        std::istringstream ss(s);
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(t);
        return v;
      };
      AdjList adj;
      std::istringstream gs(graph_spec);
      std::string entry;
      while (std::getline(gs, entry, '|')) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        adj[entry.substr(0, eq)] = csv(entry.substr(eq + 1));
      }
      try {
        auto rem = remove_umis(adj, csv(cluster_spec), csv(nodes_spec)).as_vector();
        std::sort(rem.begin(), rem.end());
        std::string out;
        for (const auto& c : rem) { if (!out.empty()) out += ','; out += c; }
        std::cout << out << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "substr_idx") {
      // substr_idx <umi_length> <min_edit> <umi,umi,...>
      //   -> "start-stop:sub=umi,umi;sub=umi|start-stop:..."  (sorted for compare)
      std::int64_t umi_length = 0, min_edit = 0;
      std::string umis_spec;
      is >> umi_length >> min_edit >> umis_spec;
      std::vector<Bytes> umis;
      {
        std::istringstream ss(umis_spec);
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) umis.push_back(t);
      }
      try {
        std::string out;
        for (const auto& [idx, by_sub] : build_substr_idx(umis, umi_length, min_edit)) {
          if (!out.empty()) out += '|';
          out += std::to_string(idx.first) + "-" + std::to_string(idx.second) + ":";
          // Sort the substring keys so the comparison does not depend on either
          // side's map ordering: Python's inner dict is a defaultdict whose key
          // order is insertion order, but the SET of umis per substring is what
          // the algorithm consumes.
          std::vector<std::pair<Bytes, std::vector<Bytes>>> flat;
          for (const auto& [sub, set_of] : by_sub) {
            auto v = set_of.as_vector();
            std::sort(v.begin(), v.end());
            flat.emplace_back(sub, v);
          }
          std::sort(flat.begin(), flat.end());
          bool first = true;
          for (const auto& [sub, v] : flat) {
            if (!first) out += ';';
            first = false;
            out += sub + "=";
            for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ','; out += v[i]; }
          }
        }
        std::cout << out << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "nearest_neighbours") {
      // nearest_neighbours <umi_length> <min_edit> <umi,umi,...>
      //   -> sorted "a>b" pairs. The ORACLE's yield order comes from set
      //   iteration and is hash-randomised, so the PAIR SET is compared.
      std::int64_t umi_length = 0, min_edit = 0;
      std::string umis_spec;
      is >> umi_length >> min_edit >> umis_spec;
      std::vector<Bytes> umis;
      {
        std::istringstream ss(umis_spec);
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) umis.push_back(t);
      }
      try {
        const auto idx = build_substr_idx(umis, umi_length, min_edit);
        std::vector<std::string> pairs;
        for (const auto& [a, b] : iter_nearest_neighbours(umis, idx))
          pairs.push_back(a + ">" + b);
        std::sort(pairs.begin(), pairs.end());
        std::string out;
        for (std::size_t i = 0; i < pairs.size(); ++i) { if (i) out += ' '; out += pairs[i]; }
        std::cout << out << "\n";
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else if (cmd == "median") {
      std::vector<std::int64_t> v;
      std::int64_t x = 0;
      while (is >> x) v.push_back(x);
      try {
        std::printf("%.17g\n", np_median(v));
      } catch (const std::exception&) {
        std::cout << "ERROR\n";
      }
    } else {
      std::cout << "ERROR unknown command\n";
    }
    std::cout.flush();
  }
  return 0;
  } catch (const umi_tools::ExitRequest& e) {
    std::cout.flush();
    std::cerr << e.message;
    return e.code;
  } catch (const std::exception& e) {
    // The other half of the same defect. The per-command handlers each catch
    // std::exception around ONE call, so anything thrown outside them — e.g.
    // AlignmentReader::require_index on an unindexed BAM, reached by
    // `rand_reads` — escaped main too. MEASURED rc=134 with
    // `terminate called after throwing ... what(): fetch called on bamfile
    // without index`; gcc does not unwind to std::terminate, so a driver that
    // had written output would have lost its trailer.
    std::cout.flush();
    std::cerr << e.what() << "\n";
    return 1;
  }
}
