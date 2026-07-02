# Architecture Decision Records

One numbered Markdown file per decision, in the lightweight
[Michael Nygard](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions)
format. Numbering is sequential and never reused or renumbered. Template:
[`template.md`](template.md).

Open an ADR when a choice affects the public surface or compatibility, when two reasonable
options exist and the rationale is non-obvious, when a **design pattern** is adopted, or
when superseding a prior decision. Do **not** open one for routine implementation details
or trivially reversible choices.

Status transitions: `Proposed` → `Accepted` → (`Superseded by ADR-XXXX` | `Deprecated`).

## Index

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-adopt-cross-language-source-layout.md) | Adopt the cross-language source layout | Accepted |
| [0003](0003-automate-pr-metadata-and-enable-project-board.md) | Automate PR metadata and enable a project board | Accepted |
| [0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) | Adopt a hybrid header-only + optional STATIC build model | Accepted |
| [0005](0005-unique-ref-non-null-ownership-semantics.md) | `UniqueRef<T>` non-null unique-ownership semantics | Accepted |
| [0006](0006-heap-array-fixed-size-over-vector.md) | `HeapArray<T>` as a fixed-size restriction over `std::vector` | Accepted |
| [0007](0007-stack-allocator-bump-arena-semantics.md) | `StackAllocator<Size>` bump-arena semantics | Accepted |
| [0008](0008-adopt-object-pool-pattern.md) | Adopt the Object Pool pattern for `ObjectPool<T>` | Accepted |
| [0009](0009-adopt-flat-sorted-contiguous-containers.md) | Adopt flat (sorted-contiguous) associative containers | Accepted |
| [0010](0010-flatmap-constexpr-construction.md) | `FlatMap<Key, Value>` constexpr support and construction strategy | Accepted |
| [0011](0011-circular-buffer-ring-semantics.md) | `CircularBuffer<T>` ring-buffer semantics | Accepted |
| [0012](0012-string-formatter-compile-time-validation.md) | `StringFormatter` compile-time format-string validation strategy | Accepted |
| [0013](0013-semaphore-monitor-based-implementation.md) | `Semaphore` monitor-based implementation over mutex + condition variable | Accepted |
| [0014](0014-reader-writer-lock-writer-preference-policy.md) | `ReaderWriterLock` writer-preference policy over a monitor | Accepted |
| [0015](0015-task-future-lightweight-promise-future.md) | `TaskFuture<T>` lightweight promise/future design | Accepted |
| [0016](0016-lock-free-queue-bounded-vyukov-mpmc.md) | `LockFreeQueue<T>` bounded Vyukov MPMC design | Accepted |
| [0017](0017-thread-pool-priority-work-pool.md) | `ThreadPool` priority work pool design | Accepted |
| [0018](0018-stopwatch-steady-clock-accumulation.md) | `Stopwatch` clock choice and accumulation semantics | Accepted |
| [0019](0019-stack-trace-compiled-tier-capture.md) | `StackTrace` compiled-tier capture with on-demand symbolization | Accepted |
