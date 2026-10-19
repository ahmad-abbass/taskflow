# TaskFlow

A dependency-free C++17 task scheduler built around OOP and hand-written data structures. The current milestone runs typed tasks through a custom linked FIFO queue. It is an in-process learning project: tasks execute locally, and there are no cloud services, frameworks, network workers, or third-party C++ libraries.

## Run

Requires a C++17 compiler and GNU Make. Python 3 is used only by the CLI integration check.

```sh
make
make demo
make test
make sanitize
./build/taskflow
```

Example commands:

```text
add totals 3 sum "12 8 -2" "Add a small batch"
add notes 9 count "six words make one small example"
next
run
list
show totals
quit
```

`next` executes one pending task. `run` executes up to 100,000 pending tasks. `cancel ID` cancels a task before it runs. Larger priority values are stored but do not affect this milestone's FIFO order. Quoted payloads support spaces; a malformed command reports an error and later commands can continue. The process exits with status 1 if any command failed.

## What this milestone demonstrates

- An abstract `TaskAction` interface with concrete sum and word-count actions.
- A factory that validates inputs and constructs the appropriate action.
- `Task` encapsulation and explicit task states.
- `unique_ptr` ownership and a move-only templated linked queue.
- Constant-time FIFO push/pop; iterative cleanup avoids recursive destruction of a long chain.
- Cancellation, overflow-safe arithmetic, CLI checks, and sanitizer configuration.

Task lookup currently scans a vector in O(n); listing is O(n) in insertion order. Standard strings, vectors, streams, and smart pointers provide basic language-library support. The queue logic is implemented here rather than delegated to `std::queue`.

## Planned increments

1. Task model, action polymorphism, linked FIFO queue, and CLI.
2. Custom hash-table registry with collision and resizing tests.
3. Stable priority scheduling with a custom binary heap and interchangeable policies.
4. Dependency graph with cycle checks and failure propagation.
5. Deterministic worker simulation, bounded retries, and delayed scheduling.
6. Validated snapshots and undo history using a custom stack.
7. AVL range/rank queries, reference-model tests, and measured examples.

Each published increment will be buildable and will describe only the functionality it contains. This is a staged learning release; a publication date is not a claim that all its code was written that day.

## Authorship

Created for Ahmad Abbas as an independent learning project. No private coursework source or course-provided scaffolding is copied into this project.
