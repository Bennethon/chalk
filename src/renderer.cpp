#include "renderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <cmath>

namespace {
QPen strokePen(const Item &item, const QString &paper) {
    const QColor color = Renderer::color(item.stroke, paper);
    if (color.alpha() == 0 || item.strokeWidth <= 0)
        return Qt::NoPen;
    // Boxes keep crisp corners; drawn lines and curves get soft ones.
    const bool angular = item.type == Item::Rect || item.type == Item::Diamond || item.type == Item::Triangle
                         || item.type == Item::Text;
    QPen pen(color, item.strokeWidth, Qt::SolidLine, Qt::RoundCap, angular ? Qt::MiterJoin : Qt::RoundJoin);
    if (item.dash == 1) {
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({4, 3});
    } else if (item.dash == 2) {
        // A dash of almost nothing with round caps is a dot.
        pen.setDashPattern({0.1, 2.4});
    }
    return pen;
}

int textFlags(const Item &item) {
    static const int horizontal[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    static const int vertical[] = {Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom};
    int flags = horizontal[qBound(0, item.hAlign, 2)] | vertical[qBound(0, item.vAlign, 2)] | Qt::TextDontClip;
    if (!item.autoSize)
        flags |= Qt::TextWordWrap;
    return flags;
}

void paintArrow(QPainter *painter, const QPointF &tip, const QPointF &direction, qreal length, const QColor &color) {
    const QPointF base = tip - direction * length;
    const QPointF normal(-direction.y(), direction.x());
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(QPolygonF{tip, base + normal * length * 0.42, base - normal * length * 0.42});
}

void paintItem(QPainter *painter, const Document &document, const Item &item, bool withText) {
    const QString &paper = document.paper;
    painter->save();
    painter->setOpacity(item.opacity);

    if (item.type == Item::Line) {
        const Route route = document.route(item);
        const QPen pen = strokePen(item, paper);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(pen);
        painter->drawPath(route.trimmed);
        if (pen.style() != Qt::NoPen) {
            if (item.arrowStart)
                paintArrow(painter, route.start, -route.startDir, Document::arrowLength(item), pen.color());
            if (item.arrowEnd)
                paintArrow(painter, route.end, route.endDir, Document::arrowLength(item), pen.color());
        }
        if (withText && !item.text.isEmpty()) {
            // The label sits on the line, so it clears a patch of paper first.
            const QRectF box = Renderer::labelRect(document, item);
            painter->fillRect(box, Renderer::paperColor(paper));
            painter->setFont(Renderer::font(item));
            painter->setPen(Renderer::color(item.textColor, paper));
            painter->drawText(box, Qt::AlignCenter | Qt::TextDontClip, item.text);
        }
    } else {
        const bool fillable = item.isShape() || item.closed;
        painter->setBrush(fillable ? QBrush(Renderer::color(item.fill, paper)) : QBrush(Qt::NoBrush));
        painter->setPen(strokePen(item, paper));
        painter->drawPath(document.outline(item));
        if (withText && item.isShape() && !item.text.isEmpty()) {
            painter->setFont(Renderer::font(item));
            painter->setPen(Renderer::color(item.textColor, paper));
            painter->drawText(Document::textRect(item), textFlags(item), item.text);
        }
    }
    painter->restore();
}
}

QColor Renderer::paperColor(const QString &paper) { return paper == "light" ? QColor("#f6f5f1") : QColor("#131316"); }

QColor Renderer::color(const QString &name, const QString &paper) {
    if (name == "none" || name.isEmpty())
        return Qt::transparent;
    if (name == "ink")
        return paper == "light" ? QColor("#1c1c20") : QColor("#ececee");
    if (name == "paper")
        return paperColor(paper);
    const QColor parsed(name);
    return parsed.isValid() ? parsed : QColor(Qt::transparent);
}

QString Renderer::family(const QString &font) {
    return font == "serif" ? "serif" : font == "mono" ? "monospace" : "sans-serif";
}

QFont Renderer::font(const Item &item) {
    QFont font(family(item.font));
    font.setStyleHint(item.font == "serif" ? QFont::Serif : item.font == "mono" ? QFont::Monospace : QFont::SansSerif);
    font.setPixelSize(qMax(1, item.fontSize));
    font.setBold(item.bold);
    font.setItalic(item.italic);
    // Unhinted glyphs keep their shape at any zoom and in every export.
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QSizeF Renderer::textSize(const Item &item) {
    const QFontMetricsF metrics(font(item));
    const QRectF box = metrics.boundingRect(QRectF(0, 0, 100000, 100000), Qt::AlignLeft | Qt::AlignTop,
                                            item.text.isEmpty() ? QStringLiteral(" ") : item.text);
    return QSizeF(std::ceil(box.width()) + 1, std::ceil(box.height()));
}

QRectF Renderer::labelRect(const Document &document, const Item &line) {
    const QSizeF size = textSize(line) + QSizeF(12, 6);
    QRectF box(QPointF(), size);
    box.moveCenter(document.route(line).path.pointAtPercent(0.5));
    return box;
}

QRectF Renderer::paintBounds(const Document &document, const Item &item) {
    qreal margin = item.strokeWidth / 2 + 1;
    if (item.type == Item::Line)
        margin += Document::arrowLength(item) / 2;
    QRectF box = document.bounds(item).adjusted(-margin, -margin, margin, margin);
    if (item.type == Item::Line && !item.text.isEmpty())
        box = box.united(labelRect(document, item));
    return box;
}

void Renderer::paint(QPainter *painter, const Document &document, const QSet<QString> &only,
                     const QString &skipTextOf) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    for (const Item &item : document.items)
        if (only.isEmpty() || only.contains(item.id))
            paintItem(painter, document, item, item.id != skipTextOf);
}
