// Optional Python bindings — a SEPARATE target, built only with
// -DUMI_TOOLS_BUILD_BINDINGS=ON. Keep this file thin: it exposes the core
// library, it never contains ported logic. That is what makes it droppable.
//
// This used to bind ONE zero-argument placeholder, so no Python
// signature was exposed and — the load-bearing part — no
// py::register_exception_translator existed. Two wrong mappings were already
// baked into the core headers and would have fired the moment anything real was
// bound:
//
//   * OrderedMap::at throws std::out_of_range where Python raises KeyError, and
//     pybind11 maps std::out_of_range to **IndexError**, not KeyError.
//   * error_exit / raise_value_error throw umi_tools::ExitRequest, which
//     deliberately does NOT derive from std::exception (logging.hpp) so that the
//     per-tool `catch (const std::exception&)` cannot swallow it. pybind11 would
//     not translate it at all: it would escape the interpreter as an unhandled
//     foreign exception and call std::terminate.
//
// Both are handled by the translators below, and both are tested from Python in
// validation/parity_bindings.py rather than asserted here.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "umi_tools/edit_distance.hpp"
#include "umi_tools/logging.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/umi_tools.hpp"

namespace py = pybind11;

PYBIND11_MODULE(umi_tools_native, m) {
  m.doc() = "umi_tools — C++ port exposed to Python";

  // --- exception translation, registered BEFORE anything that can throw ----
  //
  // Registration order is reverse-priority in pybind11: the LAST registered
  // translator is tried FIRST. ExitRequest is registered last so it is checked
  // before the std::exception fallback, though in fact it could not reach that
  // fallback at all — it is not a std::exception, which is the whole point.
  py::register_exception_translator([](std::exception_ptr p) {
    try {
      if (p) std::rethrow_exception(p);
    } catch (const std::out_of_range& e) {
      // Python's `d[k]` raises KeyError. pybind11's DEFAULT for
      // std::out_of_range is IndexError, which is wrong for every OrderedMap
      // lookup, and OrderedMap models a Python dict.
      PyErr_SetString(PyExc_KeyError, e.what());
    }
  });
  py::register_exception_translator([](std::exception_ptr p) {
    try {
      if (p) std::rethrow_exception(p);
    } catch (const umi_tools::ExitRequest& e) {
      // A deliberate exit, not a bug. SystemExit is Python's own spelling of
      // it, and it carries the code the CLI would have returned.
      PyErr_SetObject(PyExc_SystemExit, py::int_(e.code).ptr());
    }
  });

  m.def("version_major", &umi_tools::version_major);

  // --- the public surface 01_audit.md lists ---------------------------------
  // py::arg names are given so the Python signatures match upstream's, which is
  // the other half of what the review says was missing.
  m.def("edit_distance", &umi_tools::edit_distance, py::arg("first"), py::arg("second"),
        "umi_methods.edit_distance(first, second) -> int");

  m.def("get_substr_slices", &umi_tools::get_substr_slices, py::arg("umi_length"),
        py::arg("idx_size"),
        "network.get_substr_slices(umi_length, idx_size) -> list[(start, stop)]");

  py::class_<umi_tools::UMIClusterer>(m, "UMIClusterer")
      .def(py::init([](const std::string& cluster_method) {
             const auto meth = umi_tools::parse_cluster_method(cluster_method);
             if (!meth)
               throw std::invalid_argument("UMIClusterer: unknown method '" +
                                           cluster_method + "'");
             return umi_tools::UMIClusterer(*meth);
           }),
           py::arg("cluster_method") = "directional")
      .def(
          "__call__",
          [](umi_tools::UMIClusterer& self, const py::dict& umis,
             std::int64_t threshold) {
            // Takes a py::dict and walks it in INSERTION ORDER rather than
            // letting pybind11 convert to a std::map. UmiCounts is an
            // OrderedMap precisely because Python's dict order is a language
            // guarantee and the clustering is tie-broken by first-seen; a
            // std::map would silently SORT the keys and change the answer on
            // every input with a tie. That is the single most load-bearing
            // detail in this file.
            umi_tools::UmiCounts counts;
            for (auto item : umis) {
              counts[umi_tools::Bytes(py::cast<std::string>(item.first))] =
                  py::cast<std::int64_t>(item.second);
            }
            const auto groups = self(counts, threshold);
            py::list out;
            for (const auto& g : groups) {
              py::list inner;
              for (const auto& u : g) inner.append(py::bytes(std::string(u)));
              out.append(inner);
            }
            return out;
          },
          py::arg("umis"), py::arg("threshold"),
          "UMIClusterer.__call__(umis, threshold) -> list[list[bytes]]");
}
