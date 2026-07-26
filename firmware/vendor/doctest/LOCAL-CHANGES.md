# doctest — local changes

Vendored: **doctest 2.5.3**, single header, MIT (Copyright (c) 2016-2023 Viktor
Kirilov). The licence block at the top of `doctest.h` is untouched.

Source: <https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h>

`doctest.h` is **not** byte-identical to upstream. Four comments naming other
companies and their products were reworded. Nothing executable was touched: all
four are inside comments, and three of them sit in Windows-only code paths this
firmware never compiles.

| Where | Upstream comment referred to | Now reads |
|---|---|---|
| `~7958` | a named xUnit framework and its `gtest.cc` | "another xUnit framework" |
| `~325`  | an MSVC feature-support table URL | "see the vendor's compiler docs" |
| `~327`  | an MSVC version-numbering wiki URL | "see the compiler's `_MSC_VER` documentation" |
| `~7479` | mimicking a named framework's junit output | "the usual junit output" |

All four survived the 2.4.11 -> 2.5.3 bump unchanged, so re-applying them is a
mechanical `python3` replace; the guard is that each pattern must match exactly
once.

## On upgrade

`diff` against upstream will show these four hunks; re-apply them, or drop the
vendored copy entirely and fetch doctest at configure time, which removes 9148
lines of third-party code from this repo along with the problem.

`.github/workflows/deps-check.yml` reports when upstream moves ahead, since no
Dependabot ecosystem can see a vendored header.
