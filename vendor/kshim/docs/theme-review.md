# Authentic theme review artifacts

This page preserves the historical theme-gallery workflow. It does not select
the installed Y700 theme; current profiles are described in the
[Y700 menu guide](tb323fu-boot-menu.md).

The design-language implementation was developed on `feature/2610-design-language`
and reviewed through PR #1 against `feature/2610`. FLAT was not redesigned. The first complete
firmware/render validation at `e0906584a298a9c4e8d6c7eda3f194291183b797` passed
25 complete host CTest configurations (17 tests each), all 24 redesigned ARM64
runtime configurations and the GCC/Clang/Kconfig contracts. The workflow is the
source of truth for later commits; this paragraph is not a claim about future runs.

## Reproduce the viewer

The `all-menu-previews` Actions artifact retains the unchanged original PNGs,
per-theme manifests, rendered-checkout stamps, render logs and CTest logs. Extract
it so that the input contains `menu-FLAT/previews/manifest.json`, etc. Python 3.10+
and its standard library are sufficient; there is no network access or package
installation in the builder.

```sh
python3 -m unittest discover -s tests -p 'test_theme_review.py' -v
python3 tools/build_theme_review.py --input collected --output review
```

`review/index.html` uses the adjacent original PNGs. `review/standalone.html`
embeds the same bytes and can be distributed alone. `review/verification.json`
records the frame hashes, decoded-pixel hashes, source identities and test counts.
Both viewers support theme search, a complete theme overview, orientation/phase
selection, original-size viewing and original PNG downloads. Without a baseline,
old/new controls are disabled and **no FLAT pixel-comparison claim is made**.

The successful CI packaging job also creates `theme-review-html`, so a new render
run does not need a hand-built HTML deliverable. CI does not download an expiring
historical baseline or relabel a previous run as a new checkout.

## Optional old/new comparison

Extract each historical `menu-STYLE` artifact into its own `menu-STYLE` directory.
Use the exact historical workflow commit for old artifacts that did not contain
`source-commit.txt`. Its provenance is labelled `supplied`, not `stamped`.

```sh
python3 tools/build_theme_review.py --input collected --output comparison \
  --baseline baseline \
  --baseline-source d9cf99fed9b6ce64a1485ce4bf70e95028f08150
```

The baseline above comes from run `37879974147`. The original and redesigned
preview programs at the first completed validation use the same seven-entry
fixture (blob `86185e4587ee7c8f451e0b59af906c24e6f9f88a`). For other baselines, review
fixture/source compatibility separately: PNG bytes cannot prove how they were
produced. A submitted source identity does not authenticate an arbitrary image.

The builder rejects missing/duplicate frames, escaping paths, mixed commit
stamps, stale SHA256 values, malformed PNG CRCs, unsupported pixel encodings,
failed/incomplete CTest logs, FLAT pixel differences and unchanged non-FLAT
redesigns. It compares decoded RGB bytes for FLAT, not just PNG compression.
Expected-source checking cannot override an existing or absent current stamp.

The 1920x1080 / 1080x2340 four-stage baseline comparison contains 200 old and
200 new frames. Source stamps deliberately identify the *rendered checkout*:
PR-triggered runs may stamp their synthetic merge commit instead of the PR head.
The HTML must preserve that identity rather than substitute the latest branch tip.

## Review boundary

These are host LVGL frames, not photographs of a device and not interactive boot
emulation. The visible Boot control in a PNG is not clickable. The phase names
refer to samples of entry, ready, focus and confirmation animation, not a full
recording. Test/build success is not proof of native toolkit conformance, target
frame rate, input latency, memory suitability or complete accessibility.

Glass redraw cost, fixed-advance ASCII typography, effective design-unit scaling
and the reduced-effects path require target-device measurements; image hashes
alone do not establish their performance. No proprietary fonts or brand image
bundles are distributed by the gallery builder.
