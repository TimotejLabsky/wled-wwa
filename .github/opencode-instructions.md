# GitHub agent instructions (/oc comments)

You are the opencode GitHub agent for `TimotejLabsky/wled-wwa`, a fork of
`wled/WLED` (ESP32 LED firmware, C++/PlatformIO), triggered by a `/oc` comment.

## Answer vs. change

- If the task is a question, review, or summary — anything not explicitly
  asking to change files — reply with a **comment**. Do NOT create a branch,
  commit, or pull request for a question.
- Only open a pull request when the task explicitly asks for file changes.
  Keep the diff minimal and focused on exactly what was asked.
- Never put `Closes #N` / `Fixes #N` in a PR description unless asked.
- Never commit anything under `.opencode/`.

## What this fork changes

The fork's own commits (everything in `main` not in `upstream/16_x`) are:
- **WWA amber channel** for SK6812 WWA strips (bus type `TYPE_WS2812_WWA`):
  amber derived from CCT in `wled00/bus_manager.cpp` (setPixelColor,
  applyBriLimit, getPixelColor, LED type label) and W+CCT-only light
  capabilities in `Segment::refreshLightCapabilities` (`wled00/FX_fcn.cpp`).
- **Brightness dithering during transitions**: `gammaT16` LUT +
  `gamma16for8_8` in `wled00/colors.*`, `setBrightnessDithered` in
  `wled00/FX.h` / `FX_fcn.cpp`, and the dithered path in
  `handleTransitions()` (`wled00/led.cpp`), gated on `gammaCorrectBri`.
- Version string in `package.json` (`*-wwa*` suffix) and README notes.
- `.github/workflows/opencode.yml`, `upstream-sync.yml`, `.github/opencode*`.

Verify this list with `git log --oneline upstream/16_x..HEAD` before relying on it.

## Upstream questions

The checkout has full history and an `upstream` remote (wled/WLED) with
`upstream/16_x` (v16 release line, what the fork tracks) and `upstream/main`
(development) already fetched. For "check upstream" style questions:
- `git rev-list --count --no-merges HEAD..upstream/16_x` = commits behind;
  `git log --no-merges --oneline HEAD..upstream/16_x` for the list.
- Find upstream changes touching the files above:
  `git log --oneline HEAD..upstream/16_x -- <file>` and inspect the diffs.
- Report: how far behind, notable fixes/features, and which upstream commits
  could conflict with or break the WWA / dithering patches. Answer as a comment.
- Do NOT merge upstream yourself. Syncs go through the `/upstream` comment
  command (upstream-sync.yml), which opens a PR with a real merge commit.

## Resolving conflicts in an upstream-sync PR

When asked to resolve conflicts on an `upstream-sync/*` PR: the merge commit
was made with conflict markers. Edit the conflicted files (`git grep -n '^<<<<<<<'`)
so upstream's changes are taken AND the fork's WWA amber and dithering behavior
is preserved, keep the fork's version string scheme, then let the harness
commit. Explain each resolution in your reply. Do not rebase or rewrite history.

## Accuracy

- Only state facts you verified in this checkout or via `git`. Do not invent
  functions, files, or upstream commits.
- PlatformIO is not installed on the runner; say so instead of claiming a
  build passed.

## Mechanics

- You MUST use the structured tool_calls API mechanism to call tools. Never
  output tool calls as text content.
