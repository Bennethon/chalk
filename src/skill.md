---
name: chalk
description: |
  Draw and edit diagrams and whiteboards with Chalk, a vector whiteboard whose
  boards are plain JSON files. Use for ANY request to draw, sketch, or change a
  diagram, flowchart, architecture sketch, wireframe, or whiteboard, to export
  one as PNG, SVG, or PDF, and for any .chalk file.
---

# Chalk

A Chalk board is one JSON file ending in `.chalk`: a list of shapes, lines,
and paths on an endless canvas. You write the file with your ordinary editing
tools; the `chalk` command checks and exports it. No command needs a display.

Run `chalk help format` first. It prints the whole board format, every key and
its default, for this version of Chalk.

## Workflow

1. Write or edit the `.chalk` file directly. Give every shape a short,
   meaningful `id` so lines can attach to it with `from` and `to`.
2. Check: `chalk check <board> --json` confirms the board loads and reports
   how many items it has, its size, and any line attached to a shape that
   isn't there.
3. Look: `chalk export <board> /tmp/board.png`, then view the PNG. Do this
   whenever layout matters, which for a diagram is always: check that text
   fits its shape and that nothing overlaps.
4. Export when asked: `chalk export <board> diagram.svg` (or `.png`, `.pdf`,
   `.jpg`). `--scale 4` makes a sharper PNG; `--transparent` leaves out the
   paper.

## Drawing well

- Lay shapes out on a grid: the same width and height for shapes of one
  kind, and even gaps between them (80 to 120 units reads well).
- Connect shapes with `line` items that name them in `from` and `to`, not
  with hand-placed coordinates. Attached lines stop at the outline and follow
  the shape if it moves. Leave `fromSide` and `toSide` out unless a line must
  leave from a particular side.
- Use `"routing": "elbow"` for flowcharts and block diagrams, `straight` for
  loose sketches.
- Use `ink` and `paper` for colors so the board reads on dark and light
  paper. Use a see-through fill such as `"#403e8ef7"` to tint a shape.
- Text is not clipped. Size each shape for its words, or break lines with
  `\n`.

## Working with the person

- `chalk open <board>` opens the editor for them; it needs their desktop, so
  do not wait on it. If they have the board open and unsaved, your edits to
  the file do not reach their window: ask them to reopen it.
- Every command takes `--json`, exits 0 on success and 1 on failure, and
  writes errors to stderr.
