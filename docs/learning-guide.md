# Reading TaskFlow

Start with the FIFO milestone if time is limited. Learn one structure and one scheduler behavior together rather than memorizing the full codebase.

1. Read `actions.hpp` and `actions.cpp`. Explain why the base destructor is virtual, why the factory returns `unique_ptr<TaskAction>`, and how `Scheduler` executes different actions without a type switch.
2. Read `linked_queue.hpp`. Draw ownership of three nodes, then walk through pop, move construction, and iterative destruction. Explain why the tail is an observer and why a moved-from queue must reset it.
3. Read `hash_table.hpp` with `registry.hpp`. Follow a collision, then a resize. Explain why moving node links preserves the addresses of the task objects. Average O(1) lookup depends on distribution; a chain can still grow to O(n).
4. Read `binary_heap.hpp` and `policy.hpp`. Trace bubble-up and sift-down. The priority comparator uses the readiness sequence to make ties stable. A different comparator turns the same heap into a retry deadline queue.
5. Trace `addDependency`, `refresh`, and `blockDependents`. An edge goes from a prerequisite to its dependent; adding it would create a cycle if the dependent can already reach the prerequisite. BFS avoids recursion along a deep graph.
6. Trace `advance` and `finish` using `examples/exam-pipeline.txt`. A duration is virtual time, not sleeping or background execution. Explain how an attempt can fail while its task still has retries remaining.
7. Read `snapshot.cpp`. Parse into a temporary scheduler, connect edges, validate with Kahn's algorithm, validate states, then move the candidate into place. Discuss why saving completion-report messages is a separate concern from saving task state.
8. Read `avl_tree.hpp`. Work through LL, RR, LR, and RL insertions. Each rotation must refresh both height and subtree count. Rank adds the left subtree size when descending right; select uses the same count in reverse.

## Questions worth practicing

- Why are both a hash table and an AVL index useful? What would become slower if only one remained?
- Why can't three independent counters validate nested parentheses? How does the undo stack use ordering rather than just a count?
- What happens if a task was ready, gains a prerequisite, and an old queue entry remains? Explain the version check.
- Why does a failed attempt not always block descendants? At which point is failure terminal?
- What does a `unique_ptr` move transfer? Which pointers in this program own objects and which only observe them?
- Why does `rank("task-10")` use lexicographic rather than numeric order? How would changing the key type affect this?
- Why do the hash and heap tests use standard containers while the implementation does not?
- Which guarantees would need new work before using real worker threads or processes?

## A small change to make yourself

Add an action that returns the largest integer in a validated payload. Reuse the parsing rules, implement the action interface, register it in the factory, and add a negative-number test plus a snapshot round trip. This is a manageable way to demonstrate that you understand the existing design.

Before putting this project on a CV, be comfortable building it, running the example, explaining ownership and one data structure.
