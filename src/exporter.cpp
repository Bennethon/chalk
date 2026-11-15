#include "exporter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>

#include "renderer.h"

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Exporter", text); }

QSet<QString> idSet(const Exporter::Options &options) { return QSet<QString>(options.only.begin(), options.only.end()); }

void paintBoard(QPainter *painter, const Document &document, const Exporter::Options &options, const QRectF &area) {
    if (options.background)
        painter->fillRect(QRectF(QPointF(), area.size()), Renderer::paperColor(document.paper));
    painter->translate(-area.topLeft());
    Renderer::paint(painter, document, idSet(options));
}
}

QStringList Exporter::formats() { return {"png", "svg", "pdf", "jpg"}; }

QRectF Exporter::area(const Document &document, const Options &options) {
    const QSet<QString> only = idSet(options);
    QRectF area;
    bool first = true;
    for (const Item &item : document.items) {
        if (!only.isEmpty() && !only.contains(item.id))
            continue;
        const QRectF box = Renderer::paintBounds(document, item);
        area = first ? box : area.united(box);
        first = false;
    }
    if (first)
        return {};
    // Whole pixels, so a 1x export has no blurred edge row.
    return area.adjusted(-options.margin, -options.margin, options.margin, options.margin).toAlignedRect();
}

QImage Exporter::image(const Document &document, const Options &options) {
    const QRectF area = Exporter::area(document, options);
    if (area.isEmpty())
        return {};
    const qreal scale = qBound(0.25, options.scale, 8.0);
    QImage image((area.size() * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(scale, scale);
    paintBoard(&painter, document, options, area);
    return image;
}

bool Exporter::write(const Document &document, const QString &path, const Options &options, QString *error) {
    const QString format = QFileInfo(path).suffix().toLower();
    if (!formats().contains(format) && format != "jpeg") {
        *error = tr("Chalk exports PNG, SVG, PDF and JPG. \"%1\" is none of those.").arg(QFileInfo(path).fileName());
        return false;
    }
    const QRectF area = Exporter::area(document, options);
    if (area.isEmpty()) {
        *error = tr("There is nothing on the board to export.");
        return false;
    }

    // Written beside the target and renamed over it, so a failed export
    // never leaves half a file where a good one was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }

    if (format == "svg") {
        QSvgGenerator generator;
        generator.setOutputDevice(&file);
        generator.setSize(area.size().toSize());
        generator.setViewBox(QRectF(QPointF(), area.size()));
        generator.setTitle(QFileInfo(path).completeBaseName());
        generator.setDescription("Made with Chalk");
        QPainter painter(&generator);
        paintBoard(&painter, document, options, area);
    } else if (format == "pdf") {
        QPdfWriter writer(&file);
        writer.setCreator("Chalk");
        writer.setResolution(96);
        // One page, the size of the drawing: a board unit is a CSS pixel.
        writer.setPageSize(QPageSize(area.size() * 0.75, QPageSize::Point));
        writer.setPageMargins(QMarginsF());
        QPainter painter(&writer);
        paintBoard(&painter, document, options, area);
    } else {
        Options opaque = options;
        // JPEG has no transparency, so it always gets its paper.
        if (format != "png")
            opaque.background = true;
        const QImage image = Exporter::image(document, opaque);
        if (image.isNull() || !image.save(&file, format == "png" ? "PNG" : "JPG", format == "png" ? -1 : 92)) {
            *error = tr("Could not encode %1.").arg(QFileInfo(path).fileName());
            file.cancelWriting();
            return false;
        }
    }

    if (!file.commit()) {
        *error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
