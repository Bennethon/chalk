#pragma once

#include <QImage>
#include <QStringList>

#include "document.h"

// Writes a board, or part of one, as PNG, JPEG, SVG or PDF. The format comes
// from the file name.
namespace Exporter {
struct Options {
    // Pixels per board unit, for the raster formats.
    qreal scale = 2;
    // Paint the paper behind the drawing rather than leaving it see-through.
    bool background = true;
    // Only these items; empty exports the whole board.
    QStringList only;
    qreal margin = 24;
};

QStringList formats();
QRectF area(const Document &document, const Options &options);
QImage image(const Document &document, const Options &options);
bool write(const Document &document, const QString &path, const Options &options, QString *error);
}
