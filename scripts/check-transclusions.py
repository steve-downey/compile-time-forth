#!/usr/bin/env python3
# scripts/check-transclusions.py                                   -*-Python-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Verify every #+transclude: in the repository resolves, by document category.

Two categories, per docs/epistolary-pinning-plan.md P1:

  posts   docs/blog/post-*.org --- epistolary, pinned.  Each link is
          [[orgit-file:REPO::REV::PATH::UUID]]; the anchor pair must exist at
          REV, and the code must actually be present in the generated .md.

  living  docs/compiler_architecture.org, compile-time-forth.org --- rolling,
          worktree-resolved.  Each [[file:PATH::UUID]] target must exist in the
          worktree and carry its anchor pair.

Greping the generated .md for a leftover "#+transclude" does not work: a
transclusion that fails leaves no directive behind, it leaves nothing at all,
and the code block silently disappears.  That is how the whole series came to
ship empty code blocks (pins.md DISC-4).  So the post check compares the
pinned region against what the .md actually contains.
"""

import glob
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()

POSTS = os.path.join(ROOT, "docs/blog/post-*.org")
LIVING = ["docs/compiler_architecture.org", "compile-time-forth.org"]

PINNED_RE = re.compile(r"\[\[orgit-file:([^:\]]+)::([^:\]]+)::([^:\]]+)::([^:\]]+)\]\]")
FILE_RE = re.compile(r"#\+transclude:\s*\[\[file:([^:\]]+)::([^:\]]+)\]\]")
STALE_RE = re.compile(r"\[\[orgit:")

errors = []
checked = 0


def fail(where, msg):
    errors.append(f"{where}: {msg}")


def region(text, uuid):
    """Lines strictly between the anchor line and the '<uuid> end' line.

    This is what `:lines 2- :end "<uuid> end"` yields: the anchor line is
    dropped by :lines, the end marker line is excluded by :end.
    """
    lines = text.split("\n")
    beg = end = None
    endm = uuid + " end"
    for i, line in enumerate(lines):
        if beg is None and uuid in line and endm not in line:
            beg = i
        elif beg is not None and endm in line:
            end = i
            break
    if beg is None or end is None:
        return None
    return lines[beg + 1:end]


def shape(lines):
    """Non-blank lines, stripped.

    Transclusion re-indents the first and last line of a region, so an exact
    substring test would report false failures.  Comparing the stripped
    non-blank lines still catches an empty block, a truncated block, or code
    that is not the code the pin names --- which is everything this is for.
    """
    return [l.strip() for l in lines if l.strip()]


def git_show(rev, path):
    r = subprocess.run(["git", "-C", ROOT, "show", f"{rev}:{path}"],
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


for post in sorted(glob.glob(POSTS)):
    name = os.path.relpath(post, ROOT)
    text = open(post).read()

    if STALE_RE.search(text):
        fail(name, "unpinned orgit: link --- posts must pin (P1)")
    for _ in FILE_RE.finditer(text):
        fail(name, "worktree file: transclusion --- posts must pin (P1)")

    md_path = post[:-4] + ".md"
    md = open(md_path).read() if os.path.exists(md_path) else None

    for _repo, rev, path, uuid in PINNED_RE.findall(text):
        checked += 1
        blob = git_show(rev, path)
        if blob is None:
            fail(name, f"{path} does not exist at {rev}")
            continue
        if blob.count(uuid) != 2:
            fail(name, f"{path} at {rev}: anchor {uuid} appears "
                       f"{blob.count(uuid)} times, want exactly 2")
            continue
        body = shape(region(blob, uuid))
        if not body:
            fail(name, f"{path} at {rev}: anchor {uuid} encloses nothing")
            continue
        if md is None:
            fail(name, f"{md_path} not generated --- run make blog-md")
            continue
        if not all(line in md for line in body):
            missing = next(l for l in body if l not in md)
            fail(name, f"{path} at {rev}: region {uuid} is not in the "
                       f"generated .md (first missing line: {missing!r})")

for doc in LIVING:
    full = os.path.join(ROOT, doc)
    if not os.path.exists(full):
        continue
    text = open(full).read()
    if "orgit-file:" in text:
        fail(doc, "pinned orgit-file: link in a living document --- living "
                  "documents roll forward with the worktree (P1)")
    for path, uuid in FILE_RE.findall(text):
        checked += 1
        target = os.path.normpath(os.path.join(os.path.dirname(full), path))
        if not os.path.exists(target):
            fail(doc, f"{path} does not exist in the worktree")
            continue
        blob = open(target).read()
        if blob.count(uuid) != 2:
            fail(doc, f"{path}: anchor {uuid} appears {blob.count(uuid)} "
                      f"times, want exactly 2")

if errors:
    print(f"{len(errors)} transclusion problem(s):", file=sys.stderr)
    for e in errors:
        print(f"  {e}", file=sys.stderr)
    sys.exit(1)

print(f"transclusions ok: {checked} checked "
      f"({len(glob.glob(POSTS))} posts pinned, {len(LIVING)} living documents)")
