.pragma library

// Every icon is one stroked path on a 24 by 24 grid, so they all share a
// weight and can be recolored by the button that shows them.
var paths = {
    select: "M5 3 L5 19 L9.5 14.5 L12.5 21 L15 20 L12 13.5 L18.5 13.5 Z",
    rect: "M4 6 H20 V18 H4 Z",
    ellipse: "M4 12 A8 7 0 1 0 20 12 A8 7 0 1 0 4 12 Z",
    diamond: "M12 3 L21 12 L12 21 L3 12 Z",
    triangle: "M12 4 L21 20 H3 Z",
    text: "M5 7 V5 H19 V7 M12 5 V19 M9 19 H15",
    line: "M5 19 L19 5",
    arrow: "M5 19 L19 5 M11 5 H19 V13",
    pen: "M12 3 L17 12 L14.5 18 H9.5 L7 12 Z M12 3 V11 M8.5 21 H15.5",
    pencil: "M3 17 C6 7 9 7 11 12 S15 19 17 13 S20 6 21 6",
    undo: "M8 6 L3 11 L8 16 M3 11 H15 A5 5 0 0 1 15 21 H11",
    redo: "M16 6 L21 11 L16 16 M21 11 H9 A5 5 0 0 0 9 21 H13",

    alignLeft: "M4 3 V21 M8 6 H18 V10 H8 Z M8 14 H14 V18 H8 Z",
    alignHCenter: "M12 3 V21 M5 6 H19 V10 H5 Z M8 14 H16 V18 H8 Z",
    alignRight: "M20 3 V21 M6 6 H16 V10 H6 Z M10 14 H16 V18 H10 Z",
    alignTop: "M3 4 H21 M6 8 V18 H10 V8 Z M14 8 V14 H18 V8 Z",
    alignVCenter: "M3 12 H21 M6 5 V19 H10 V5 Z M14 8 V16 H18 V8 Z",
    alignBottom: "M3 20 H21 M6 6 V16 H10 V6 Z M14 10 V16 H18 V10 Z",
    distributeH: "M4 3 V21 M20 3 V21 M9 7 H15 V17 H9 Z",
    distributeV: "M3 4 H21 M3 20 H21 M7 9 H17 V15 H7 Z",

    toFront: "M4 4 H20 M12 20 V9 M7 14 L12 9 L17 14",
    forward: "M12 20 V6 M7 11 L12 6 L17 11",
    backward: "M12 4 V18 M7 13 L12 18 L17 13",
    toBack: "M4 20 H20 M12 4 V15 M7 10 L12 15 L17 10",
    group: "M4 8 V4 H8 M16 4 H20 V8 M20 16 V20 H16 M8 20 H4 V16 M8 8 H16 V16 H8 Z",
    ungroup: "M4 8 V4 H8 M16 4 H20 V8 M20 16 V20 H16 M8 20 H4 V16 M7 7 H12 V12 H7 Z M13 13 H17 V17 H13 Z",

    textLeft: "M4 6 H20 M4 10 H14 M4 14 H20 M4 18 H12",
    textCenter: "M4 6 H20 M7 10 H17 M4 14 H20 M8 18 H16",
    textRight: "M4 6 H20 M10 10 H20 M4 14 H20 M12 18 H20",
    textTop: "M4 4 H20 M8 9 H16 M8 13 H16",
    textMiddle: "M4 4 H20 M4 20 H20 M8 10 H16 M8 14 H16",
    textBottom: "M4 20 H20 M8 11 H16 M8 15 H16",

    solid: "M3 12 H21",
    dashed: "M3 12 H8 M10.5 12 H13.5 M16 12 H21",
    dotted: "M4 12 H4.1 M9.3 12 H9.4 M14.6 12 H14.7 M20 12 H20.1",
    straight: "M5 18 L19 6",
    elbow: "M4 18 H12 V6 H20",
    curved: "M4 18 C12 18 12 6 20 6",
    arrowStart: "M21 12 H4 M9 7 L4 12 L9 17",
    arrowEnd: "M3 12 H20 M15 7 L20 12 L15 17",
    plus: "M12 5 V19 M5 12 H19",
    minus: "M5 12 H19",
    fit: "M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15",
    sun: "M12 8 A4 4 0 1 0 12 16 A4 4 0 1 0 12 8 Z M12 2 V4 M12 20 V22 M2 12 H4 M20 12 H22 M4.9 4.9 L6.3 6.3 M17.7 17.7 L19.1 19.1 M4.9 19.1 L6.3 17.7 M17.7 6.3 L19.1 4.9",
    moon: "M20 14.5 A8 8 0 1 1 9.5 4 A6.5 6.5 0 0 0 20 14.5 Z",
    none: "M5 19 L19 5"
}
