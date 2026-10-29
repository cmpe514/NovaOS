# ui/original — the source UI, untouched

`ui.html` is the file exactly as uploaded to `main` (commit 691e81e), kept byte for byte so every change
made for the offline build can be diffed against it:

    sha256  f9e1a5f06007d311a0f3735512aa2f90afd41ccb7e4c4604452aa68ef0888e71  ui.html

**It is truncated.** The upload ends mid-statement at line 543 (`...content.`), so the rest of the
script and the closing `</script></body></html>` are missing. As uploaded, the inline script is a
syntax error and none of the page's JavaScript runs (no clock, no icons, no apps).

What was missing, judging from the code that survives, and how it was rebuilt in
`ui/src/js/app.js` (marked `RECONSTRUCTED`):

| Missing piece | Evidence in the surviving code | Rebuilt as |
|---|---|---|
| end of `renderList` | the cut-off `content.` line | `content.textContent=r.content\|\|''` |
| `renderRecords()` | called by `handler.onDataChanged` | renders the three lists |
| write queue | `writeQueue`, `lastWrite`, `sleep` declared but unused | `queueWrite()` keeps writes ordered |
| `removeRecord(id,button,row)` | bound to every Delete button | delete through `dataSdk.delete` |
| `toggleTask(id,checked)` | bound to every task checkbox; `pending` map read by `renderList` | optimistic toggle through `dataSdk.update` |
| form handlers | `#note-form`, `#task-form`, `#file-form` have no listener | create through `dataSdk.create` |
| start-up | `lucide.createIcons()` and `initialize()` never called | called at the end |

The visual tests render this file (completed with the same reconstructed block) as the reference, see
`tests/reference/build-reference.mjs`. If you have the complete original, drop it in here and run
`make test`: the reference and the parity tests will use it.
