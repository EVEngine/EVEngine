# Checked Results and Return Values

[中文](../../dev/Result检查与不得丢弃返回值规范.md) · [Bindings](SCRIPT-BINDINGS.md) · [Developer index](README.md)

This contributor guide translates the operational rules of the Chinese specification. Detailed migration inventories and audit history remain in the authoritative source.

## Two protections

For APIs that can fail or whose return affects correctness, `[[nodiscard]]` warns against discarding a return. In Debug, Result tracks whether it was observed before destruction and uses `EV_ASSERT` if it was not. Release removes observation overhead while retaining the attribute.

The public type carries the attribute:

```cpp
template<class T>
class [[nodiscard("Result must be checked or explicitly ignored")]] Result;
```

The specification's Debug-only observed/must-observe flags describe the shared Result implementation; do not introduce a second implementation.

## Observation

Status checks (`ok`, `accepted`, `status`, explicit boolean conversion), value access (`value`, `valueOr`, `takeValue`), diagnostics/error access, composition helpers and explicit ignore/discard operations mark observation. A Result-to-exception bridge, where available, also consumes it. Checking failure counts just as checking a value does.

Construction, moving, assignment, printing an address, unrelated debug labels and storage in a container do not discharge responsibility.

Explicitly state intentional ignoring:

```cpp
operation().ignore();
```

An optional reason records best-effort intent. Do not use `(void)operation()` to bypass the contract: compiler diagnostics differ, but Debug destruction must still detect an unobserved Result. For required success, use the API's explicit `expect(...)` helper where available; ignoring is not a success assertion.

## Move responsibility

Result is preferably move-only. Moving transfers responsibility and disarms the source. Move assignment must first handle an unobserved destination, rather than silently overwriting it.

If copying is needed later, independent observation flags create false positives. Use a shared Debug observation token or clone a plain value without responsibility. Do not enable copying just for convenience.

## Short-circuit trap

Observe every Result before making a combined decision:

```cpp
// Wrong: b and c are never inspected if a fails.
if (!a || !b || !c) return fail<T>(DiagnosticCode::ParseError, "invalid fields", path);

// Correct: observe all arguments without short-circuiting.
if (!eve::everyResultValid(a, b, c))
    return fail<T>(DiagnosticCode::ParseError, "invalid fields", path);
```

To preserve the first failure's status:

```cpp
const bool aOk = a.ok();
const bool bOk = b.ok();
if (!aOk) return Result<T>::failure(a.status());
if (!bOk) return Result<T>::failure(b.status());
```

Multiple unobserved destructions during stack unwinding can terminate before useful diagnostics appear. Plain pointers/optionals/count checks may short-circuit; do not mix them with unobserved Results in a way that skips observation.

## Classify return values by meaning

- Checked Result: fallible create/load/save/import/build operations, transactions, registry mutation, resource transfers, actions/orders/effects, schema validation/restoration, editor commands and hot reload.
- Non-Result nodiscard: identity/ownership handles, persistent IDs, subscriptions, task/transaction tokens, locks/guards and paired scope objects. Ordinary values do not automatically need Result observation fields.
- Risk-dependent annotation: cache hit/miss, statistics, ordinary optional queries or booleans meaning only changed/not-changed. Avoid warning noise for correct ignoring.
- Safely discardable: void setters, explicit fire-and-forget operations and logging/telemetry. Do not hide a fallible Result behind a default silent void wrapper.

Factories, ownership transfer and side-effecting consume/pop/take operations require particular care: discarding may lose a resource or consumed data. Retain subscriptions; dropping an RAII subscription usually unsubscribes immediately. Detached tasks require explicit semantics rather than accidental handle destruction.

Distinguish changed from success. Annotate important non-Result returns consistently on public base/override declarations; function attributes should not be assumed to propagate to every static call type. Ordinary getters are not a bulk-annotation target.

## Asynchronous results

Starting and completing a task are separate responsibilities: the start API may return `Result<TaskHandle>`, and completion `Result<Artifact>`. Handle destruction follows its own/cancel/detach contract; an observed flag does not manage task lifetime. A Future/Promise carrying Result requires observation by the final consumer.

## Script projection

Squirrel GC cannot guarantee prompt destruction. Bindings consume the C++ Result when producing the shared projection in `src/engine/common/SquirrelBinding.h`:

```text
{
  ok, code, hasValue, checked: true, ignored, ignoreReason,
  status: { ok, code, summary, diagnostics, diagnosticCount },
  diagnostics: [{ code, severity, message, path, source, details }],
  value: Value | null
}
```

Callers check `ok` or call `eve.result.ignore(result, reason)` with a nonempty reason. Ignoring does not turn failure into success. Branch on stable codes/paths, not human-readable message text. Source identifies the producer/binding.

The common `eve::Value` adapter accepts Null/Bool/Int64/finite Double/String/Array/Object. It bounds nesting/elements and rejects cycles, non-string table keys, non-finite numbers and unsupported objects, preserving diagnostic path/source. Failed reverse push restores the VM stack.

Canonical checked bindings share this projection. Historical modules may still return integers, booleans or strings; their actual binding contract remains authoritative until explicitly migrated. Do not reinterpret those returns as tables.

## Verify and migrate

Test compiler diagnostics, unobserved Debug destruction, observing accessors, explicit ignoring, failure diagnostics, moves/assignment, forwarding, Release overhead removal and binding consumption. Include the combined-condition trap. `make check/nodiscard` and `make check/adhoc-result-tables` provide source gates.

Migrate shared Result types first, then high-risk Transaction/Schema/Editor writes, procgen/restoration/registry changes and other gameplay mutations. Audit handles/tokens/subscriptions before retiring `bool/nullptr + lastError`. Detailed audited API families and migration status remain in the Chinese source.
