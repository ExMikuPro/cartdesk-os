## Git commit message style

When generating git commit messages, follow the existing repository style.

### Style rules

- Use Conventional Commit prefixes when writing the commit subject.
- Preferred types include:
    - `feat:`
    - `fix:`
    - `refactor:`
    - `docs:`
    - `chore:`
    - `build:`
    - `perf:`
    - `style:`
    - `test:`
- After the prefix, write a concise Chinese description.
- Keep the message short, direct, and engineering-oriented.
- Prefer describing the actual change, not abstract goals.
- Mention the affected module, subsystem, or feature when helpful.
- Do not write overly long commit subjects.
- Do not add body text unless explicitly requested.
- Do not mention AI, ChatGPT, Codex, or the assistant.

### Preferred wording patterns

- `feat: 添加 xxx`
- `feat: 接入 xxx`
- `feat: 升级 xxx 到 x.x`
- `fix: 修正 xxx`
- `fix: 修复 xxx`
- `refactor: 调整 xxx`
- `refactor: 重构 xxx`
- `docs: 更新 xxx`
- `chore: 整理 xxx`
- `build: 添加 xxx 配置`

### Example commit subjects

- `feat: upgrade lvgl to 9.5 and wire launcher init`
- `fix: 修正 LTDC VSync 通知链路`
- `refactor: 调整 LVGL 9.5 移植代码`
- `feat: 添加 STLink 下载配置`

### Commit generation process

Before proposing a commit message:

1. Check the staged diff with `git diff --cached`.
2. Determine the single main purpose of the change.
3. Match the purpose to the most appropriate commit type.
4. Write one concise commit subject in the existing style.
5. If the staged changes are unrelated, suggest splitting them into multiple commits.

### Output format

Unless otherwise requested, output only the final commit subject.

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