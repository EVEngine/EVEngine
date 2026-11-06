# C++ call-site optimization audit

Run from the repository root:

```powershell
python scripts/audit_cpp_calls.py
python scripts/audit_cpp_calls.py src/modules/card --output build/card-audit.json
python scripts/audit_cpp_calls.py src/modules/card --tidy --build-dir build/win32-debug
cmake\with-msvc.cmd python scripts/audit_cpp_calls.py --tidy --build-dir build/win32-debug --compiled-only --jobs 8 --log-dir build/tidy-units --output build/cpp-call-audit-semantic.json
```

The first pass scans tracked engine/module headers and sources without building.
It reports file, line, column, source text, rule and review advice in JSON, plus
clickable compiler-style console locations. Candidate rules cover direct
`return std::move(name)`, value-based `auto` range loops, named type temporaries
in `push_back`, and potentially allocating `substr` calls. Comments and string
literals are masked. These are review candidates, not confirmed defects; scalar
copies and `string_view::substr` are common legitimate matches. Macros, inactive
branches, type aliases and complex expressions limit lexical coverage.

Use `--tidy` with a compilation database produced by the normal Ninja debug
configure (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) for type-aware performance,
emplacement, pass-by-value and move checks. The database must belong to this
checkout, with dependencies and platform includes accessible to clang-tidy.
On Windows MSVC builds, run in the configured compiler environment; a database
alone does not supply the MSVC SDK environment. Header diagnostics are obtained
through the selected translation units. Header-only selections do not constitute
semantic coverage. Diagnostics and compiler failures are retained in the report;
missing or failed translation units cause exit code 2. No automatic fixes run.

`--jobs` controls process concurrency (default 4). `--log-dir` saves each completed
translation unit before the overall run finishes. `--compiled-only` explicitly
limits semantic coverage to this build configuration; absent files (other-platform
backends, disabled modules) remain listed in `excluded_units`. Without this flag,
absent files remain coverage failures. The semantic report deduplicates findings
by location, check and message and records the translation units that produced
them. `verified_translation_units` names only units that parsed successfully;
warnings from a failed unit alone are not sufficient evidence for a fix.

The repository's `msvc-cl.cmd` wrapper needs explicit `--driver-mode=cl` for Clang.
The script detects that wrapper and adds `/WX-` only for analysis, retaining
compiler warnings without treating MSVC/Clang warning differences as parse errors.
It also writes a derived `cpp-call-audit-db/compile_commands.json` below the build
directory, normalizing `/external:I` to `-I`. Clang otherwise treats that flag as
`-isystem` and moves the bundled GLM/Vulkan roots behind regular SDK includes;
MSVC preserves their original search order. This normalization uses the same
canonical dependency headers, with no header replacement or shadow copy.
Normal engine build flags and the generated compilation database are unchanged.
The report records the check selection, database hash and these adjustments.

LLVM 23 with MSVC 14.51 and the pinned SDL headers needs explicit standard-header
preincludes for SDL-first translation units. MSVC STL's vectorized `find` path
also fails a Clang-only assertion for the project's `Cell` type. For this toolchain,
the complete analysis can use the following explicit options:

```powershell
.\cmake\with-msvc.cmd python scripts/audit_cpp_calls.py --tidy --build-dir build/win32-debug --compiled-only --jobs 8 --extra-arg=/FIintrin.h --extra-arg=/FIcstring --extra-arg=-D_USE_STD_VECTOR_ALGORITHMS=0 --log-dir build/tidy-final-units --output build/cpp-call-audit-semantic-final.json
```

These options are recorded in `extra_args`; they do not change production builds
or dependency sources. The STL macro selects the documented scalar implementation
for analysis, so this run does not assess the vectorized STL implementation's
performance. Do not silently suppress genuine compiler errors to claim coverage.

Exit code 0 means the requested scan completed, not that all calls are optimized.
Raw diagnostics are sorted by source location, not expected benefit. Build a
prioritized review queue from an existing semantic report without rerunning Clang:

```powershell
python scripts/rank_cpp_call_audit.py build/cpp-call-audit-semantic-round9.json --output build/cpp-call-audit-priority.json --top 30
```

The queue favors potential owning-container copies, callable copies and move
opportunities over enum sizing and style-only suggestions. Shared-pointer copies
are discounted because they do not clone the pointee. A declared `eve-hot-path`
source or `.eve-hot-path` directory increases priority; filenames alone never
establish frequency. Scores express static review potential, not measured speedup,
copy count or safety. Failed-only diagnostics are deferred, not treated as verified.

Optional `--decisions build/cpp-call-audit-decisions.json` reads a JSON list of
objects with `finding_id`, `source_sha256` (both copied from the queue), `status`
(`retained`, `confirmed`, or `needs-measurement`) and a nonempty `reason` describing
caller, ownership and measurement evidence. Valid retained decisions move to the
end, while confirmed candidates lead the verified active queue. Changed source
hashes reopen decisions; changed diagnostic identities are listed as unmatched.
Keep these generated evidence files under `build/`; no diagnostics are deleted.

For prioritization, intersect findings with profiled hot paths. Before replacing
a value with a reference/view, check mutation, lifetime, proxies and aliasing.
Before removing `std::move`, check NRVO eligibility and return-type conversion.
Before emplacement, check overload selection and exception safety. C++17 guaranteed
copy elision already removes many apparent temporaries. Measure allocations and
runtime after a change; static syntax does not establish a performance improvement.
