## Code exploration policy

Before using grep/find/Read for project-wide exploration, use CodeGraph first.

Preferred order:
1. codegraph_status
2. codegraph_explore for architecture or "how does X work"
3. codegraph_search for locating symbols
4. codegraph_callers / codegraph_callees for call chains
5. codegraph_impact before edits
6. Read only the specific files that must be changed

Do not scan the whole repository unless CodeGraph cannot answer the question.