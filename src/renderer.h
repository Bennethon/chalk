#pragma once

#include <QColor>
#include <QFont>
#include <QSet>
#include <QSizeF>

#include "document.h"

class QPainter;

// The one place a board is turned into pixels. The canvas, the PNG, the SVG
// and the PDF all paint through here, so what's exported is what was drawn.
namespace Renderer {
QColor paperColor(const QString &paper);
// Resolves "none", "ink", "paper", or a hex color against the paper.
QColor color(const QString &name, const QString &paper);
QFont font(const Item &item);
QString family(const QString &font);
// The room an item's text takes on one unwrapped run of lines.
QSizeF textSize(const Item &item);
// The box a line's label is painted in, centered on the middle of its run.
QRectF labelRect(const Document &document, const Item &line);
// Everything an item paints, stroke, arrowheads and label included.
QRectF paintBounds(const Document &document, const Item &item);

// `only` limits painting to those ids (empty paints everything);
// `skipTextOf` leaves out the text of the item being edited in place.
void paint(QPainter *painter, const Document &document, const QSet<QString> &only = {},
           const QString &skipTextOf = {});
}
