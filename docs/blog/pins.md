# Blog transclusion pins

Every post in `docs/blog/` pins its transcluded code to a git revision, so the
code inside a diary entry stays the code that entry was written against. See
`docs/epistolary-pinning-plan.md` for the policy; `docs/blog/AGENTS.md` for the
authoring rule.

Living documents (`docs/compiler_architecture.org`, `compile-time-forth.org`)
are deliberately **not** pinned — they transclude with `file:` links against the
worktree and are supposed to roll forward.

## Pin table

| Post | Title | Phase covered | Pin SHA | Tag | Transcludes |
|---|---|---|---|---|---|
| Part 0 | The Pitch | F0 | `8db705c` | `blog/part-00` | 0 |
| Part 1 | Standing on the Scheme Repo | F1–F3 | `8db705c` | `blog/part-01` | 1 |
| Part 2 | Parser Combinators | F4 | `8db705c` | `blog/part-02` | 0 |
| Part 3 | Reading Forth Text | F5–F6 | `8db705c` | `blog/part-03` | 0 |
| Part 4 | The Grammar That Couldn't Be a Combinator | F7 | `8db705c` | `blog/part-04` | 0 |
| Part 5 | The Machine | F8–F10 | `8db705c` | `blog/part-05` | 2 |
| Part 6 | Elaboration and the Effect Checker | F11–F12 | `8db705c` | `blog/part-06` | 2 |
| Part 7 | The Oracle | F13 | `8db705c` | `blog/part-07` | 0 |
| Part 8 | The Program That Survives to Runtime | F14 | `8db705c` | `blog/part-08` | 2 |
| Part 9 | The One-Shot API | F15 | `8db705c` | `blog/part-09` | 2 |
| Part 10 | The Address Was Always a Cell | F16 | `185fc9a` | `blog/part-10` | 4 |
| Part 11 | The Two Cells the Checker Never Saw | F17 | `474659a` | `blog/part-11` | 5 |
| Part 12 | The Patch Stream Was Already There | F23 | `49c5f34` | `blog/part-12` | 1 |
| Part 13 | >IN Is Not a Cursor | F24 | `845bc7f` | `blog/part-13` | 2 |
| Part 14 | Correct by Accident | F25 | `2851c5b` | `blog/part-14` | 7 |

Pin SHAs are the commits that introduced each post. They are **not** the phase
merges, for the reason in DISC-1.

The tags exist locally and are **not pushed**: `AGENTS.md` does not let an agent
push without being asked. Push them before this branch reaches anyone else, or
the pinned links resolve only on this machine:

```sh
git push origin --tags 'blog/*'
```

## How the pins were chosen

Per the plan's P4, three methods, all run:

1. **First-parent merge walk.** `git log --first-parent --merges` gives the
   phase merges F0–F17; the "Phase covered" column is the arc each post
   narrates, matched by subject.
2. **Post date.** Each `#+DATE` equals its step's commit date, which agrees
   with the phase column throughout.
3. **Anchor test.** A revision is admissible for a post only if, for every
   transclusion in it, `git show REV:PATH` contains the UUID exactly twice.
   Run over all 30 first-parent commits of `main` for all 18 transclusions.

Methods 1–2 and method 3 disagree for every post, and method 3 governs
(P4.3): the anchor pairs do not exist at the phase merges. The earliest
admissible commit for each post is exactly the commit that introduced it, and
admissibility is contiguous from there to `HEAD` in every case.

## Discrepancies

**DISC-1 — anchors post-date their phases.** Every `// <uuid>` / `// <uuid> end`
pair was added by the blog commit that needed it, not by the step that wrote the
code. So no post is admissible at its own phase merge, and P4.3's fallback
applies throughout: pin to the earliest admissible commit. B5 closes this going
forward — the tag is cut at the merge, and the blog agent adds anchors before
the tag exists only if the orchestrator re-tags.

**DISC-2 — Parts 0–9 were backfilled in one commit.** `8db705c` ("docs: add
compile-time Forth blog series (Parts 0-9)", 2026-07-24) landed all ten posts
at once, after F16 merged, so all ten pin to it. Each transcluded region was
then compared against its nominal phase merge. Sixteen of eighteen regions are
byte-identical to their phase. Two are not:

- `foundation/parse_error.hpp` `fb523d66` (Part 1, phase F3). Differs from F3 by
  one space of `clang-format` continuation alignment, introduced by `a71f2e0`
  ("Apply clang-format across F2-F8 files"). Not substantive.
- `machine/instruction.hpp` `7bb215b8` (Part 8, phase F14) — **substantive
  leak, not fixed by pinning.** The doc comment on `compiled_program::data_space_size`
  read, at F14, "not yet consumed by this step's own VM (F16 wires `@`/`!`/`+!`),
  but recorded now since it is already known". F16 rewrote it to "F16's own
  @ref run consumes this at the start of every run". Part 8 is dated at F14 and
  publishes the F16 wording. The anchor does not exist at F14, so no admissible
  revision carries the F14 text; pinning preserves the leak rather than
  introducing it. Recorded, prose untouched — an editorial call, not a
  mechanical one.

**DISC-3 — Part 10's pin is later than its phase.** `185fc9a` lands after the
F17 merge `da05179`. All four of its regions are byte-identical to their content
at the F16 merge `fb09773`, so no F17 code reaches Part 10; the pin is late but
the content is right.

**DISC-4 — worktree transclusion was already broken.** Every existing link named
`~/src/compile-time-forth/main`, a directory that no longer exists. Exporting
`docs/blog/post-1-standing-on-scheme.org` against the current tree produced an
**empty** code block, not the code — the committed `.md` files still hold code
from when that path was live. Pinning fixes this permanently: `git show` resolves
against the object store, and the module falls back to the repository containing
the post when the named path is absent.
