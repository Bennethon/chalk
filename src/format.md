# The Chalk board format

A board is one JSON file ending in `.chalk`. Coordinates are in board units
(one unit is one pixel at 100% zoom), x grows to the right and y grows down.
The board is endless: any coordinates work, negative ones included.

```json
{
  "chalk": 1,
  "paper": "dark",
  "items": [
    { "id": "start", "type": "ellipse", "x": 0, "y": 0, "w": 140, "h": 70, "text": "Start" },
    { "id": "check", "type": "diamond", "x": -10, "y": 140, "w": 160, "h": 110, "text": "Ready?" },
    { "id": "a", "type": "line", "from": "start", "to": "check", "arrowEnd": true }
  ]
}
```

`chalk` is the format version, always `1`. `paper` is `"dark"` or `"light"`.
`items` are painted in order, so later items sit on top of earlier ones.

## Every item

| Key | Meaning | Default |
| --- | --- | --- |
| `id` | Unique name. Lines use it to attach. | made up when missing |
| `type` | `rect`, `ellipse`, `diamond`, `triangle`, `text`, `line`, `path` | required |
| `stroke` | Outline or line color | `ink` (`none` for `text`) |
| `strokeWidth` | 0 to 40 | `2` |
| `dash` | `solid`, `dashed`, `dotted` | `solid` |
| `opacity` | 0 to 1 | `1` |
| `groups` | Group ids, innermost first. Items that share their last id select and move as one. | none |

Colors are `"#rrggbb"`, `"#aarrggbb"` (see-through), `"none"`, or one of two
names that follow the paper: `"ink"` (light on dark paper, dark on light) and
`"paper"` (the paper's own color). Prefer `ink` and `paper` so a board still
reads when its paper is switched.

## Shapes: `rect`, `ellipse`, `diamond`, `triangle`, `text`

| Key | Meaning | Default |
| --- | --- | --- |
| `x`, `y`, `w`, `h` | The box the shape fills | `0`, `0`, `160`, `100` |
| `fill` | Fill color | `paper` (`none` for `text`) |
| `radius` | Corner radius, `rect` and `text` only | `0` |
| `text` | Text inside the shape; `\n` breaks a line | none |
| `font` | `sans`, `serif`, `mono` | `sans` |
| `fontSize` | In board units | `16` |
| `bold`, `italic` | | `false` |
| `textColor` | | `ink` |
| `align` | `left`, `center`, `right` | `center` (`left` for `text`) |
| `valign` | `top`, `middle`, `bottom` | `middle` (`top` for `text`) |
| `autoSize` | `text` only: the box hugs the text instead of wrapping it | `false` |

Text wraps inside the shape and is not clipped, so give a shape room for its
words: about 9 units of width per character at the default size, and 22 units
of height per line. A `text` item is a box with no fill and no outline.

## Lines: `line`

| Key | Meaning | Default |
| --- | --- | --- |
| `from`, `to` | Ids of the shapes the ends attach to | loose ends |
| `fromSide`, `toSide` | `auto`, `top`, `right`, `bottom`, `left` | `auto` |
| `x1`, `y1`, `x2`, `y2` | Where a loose end is. Ignored for an attached end. | `0` |
| `arrowStart`, `arrowEnd` | Arrowheads | `false` |
| `routing` | `straight`, `elbow`, `curved` | `straight` |
| `text` | A label on the middle of the line | none |

An attached end follows its shape and stops at the shape's outline. With side
`auto` it leaves from whichever side faces the other end, which is nearly
always what a diagram wants. Lines only attach to shapes, not to other lines
or paths. Put lines before the shapes they join, or after: either works,
since an attached line never runs under its shape.

## Paths: `path`

| Key | Meaning | Default |
| --- | --- | --- |
| `nodes` | The anchors, in order; at least one | required |
| `closed` | Join the last anchor back to the first | `false` |
| `fill` | Fill color, painted only when closed | `none` |

Each node is `{ "x": 10, "y": 20 }`, optionally with curve handles
`"in": [dx, dy]` and `"out": [dx, dy]`, offsets from the anchor. A segment
between two nodes is straight when the first has no `out` and the second no
`in`, and a cubic Bezier otherwise.
