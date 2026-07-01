# Git and Absolute Path Cleanup

Date: 2026-07-01 KST

This quarantine was created while cleaning project-local absolute paths and
repairing broken Git metadata.

## Root Git Repair

The original root `.git` directory was corrupt:

- `.git/index` was zero bytes.
- `refs/heads/main` and `refs/remotes/origin/HEAD` were empty.
- multiple loose objects were zero bytes.

It was moved to:

```text
deprecated/20260701_git_abs_cleanup/root_git_corrupt/
```

A clean `.git` directory was copied from:

```text
https://github.com/ballsun1003/FPGA_GPTalk.git
origin/main: a75ceaa4c2799f7e78d8943f14443e4dbad7fac4
```

Only Git metadata was replaced. Working tree files were not reset or
overwritten.

## Nested Git Directories

Nested `.git` directories were moved under:

```text
deprecated/20260701_git_abs_cleanup/nested_git/
```

This keeps the root repository from treating those directories as independent
repositories or accidental submodules.

## Deprecated Internal Docs

The old `docs/internal/deprecated/` directory was moved to:

```text
deprecated/20260701_git_abs_cleanup/docs_internal/deprecated/
```

Current active docs should live directly under `docs/` or `docs/internal/`.

The old `prompts/deprecated/` directory was moved to:

```text
deprecated/20260701_git_abs_cleanup/prompts_deprecated/deprecated/
```

Current active prompts should live directly under `prompts/`.

## Restore Rule

Do not restore files from this quarantine into active paths unless a later task
explicitly needs them. If restored, document the active path and reason in
`docs/00_ACTIVE_KR.md`.
