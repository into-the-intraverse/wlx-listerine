# Research: lightweight alternatives to tree-sitter for static highlighting

**Date:** 2026-06-17
**Goal:** tree-sitter is memory-intense; find a lighter engine for *static* text coloring
(no editing / no incremental re-parse needed). Research only — no implementation.

---

## 1. Constraints this codebase imposes

These narrow the field hard, so they go first:

- **Native C++20, Windows-only, "minimalistic, native Direct2D."** No GUI-framework
  dependency (no Qt/GTK). Adding a heavy runtime fights the project's ethos.
- **The engine sits behind a C ABI** (`include/wlx_core/abi.h`, ABI v5). The plugins
  only ever see `WlxColorSpan { start, length, color, bg_color, modifiers }` ranges —
  **the engine is already an implementation detail behind that boundary.** Anything that
  can turn "byte range → styled spans" drops in without touching the plugins.
- **Themes are Helix** (TextMate-style *scope → color* resolution, `config/themes/*.toml`).
  How cleanly an alternative's token names map to scope strings decides how much of the
  theme system survives.
- **26 languages / 27 grammar DLLs** today, fetched + compiled by CMake FetchContent
  (`cmake/grammars.cmake`) plus the tree-sitter runtime. One feature (`cpp_grammar =
  unreal`) depends on a tree-sitter *fork*.

## 2. Honest framing: how much memory is actually left to win?

The memory-optimization project (2026-06) already **frees the tree-sitter tree after the
background sweep** and keeps only a flat `SpanTable`. So the picture is *not* "tree-sitter
holds a giant CST forever" anymore. Breaking down sqlite (8.85 MB source, ~74 MB held):

| Component                         | ~Size  | Engine-dependent? |
|-----------------------------------|--------|-------------------|
| Raw UTF-8 bytes                   | 8.9 MB | No                |
| Sealed `SpanTable` (colors)       | 22.8 MB| **No** — any engine fills this |
| Line/geometry indices             | few MB | No                |
| Process baseline (grammar DLL + compiled query + D2D/DWrite factories + **parse-arena high-water**) | ~38 MB | **Partly** |
| Transient CST during parse+sweep  | ~140 MB (freed) | **Yes** |

Conclusions that should temper expectations:

1. **Steady-state retained memory is already mostly engine-independent.** Switching
   engines does *not* shrink the SpanTable, raw bytes, or indices.
2. **The real, large win is the transient peak.** tree-sitter's CST for sqlite is ~15× the
   file (~140 MB) and exists during parse+sweep; the note even flags json.hpp's transient
   peak going *+23%* because the sweep coexists with the live tree. A streaming lexer
   **never materializes a CST**, so peak collapses toward `O(file + spans)`.
3. **Secondary wins:** the tree-sitter parse-arena retains freed pages (part of that
   ~38 MB high-water); a lexer's arena is ~0. And dropping tree-sitter removes the runtime
   + 27 grammar DLLs + the whole FetchContent/CI-tarball machinery (a known CI pain point).

So the accurate pitch is **"cut the transient peak + simplify the build,"** not "cut
steady-state held memory in half." Worth deciding up front whether peak is the metric you
care about.

## 3. The dividing line: CST engines vs. streaming lexers

- **Tree/AST engines (tree-sitter):** build a full concrete syntax tree, then run capture
  queries. Accurate, but the tree *is* the memory cost, and it can't partially parse
  (confirmed dead-end in `project_treesitter_fragment_highlighting`).
- **Streaming lexers (everything below):** a state machine walks the text line-by-line and
  emits styled tokens. Retained state is `O(context-stack depth)` ≈ tiny. **No tree ever
  exists.** This is the architectural property that kills the transient peak. Cost: coloring
  is heuristic/lexical, not full-grammar semantic.

For *static coloring with no editing*, the streaming-lexer model is the natural fit — the
one thing tree-sitter buys you (incremental re-parse on edit) is exactly what we don't need.

## 4. Candidate comparison

| Engine | Lang | Runtime added | Mem model | Coverage | Theme fit | Verdict |
|---|---|---|---|---|---|---|
| **Lexilla** | C++ | none (1 lib, all lexers built-in) | streaming, ~0 retained | 100+ lexers, all 26 here | per-lexer **numeric** style IDs → needs a style→scope map | **Top pick for this project** |
| **syntect** | Rust | rustc/cargo + C-FFI shim | streaming, line state | Sublime/TextMate syntaxes (bat's set) | emits **TextMate scope strings** → maps straight onto Helix infra | **Strong #2 if Rust is OK** |
| TextMate engine (Oniguruma + interpreter) | C (regex) + ? | Oniguruma is C; **no mature native-C++ interpreter** exists | streaming | every VS Code grammar (JSON) | native scope strings (best fit) | weak — you'd port/write the interpreter |
| KSyntaxHighlighting (KDE) | C++ | **QtCore** | streaming | ~300 Kate XML defs | scope-ish | quality is great, but Qt dependency is a dealbreaker here |
| Chroma | Go | cgo/Go runtime | streaming | Pygments-derived | token types | wrong runtime |
| Pygments | Python | CPython | streaming | huge | token types | wrong runtime, slow |
| GNU source-highlight / `highlight` | C++ | small | regex | fewer langs, less active | own model | not competitive on coverage |

## 5. Top candidate: Lexilla

- **What it is:** the lexer library split out of Scintilla 5.0. Verified: *"Lexilla does not
  interact with the display so there is no need to compile it for a particular GUI
  toolkit"* — it's a pure static/shared lib. Used standalone by e.g. Scintillua.
- **License:** HPND (Historical Permission Notice and Disclaimer) — permissive, explicitly
  allows commercial/closed use; only requires retaining the notice. Compatible.
- **Coverage:** 100+ hand-written lexers; covers every language currently shipped here plus
  many more, **with zero per-grammar downloads** (all compiled into the one lib). This
  alone deletes `cmake/grammars.cmake`, the 27 grammar DLLs, and the CI tarball setup.
- **API (standalone):** `CreateLexer("cpp")` → an `ILexer5*`. The host implements a small
  `IDocument` (read bytes via `GetCharRange`/`CharAt`/`Length`, receive styles via
  `StartStyling`/`SetStyles`) over a flat byte buffer, calls `Lex(...)`, then reads the
  per-character style bytes back out. ~a few hundred lines of glue (Scintillua and others
  do exactly this). It naturally supports **lexing only a byte range**, matching the
  existing viewport-scoped `wlx_core_highlight_range` shape.
- **Main integration cost — theme remapping:** lexers emit **numeric style IDs that are
  per-lexer** (`SCE_C_COMMENT`, `SCE_P_STRING`, …). There is no universal scope vocabulary,
  so you'd build a `lexer-id × style-number → scope-name` table (this is exactly what
  Notepad++'s `stylers.xml` is). Bounded one-time work (~26 small maps), but it's the part
  that doesn't reuse the Helix scope names for free.
- **What's lost:** the Unreal-C++ tree-sitter fork has no Lexilla equivalent; that variant
  feature would need a different approach or be dropped.

## 6. Strong alternative: syntect

- **What it is:** Rust library highlighting via Sublime Text `.sublime-syntax` (and
  TextMate) definitions; the engine behind `bat`. Line-by-line context-stack state machine,
  **no AST** — "compact binary representation of scopes," with optional state caching *"at
  the cost of more memory"* (i.e. baseline is low/streaming; we wouldn't need the cache for
  a one-pass sweep).
- **Why it's attractive here:** it emits **TextMate scope strings** (`keyword.control`,
  `string.quoted.double`, …). Helix themes *are* TextMate-derived scopes — so syntect maps
  onto the **existing** `HelixTheme` resolution far more directly than Lexilla's numeric
  IDs. Theme system survives largely intact.
- **Cost:** adds the Rust toolchain to the build and a thin Rust `cdylib`/`staticlib`
  exposing the same C ABI we already have (so the plugins still see `WlxColorSpan`). Ships
  its own curated syntax set (covers all 26). This is the cleanest *quality + theme-fit*
  option if a Rust dependency is acceptable in an otherwise pure-C++/MSVC build.

## 7. Why not the TextMate-grammar route directly

It's the most *theme-aligned* model (native scope strings, every VS Code language has a
grammar), but the reference engine is TypeScript (`vscode-textmate`) and the only mature
port is **C# (`TextMateSharp`, needs .NET)**. There is **no mature native-C++ engine** — you'd
be writing/porting the grammar interpreter on top of Oniguruma yourself. That's a build
project, not a drop-in. syntect is effectively "the TextMate model, already implemented,
in a linkable form."

## 8. Recommendation

1. **If the goal is max memory-peak reduction + radical build/bundle simplification, and
   staying pure-C++/MSVC: → Lexilla.** Biggest structural win (one lib replaces runtime +
   27 DLLs + fetch machinery), lowest memory, broadest built-in coverage. Accept the
   per-lexer style→scope mapping layer and the loss of the Unreal-C++ fork.
2. **If best highlighting quality + reusing the Helix scope theme system matters more, and a
   Rust dependency is acceptable: → syntect** behind the same C ABI.
3. **TextMate-direct, KSyntaxHighlighting, Chroma, Pygments:** rejected for this project
   (no native-C++ engine / Qt / wrong runtime).

**Caveat to confirm with the user before any prototype:** because the tree is already freed
post-sweep, the *steady-state* held-memory improvement is modest; the case for switching
rests on **(a) transient peak, (b) build/bundle/CI simplification, (c) parse-arena
high-water**. If steady-state held memory is the metric the user actually cares about, an
engine swap is the wrong lever and we should instead look at the SpanTable/index sizes.

## 9. Open questions

- Is the **transient peak** the metric of record, or steady-state held? (Decides whether
  this is worth doing at all — see §2/§8.)
- Is a **Rust toolchain** acceptable in the build? (Gates syntect.)
- Is the **Unreal-C++ variant** a must-keep? (Only tree-sitter has it.)
- Acceptable to trade tree-sitter's semantic captures for **lexical/heuristic** coloring on
  the long tail of languages? (Mainstream langs are comparable; niche ones vary.)

## Sources

- Lexilla — https://www.scintilla.org/Lexilla.html , https://www.scintilla.org/LexillaDoc.html
- Scintilla/Lexilla license (HPND) — https://www.scintilla.org/License.txt
- syntect — https://github.com/trishume/syntect , https://docs.rs/syntect
- vscode-textmate (TS reference) — https://github.com/microsoft/vscode-textmate
- TextMateSharp (C# port) via AlterNET — https://www.alternetsoft.com/blog/text-mate-parsing
- tree-sitter highlighting model — https://tree-sitter.github.io/tree-sitter/3-syntax-highlighting.html
