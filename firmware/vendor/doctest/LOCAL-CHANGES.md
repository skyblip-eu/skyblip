# doctest — local changes

Vendored: **doctest 2.4.11**, single header, MIT (Copyright (c) 2016-2023 Viktor
Kirilov). The licence block at the top of `doctest.h` is untouched.

`doctest.h` is **not** byte-identical to upstream. Four comments naming other
companies and their products were reworded. Nothing executable was touched: all
four are inside comments, and three of them sit in Windows-only code paths this
firmware never compiles.

| Where | Upstream comment referred to | Now reads |
|---|---|---|
| `~4717` | a named xUnit framework and its `gtest.cc` | "another xUnit framework" |
| `~256`  | an MSVC feature-support table URL | "see the vendor's compiler docs" |
| `~258`  | an MSVC version-numbering wiki URL | "see the compiler's `_MSC_VER` documentation" |
| `~5840` | mimicking a named framework's junit output | "the usual junit output" |

## On upgrade

`diff` against upstream will show these four hunks; re-apply them, or drop the
vendored copy entirely and fetch doctest at configure time, which removes 7106
lines of third-party code from this repo along with the problem.
