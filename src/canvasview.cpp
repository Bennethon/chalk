#include "canvasview.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <cmath>

#include "backend.h"
#include "renderer.h"

namespace {
const qreal minZoom = 0.05, maxZoom = 16;
}

CanvasView::CanvasView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setOpaquePainting(true);
}

QObject *CanvasView::editor() const { return m_backend; }

void CanvasView::setEditor(QObject *editor) {
    Backend *backend = qobject_cast<Backend *>(editor);
    if (backend == m_backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::changed, this, [this] { update(); });
        connect(m_backend, &Backend::editingChanged, this, [this] { update(); });
        connect(m_backend, &Backend::loaded, this, &CanvasView::fitContent);
    }
    emit editorChanged();
    update();
}

void CanvasView::setAccent(const QColor &accent) {
    if (accent == m_accent)
        return;
    m_accent = accent;
    emit accentChanged();
    update();
}

QPointF CanvasView::toScene(qreal x, qreal y) const { return m_origin + QPointF(x, y) / m_zoom; }

void CanvasView::panBy(qreal dx, qreal dy) {
    m_origin -= QPointF(dx, dy) / m_zoom;
    emit viewChanged();
    update();
}

void CanvasView::zoomAt(qreal x, qreal y, qreal factor) {
    const QPointF anchor = toScene(x, y);
    const qreal zoom = qBound(minZoom, m_zoom * factor, maxZoom);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    m_origin = anchor - QPointF(x, y) / m_zoom;
    emit viewChanged();
    update();
}

void CanvasView::zoomBy(qreal factor) { zoomAt(width() / 2, height() / 2, factor); }

void CanvasView::resetZoom() { zoomAt(width() / 2, height() / 2, 1 / m_zoom); }

void CanvasView::fitContent() {
    if (width() <= 0 || height() <= 0) {
        m_fitPending = true;
        return;
    }
    m_fitPending = false;
    m_placed = true;
    const QRectF content = m_backend ? m_backend->contentBounds() : QRectF();
    if (content.isEmpty()) {
        // An empty board starts with its origin in the middle of the window.
        m_zoom = 1;
        m_origin = -QPointF(width(), height()) / 2;
    } else {
        const qreal margin = 48;
        m_zoom = qBound(minZoom, qMin((width() - margin * 2) / content.width(),
                                      (height() - margin * 2) / content.height()), 1.0);
        m_origin = content.center() - QPointF(width(), height()) / 2 / m_zoom;
    }
    emit viewChanged();
    update();
}

void CanvasView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() <= 0 || newGeometry.height() <= 0)
        return;
    if (!m_placed || m_fitPending) {
        fitContent();
    } else {
        // Resizing the window keeps the middle of the view in the middle.
        m_origin -= QPointF(newGeometry.width() - oldGeometry.width(), newGeometry.height() - oldGeometry.height())
                    / 2 / m_zoom;
        emit viewChanged();
    }
    update();
}

void CanvasView::paint(QPainter *painter) {
    const QString paper = m_backend ? m_backend->paper() : QStringLiteral("dark");
    painter->fillRect(QRectF(0, 0, width(), height()), Renderer::paperColor(paper));
    if (!m_backend)
        return;
    paintGrid(painter);

    painter->save();
    painter->scale(m_zoom, m_zoom);
    painter->translate(-m_origin);
    Renderer::paint(painter, m_backend->document(), {}, m_backend->editingId());
    paintOverlay(painter);
    painter->restore();
}

// A dot grid that thins out as the view zooms away, so it reads as texture
// at any scale rather than as noise.
void CanvasView::paintGrid(QPainter *painter) const {
    qreal step = 24;
    while (step * m_zoom < 16)
        step *= 2;
    while (step * m_zoom > 64)
        step /= 2;
    QColor dot = Renderer::color("ink", m_backend->paper());
    dot.setAlpha(46);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(dot, 1));
    const QPointF end = toScene(width(), height());
    QList<QPointF> dots;
    for (qreal y = std::floor(m_origin.y() / step) * step; y <= end.y(); y += step)
        for (qreal x = std::floor(m_origin.x() / step) * step; x <= end.x(); x += step)
            dots << (QPointF(x, y) - m_origin) * m_zoom;
    painter->drawPoints(dots.constData(), dots.size());
    painter->setRenderHint(QPainter::Antialiasing, true);
}

void CanvasView::paintOverlay(QPainter *painter) const {
    const Document &doc = m_backend->document();
    // Marks keep their size on screen whatever the zoom.
    const qreal px = 1 / m_zoom;
    // The accent, kept readable against the paper: a bright one is darkened
    // on light paper and a dim one lifted on dark.
    QColor accent = m_accent;
    const qreal luminance = .2126 * accent.redF() + .7152 * accent.greenF() + .0722 * accent.blueF();
    if (doc.paper == "light" && luminance > .55)
        accent = accent.darker(190);
    else if (doc.paper != "light" && luminance < .25)
        accent = accent.lighter(190);
    const QPen line(accent, 1.5 * px);
    const QColor knob = Renderer::paperColor(doc.paper);
    const QStringList selection = m_backend->selection();
    const QString nodeEditId = m_backend->nodeEditId(), penId = m_backend->penId();

    auto square = [&](const QPointF &center, bool filled) {
        painter->setPen(line);
        painter->setBrush(filled ? QBrush(accent) : QBrush(knob));
        painter->drawRect(QRectF(center.x() - 4 * px, center.y() - 4 * px, 8 * px, 8 * px));
    };
    auto circle = [&](const QPointF &center, bool filled) {
        painter->setPen(line);
        painter->setBrush(filled ? QBrush(accent) : QBrush(knob));
        painter->drawEllipse(center, 4.5 * px, 4.5 * px);
    };

    // What a click would pick up.
    const Item *hovered = doc.find(m_backend->hoverId());
    if (hovered && !selection.contains(hovered->id)) {
        QColor soft = accent;
        soft.setAlphaF(0.7);
        painter->setPen(QPen(soft, 1.5 * px));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(doc.outline(*hovered));
    }

    // What a line being dragged would attach to.
    if (const Item *target = doc.find(m_backend->attachId())) {
        painter->setPen(QPen(accent, 2.5 * px));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(doc.outline(*target));
        for (int side = Item::Top; side <= Item::Left; ++side)
            circle(doc.sidePoint(*target, side), false);
    }

    painter->setBrush(Qt::NoBrush);
    for (const QString &id : selection) {
        const Item *item = doc.find(id);
        if (!item || id == nodeEditId)
            continue;
        painter->setPen(QPen(accent, 1 * px));
        painter->setBrush(Qt::NoBrush);
        if (item->type == Item::Line) {
            const Route route = doc.route(*item);
            painter->drawPath(route.path);
            if (selection.size() == 1) {
                circle(route.start, !item->from.isEmpty());
                circle(route.end, !item->to.isEmpty());
            }
        } else if (selection.size() > 1) {
            painter->drawRect(doc.bounds(*item));
        }
    }
    if (m_backend->resizable()) {
        const QRectF box = m_backend->selectionBounds();
        painter->setPen(line);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box);
        for (const QPointF &handle : Backend::handlePoints(box))
            square(handle, false);
    }

    // Anchors and curve handles of the path being reshaped or drawn.
    const Item *path = doc.find(nodeEditId.isEmpty() ? penId : nodeEditId);
    if (path && path->type == Item::Path) {
        painter->setPen(QPen(accent, 1 * px));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(doc.outline(*path));
        const int active = penId.isEmpty() ? m_backend->activeNode() : path->nodes.size() - 1;
        if (active >= 0 && active < path->nodes.size()) {
            const PathNode &node = path->nodes[active];
            for (const QPointF &handle : {node.in, node.out}) {
                if (handle.isNull())
                    continue;
                painter->setPen(QPen(accent, 1 * px));
                painter->drawLine(node.p, node.p + handle);
                circle(node.p + handle, false);
            }
        }
        for (int i = 0; i < path->nodes.size(); ++i)
            square(path->nodes[i].p, i == active);
        if (!penId.isEmpty() && m_backend->hasPenPreview() && !path->nodes.isEmpty()) {
            QPen preview(accent, 1 * px, Qt::DashLine);
            painter->setPen(preview);
            painter->drawLine(path->nodes.last().p, m_backend->penPreview());
        }
    }

    // The freehand stroke, before it's smoothed into a path.
    const QList<QPointF> pencil = m_backend->pencilPoints();
    if (pencil.size() > 1) {
        painter->setPen(QPen(Renderer::color("ink", doc.paper), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(pencil.constData(), pencil.size());
    }

    for (const QLineF &guide : m_backend->guides()) {
        painter->setPen(QPen(QColor("#ff4d8d"), 1 * px));
        painter->drawLine(guide);
    }

    // A measured span: a line with a tick at each end and its length beside
    // it, lettered at screen size whatever the zoom, on a patch of paper so
    // the number reads over whatever is underneath.
    auto measured = [&](const QLineF &span, const QColor &color) {
        const QPointF along = (span.p2() - span.p1()) / qMax(span.length(), 1e-6);
        const QPointF across(-along.y(), along.x());
        const bool level = qAbs(along.x()) > qAbs(along.y());
        painter->setPen(QPen(color, 1 * px));
        painter->drawLine(span);
        for (const QPointF &end : {span.p1(), span.p2()})
            painter->drawLine(end - across * 4 * px, end + across * 4 * px);

        painter->save();
        painter->translate(span.center());
        painter->scale(px, px);
        QFont font = painter->font();
        font.setPixelSize(11);
        painter->setFont(font);
        const QString size = QLocale().toString(qRound(span.length()));
        const QSizeF text = QFontMetricsF(font).size(Qt::TextSingleLine, size) + QSizeF(6, 2);
        // Above a level span, beside an upright one.
        QRectF box(QPointF(), text);
        box.moveCenter(level ? QPointF(0, -text.height() / 2 - 4) : QPointF(text.width() / 2 + 6, 0));
        QColor patch = knob;
        patch.setAlpha(215);
        painter->fillRect(box, patch);
        painter->setPen(color);
        painter->drawText(box, Qt::AlignCenter, size);
        painter->restore();
    };
    // Plain distances are quiet; a gap that matches its neighbours' is loud.
    QColor quiet = Renderer::color("ink", doc.paper);
    quiet.setAlpha(170);
    for (const QLineF &distance : m_backend->distances())
        measured(distance, quiet);
    for (const QLineF &spacer : m_backend->spacers())
        measured(spacer, QColor("#ff4d8d"));

    const QRectF marquee = m_backend->marquee();
    if (marquee.width() > 0 || marquee.height() > 0) {
        QColor wash = accent;
        wash.setAlphaF(0.12);
        painter->setPen(QPen(accent, 1 * px));
        painter->setBrush(wash);
        painter->drawRect(marquee);
    }
}
