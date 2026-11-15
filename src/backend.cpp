#include "backend.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <functional>
#include <cmath>

#include "exporter.h"
#include "filepicker.h"
#include "renderer.h"

namespace {
const int maxUndo = 200;

qreal distance(const QPointF &a, const QPointF &b) { return QLineF(a, b).length(); }
QPointF lerp(const QPointF &a, const QPointF &b, qreal t) { return a + (b - a) * t; }
QSet<QString> toSet(const QStringList &ids) { return QSet<QString>(ids.begin(), ids.end()); }

// Carries points from one box to another. `to` may be flipped, which mirrors
// what's inside, and a flat `from` only moves things along its flat side.
struct RectMap {
    QRectF from, to;
    qreal sx() const { return qAbs(from.width()) > 1e-6 ? to.width() / from.width() : 1; }
    qreal sy() const { return qAbs(from.height()) > 1e-6 ? to.height() / from.height() : 1; }
    QPointF map(const QPointF &p) const {
        return {to.left() + (p.x() - from.left()) * sx(), to.top() + (p.y() - from.top()) * sy()};
    }
    QPointF scale(const QPointF &v) const { return {v.x() * sx(), v.y() * sy()}; }
};

void transformItem(Item &item, const RectMap &map) {
    if (item.isShape()) {
        item.rect = QRectF(map.map(item.rect.topLeft()), map.map(item.rect.bottomRight())).normalized();
    } else if (item.type == Item::Line) {
        item.p1 = map.map(item.p1);
        item.p2 = map.map(item.p2);
    } else {
        for (PathNode &node : item.nodes) {
            node.p = map.map(node.p);
            node.in = map.scale(node.in);
            node.out = map.scale(node.out);
        }
    }
}

// Holds a dragged point to the nearest 45 degrees around a fixed one.
QPointF constrainAngle(const QPointF &origin, const QPointF &pos) {
    const QPointF d = pos - origin;
    const qreal length = std::hypot(d.x(), d.y());
    const qreal step = M_PI / 4;
    const qreal angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return origin + QPointF(std::cos(angle), std::sin(angle)) * length;
}

// Ramer-Douglas-Peucker: keeps the points a freehand stroke turns on.
void simplifyInto(const QList<QPointF> &points, int first, int last, qreal epsilon, QList<QPointF> &out) {
    qreal worst = 0;
    int index = -1;
    const QLineF chord(points[first], points[last]);
    for (int i = first + 1; i < last; ++i) {
        qreal d;
        if (chord.length() < 1e-6) {
            d = distance(points[i], points[first]);
        } else {
            const QPointF a = points[i] - points[first], b = points[last] - points[first];
            d = qAbs(a.x() * b.y() - a.y() * b.x()) / chord.length();
        }
        if (d > worst) {
            worst = d;
            index = i;
        }
    }
    if (index >= 0 && worst > epsilon) {
        simplifyInto(points, first, index, epsilon, out);
        simplifyInto(points, index, last, epsilon, out);
    } else {
        out << points[last];
    }
}

Item::Type toolType(const QString &tool) {
    return tool == "ellipse" ? Item::Ellipse : tool == "diamond" ? Item::Diamond : tool == "triangle" ? Item::Triangle
         : tool == "text" ? Item::Text : Item::Rect;
}

bool overlaps(const QRectF &box, const QRectF &area) {
    // QRectF::intersects ignores flat boxes, and a straight line has one.
    return box.left() <= area.right() && box.right() >= area.left() && box.top() <= area.bottom()
           && box.bottom() >= area.top();
}
}

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    m_defaults.type = Item::Rect;
    if (m_picker) {
        connect(m_picker, &FilePicker::openSelected, this, [this](const QUrl &url) { load(url.toLocalFile()); });
        connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) { saveTo(url.toLocalFile()); });
        connect(m_picker, &FilePicker::exportSelected, this,
                [this](const QUrl &url) { exportTo(url.toLocalFile(), m_exportOptions); });
        connect(m_picker, &FilePicker::canceled, this, &Backend::saveAbandoned);
        connect(m_picker, &FilePicker::failed, this, [this](const QString &message) {
            say(message, true);
            emit saveAbandoned();
        });
    }
    refresh();
}

// --- State ---

void Backend::setTool(const QString &tool) {
    // Picking another tool ends the path being drawn rather than losing it.
    if (!m_penId.isEmpty() && tool != "pen")
        finishPen();
    if (tool != "select" && !m_nodeEditId.isEmpty())
        leaveNodeEdit();
    if (m_tool == tool)
        return;
    m_tool = tool;
    m_hoverId.clear();
    setCursor(tool == "select" ? Qt::ArrowCursor : tool == "text" ? Qt::IBeamCursor : Qt::CrossCursor);
    emit toolChanged();
    emit changed();
}

void Backend::setPaper(const QString &paper) {
    const QString value = paper == "light" ? "light" : "dark";
    if (value == m_doc.paper)
        return;
    checkpoint();
    m_doc.paper = value;
    if (!m_editingId.isEmpty())
        emit editingChanged();
    refresh();
}

void Backend::setDefaultPaper(const QString &paper) {
    const QString value = paper == "light" ? "light" : "dark";
    if (value == m_defaultPaper)
        return;
    m_defaultPaper = value;
    // A board nobody has touched yet is still a new board.
    if (m_doc.items.isEmpty() && m_filePath.isEmpty() && m_undo.isEmpty() && m_redo.isEmpty())
        m_doc.paper = value;
    refresh();
}

QString Backend::fileName() const {
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).completeBaseName();
}

QColor Backend::colorOf(const QString &name) const { return Renderer::color(name, m_doc.paper); }

void Backend::setSelection(const QStringList &ids) {
    m_selection = ids;
    m_coalesceKey.clear();
    if (!m_nodeEditId.isEmpty() && !(ids.size() == 1 && ids.first() == m_nodeEditId))
        leaveNodeEdit();
}

void Backend::setDirty(bool dirty) {
    if (dirty == m_dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void Backend::setCursor(int cursor) {
    if (cursor == m_cursor)
        return;
    m_cursor = cursor;
    emit cursorChanged();
}

void Backend::say(const QString &message, bool error) {
    m_message = message;
    m_messageIsError = error;
    emit messageChanged();
}

void Backend::clearMessage() { say({}); }

void Backend::pushUndo(const Snapshot &snapshot) {
    m_undo.append(snapshot);
    if (m_undo.size() > maxUndo)
        m_undo.removeFirst();
    m_redo.clear();
    m_coalesceKey.clear();
    setDirty(true);
    emit historyChanged();
}

void Backend::checkpoint() { pushUndo({m_doc, m_selection}); }

void Backend::checkpointFor(const QString &key) {
    if (key == m_coalesceKey && m_coalesceTimer.isValid() && m_coalesceTimer.elapsed() < 1000) {
        m_coalesceTimer.restart();
        return;
    }
    checkpoint();
    m_coalesceKey = key;
    m_coalesceTimer.restart();
}

// The first real change of a drag puts the board as it was at the press on
// the undo stack; a press that never moves anything leaves no step behind.
void Backend::beginChange() {
    if (m_dragChanged)
        return;
    m_dragChanged = true;
    pushUndo({m_dragOrigin, m_dragOriginSelection});
}

void Backend::restore(const Snapshot &snapshot) {
    m_doc = snapshot.doc;
    QStringList ids;
    for (const QString &id : snapshot.selection)
        if (m_doc.indexOf(id) >= 0)
            ids << id;
    m_nodeEditId.clear();
    m_node = -1;
    m_selection = ids;
    m_coalesceKey.clear();
    m_drag = Drag::None;
    setDirty(true);
    emit modeChanged();
    emit historyChanged();
    refresh();
}

void Backend::undo() {
    if (!m_penId.isEmpty()) {
        // A path with a single point undoes to nothing just by ending it.
        const Item *path = m_doc.find(m_penId);
        const bool kept = path && path->nodes.size() >= 2;
        finishPen();
        if (!kept)
            return;
    }
    if (m_undo.isEmpty())
        return;
    m_redo.append({m_doc, m_selection});
    restore(m_undo.takeLast());
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append({m_doc, m_selection});
    restore(m_redo.takeLast());
}

// --- What's selected, as the panel shows it ---

QRectF Backend::selectionBounds() const { return unitBounds(m_selection); }

QRectF Backend::contentBounds() const {
    QRectF all;
    bool first = true;
    for (const Item &item : m_doc.items) {
        const QRectF box = Renderer::paintBounds(m_doc, item);
        all = first ? box : all.united(box);
        first = false;
    }
    return all;
}

QRectF Backend::unitBounds(const QStringList &ids) const {
    QRectF all;
    bool first = true;
    for (const QString &id : ids) {
        const Item *item = m_doc.find(id);
        if (!item)
            continue;
        const QRectF box = m_doc.bounds(*item);
        // United by hand: QRectF::united drops a box with no area, and a
        // straight line is exactly that.
        if (first) {
            all = box;
        } else {
            const qreal left = qMin(all.left(), box.left()), top = qMin(all.top(), box.top());
            all = QRectF(left, top, qMax(all.right(), box.right()) - left, qMax(all.bottom(), box.bottom()) - top);
        }
        first = false;
    }
    return all;
}

bool Backend::resizable() const {
    // No handles while a path is being shaped or text is being typed: they
    // would sit on top of the very thing being worked on.
    if (m_selection.isEmpty() || !m_nodeEditId.isEmpty() || !m_penId.isEmpty() || !m_editingId.isEmpty())
        return false;
    if (m_selection.size() == 1) {
        // A lone line is reshaped by its two ends instead.
        const Item *item = m_doc.find(m_selection.first());
        return item && item->type != Item::Line;
    }
    return true;
}

QList<QPointF> Backend::handlePoints(const QRectF &b) {
    return {b.topLeft(), {b.center().x(), b.top()}, b.topRight(), {b.right(), b.center().y()},
            b.bottomRight(), {b.center().x(), b.bottom()}, b.bottomLeft(), {b.left(), b.center().y()}};
}

int Backend::handleAt(const QPointF &pos, qreal tolerance) const {
    if (!resizable())
        return -1;
    const QList<QPointF> points = handlePoints(selectionBounds());
    // Corners first, so they win over the edges on a small selection.
    for (int index : {0, 2, 4, 6, 1, 3, 5, 7})
        if (distance(points[index], pos) <= tolerance)
            return index;
    return -1;
}

QStringList Backend::unitIds(const Item &item) const {
    if (item.groups.isEmpty())
        return {item.id};
    QStringList ids;
    for (const Item &other : m_doc.items)
        if (!other.groups.isEmpty() && other.groups.last() == item.groups.last())
            ids << other.id;
    return ids;
}

// The selection as the things that move on their own: each group is one, and
// so is each loose item. A loose line held at an end by a shape goes where the
// shape goes, so it isn't something to align.
QList<QStringList> Backend::selectedUnits(bool movableOnly) const {
    const QSet<QString> selected = toSet(m_selection);
    QList<QStringList> units;
    QHash<QString, int> byGroup;
    for (const Item &item : m_doc.items) {
        if (!selected.contains(item.id))
            continue;
        if (item.groups.isEmpty()) {
            if (!(movableOnly && item.type == Item::Line && (!item.from.isEmpty() || !item.to.isEmpty())))
                units.append({item.id});
            continue;
        }
        const QString key = item.groups.last();
        if (!byGroup.contains(key)) {
            byGroup.insert(key, units.size());
            units.append(QStringList());
        }
        units[byGroup.value(key)].append(item.id);
    }
    return units;
}

void Backend::refresh() {
    QVariantMap s;
    const QSet<QString> selected = toSet(m_selection);
    const Item *any = nullptr, *fillable = nullptr, *rect = nullptr, *textual = nullptr, *line = nullptr;
    bool grouped = false;
    for (const Item &item : m_doc.items) {
        if (!selected.contains(item.id))
            continue;
        if (!any)
            any = &item;
        if (item.type != Item::Line && !fillable)
            fillable = &item;
        if ((item.type == Item::Rect || item.type == Item::Text) && !rect)
            rect = &item;
        if (item.type != Item::Path && !textual)
            textual = &item;
        if (item.type == Item::Line && !line)
            line = &item;
        grouped = grouped || !item.groups.isEmpty();
    }
    // With nothing selected the panel styles the next thing drawn.
    const bool none = !any;
    if (none)
        any = fillable = rect = textual = line = &m_defaults;

    s["count"] = int(m_selection.size());
    // What the one selected thing is, so the menu can offer to edit it.
    s["single"] = m_selection.size() != 1 || none ? "" : any->type == Item::Path ? "path"
                : any->type == Item::Line ? "line" : "shape";
    s["showFill"] = fillable != nullptr;
    s["showRadius"] = rect != nullptr;
    s["showText"] = textual != nullptr;
    s["showLine"] = line != nullptr;
    s["showArrows"] = line != nullptr && !none;
    s["fill"] = (fillable ? fillable : any)->fill;
    s["stroke"] = any->stroke;
    s["strokeWidth"] = any->strokeWidth;
    s["dash"] = any->dash;
    s["radius"] = (rect ? rect : any)->radius;
    s["opacity"] = any->opacity;
    const Item *t = textual ? textual : any;
    s["font"] = t->font;
    s["fontSize"] = t->fontSize;
    s["bold"] = t->bold;
    s["italic"] = t->italic;
    s["textColor"] = t->textColor;
    s["hAlign"] = t->hAlign;
    s["vAlign"] = t->vAlign;
    const Item *l = line ? line : any;
    s["arrowStart"] = l->arrowStart;
    s["arrowEnd"] = l->arrowEnd;
    s["routing"] = l->routing;

    const QRectF box = selectionBounds();
    s["x"] = box.x();
    s["y"] = box.y();
    s["w"] = box.width();
    s["h"] = box.height();

    const QList<QStringList> units = selectedUnits(true);
    s["units"] = int(units.size());
    s["canGroup"] = m_selection.size() >= 2;
    s["canUngroup"] = grouped;
    // The gap between neighbours, when it is the same all the way along.
    for (const bool horizontal : {true, false}) {
        if (units.size() < 2)
            break;
        QList<QRectF> boxes;
        for (const QStringList &unit : units)
            boxes << unitBounds(unit);
        std::sort(boxes.begin(), boxes.end(), [horizontal](const QRectF &a, const QRectF &b) {
            return horizontal ? a.left() < b.left() : a.top() < b.top();
        });
        auto gap = [&](int i) {
            return horizontal ? boxes[i + 1].left() - boxes[i].right() : boxes[i + 1].top() - boxes[i].bottom();
        };
        bool even = true;
        for (int i = 1; i + 1 < boxes.size(); ++i)
            even = even && qAbs(gap(i) - gap(0)) < 0.5;
        if (even)
            s[horizontal ? "gapH" : "gapV"] = gap(0);
    }

    m_style = s;
    emit styleChanged();
    emit changed();
}

// --- The pointer ---

void Backend::press(qreal x, qreal y, int modifiers, qreal zoom) {
    QPointF pos(x, y);
    m_zoom = qMax(zoom, 0.01);
    m_press = pos;
    m_dragOrigin = m_doc;
    m_dragOriginSelection = m_selection;
    m_dragChanged = false;
    m_drag = Drag::None;
    clearMarks();
    m_activeId.clear();
    m_attachId.clear();
    m_hoverId.clear();

    if (m_tool == "select") {
        pressSelect(pos, modifiers);
    } else if (m_tool == "pen") {
        pressPen(pos, modifiers);
    } else if (m_tool == "pencil") {
        setSelection({});
        m_drag = Drag::Pencil;
        m_pencil = {pos};
    } else if (m_tool == "line" || m_tool == "arrow") {
        m_drag = Drag::CreateLine;
        m_startSide = Item::Auto;
        m_startAttach = (modifiers & Qt::ControlModifier) ? QString() : attachTarget(pos, {}, &m_startSide);
        m_attachId = m_startAttach;
    } else {
        m_drag = Drag::CreateShape;
    }
    refresh();
}

void Backend::pressSelect(const QPointF &pos, int modifiers) {
    const bool shift = modifiers & Qt::ShiftModifier;
    const qreal tolerance = 7 / m_zoom;

    if (!m_nodeEditId.isEmpty()) {
        const Item *path = m_doc.find(m_nodeEditId);
        if (path && m_node >= 0 && m_node < path->nodes.size()) {
            const PathNode &node = path->nodes[m_node];
            for (int kind : {1, 2}) {
                const QPointF handle = kind == 1 ? node.in : node.out;
                if (handle.isNull() || distance(node.p + handle, pos) > tolerance)
                    continue;
                m_drag = Drag::Handle;
                m_handleKind = kind;
                // A smooth anchor keeps its two handles in one line.
                const qreal cross = node.in.x() * node.out.y() - node.in.y() * node.out.x();
                const qreal lengths = std::hypot(node.in.x(), node.in.y()) * std::hypot(node.out.x(), node.out.y());
                m_mirrorHandles = lengths > 0 && qAbs(cross) < 0.05 * lengths
                                  && QPointF::dotProduct(node.in, node.out) < 0;
                return;
            }
        }
        if (path) {
            for (int i = 0; i < path->nodes.size(); ++i) {
                if (distance(path->nodes[i].p, pos) > tolerance)
                    continue;
                m_node = i;
                m_drag = Drag::Node;
                return;
            }
            if (m_doc.hit(*path, pos, tolerance)) {
                m_node = -1;
                return;
            }
        }
        leaveNodeEdit();
    }

    if (m_selection.size() == 1) {
        const Item *line = m_doc.find(m_selection.first());
        if (line && line->type == Item::Line) {
            const Route route = m_doc.route(*line);
            const bool start = distance(route.start, pos) <= tolerance, end = distance(route.end, pos) <= tolerance;
            if (start || end) {
                m_drag = Drag::LineEnd;
                m_activeId = line->id;
                m_lineStart = start && !(end && distance(route.end, pos) < distance(route.start, pos));
                return;
            }
        }
    }

    const int handle = handleAt(pos, tolerance);
    if (handle >= 0) {
        m_drag = Drag::Resize;
        m_handle = handle;
        m_originBounds = selectionBounds();
        return;
    }

    const int index = m_doc.hitTest(pos, 5 / m_zoom);
    if (index < 0) {
        if (!shift)
            setSelection({});
        m_drag = Drag::Marquee;
        m_marqueeBase = m_selection;
        m_marquee = QRectF(pos, QSizeF(0, 0));
        return;
    }

    // A press takes the whole group. What Ctrl means isn't known yet: let
    // go without moving and it reaches the one item inside the group; drag
    // and it carries off a copy.
    const Item &item = m_doc.items[index];
    const QStringList unit = unitIds(item);
    m_clickedId = item.id;
    const QSet<QString> selected = toSet(m_selection);
    bool already = true;
    for (const QString &id : unit)
        already = already && selected.contains(id);

    m_clickedUnit = unit;
    m_clickedWasSelected = already;
    if (shift) {
        QStringList ids = m_selection;
        for (const QString &id : unit) {
            if (already)
                ids.removeAll(id);
            else if (!ids.contains(id))
                ids << id;
        }
        setSelection(ids);
        if (already)
            return;
    } else if (!already) {
        setSelection(unit);
    }
    m_drag = Drag::PendingMove;
    m_originBounds = selectionBounds();
}

void Backend::move(qreal x, qreal y, int modifiers, qreal zoom) {
    QPointF pos(x, y);
    m_zoom = qMax(zoom, 0.01);
    const bool shift = modifiers & Qt::ShiftModifier, control = modifiers & Qt::ControlModifier;
    const bool moved = distance(pos, m_press) > 3 / m_zoom;

    switch (m_drag) {
    case Drag::None:
        return;
    case Drag::PendingMove:
        if (!moved)
            return;
        m_drag = Drag::Move;
        if (control) {
            // Ctrl-drag leaves the original where it is and carries off a
            // copy. The copy is what moves from here on.
            beginChange();
            m_selection = addCopies(clones(m_selection), QPointF(), false);
            m_dragOrigin = m_doc;
        }
        [[fallthrough]];
    case Drag::Move: {
        QPointF delta = pos - m_press;
        if (shift) {
            if (qAbs(delta.x()) > qAbs(delta.y()))
                delta.setY(0);
            else
                delta.setX(0);
        }
        clearMarks();
        // Alt moves freely, past everything a move would otherwise catch on.
        if (!(modifiers & Qt::AltModifier))
            delta += snap(m_originBounds.translated(delta));
        measure(m_originBounds.translated(delta));
        beginChange();
        const RectMap map{m_originBounds, m_originBounds.translated(delta)};
        for (const QString &id : m_selection) {
            const Item *origin = m_dragOrigin.find(id);
            Item *item = m_doc.find(id);
            if (!origin || !item)
                continue;
            *item = *origin;
            transformItem(*item, map);
        }
        break;
    }
    case Drag::Resize: {
        const QRectF b = m_originBounds;
        qreal left = b.left(), top = b.top(), right = b.right(), bottom = b.bottom();
        const int h = m_handle;
        const bool west = h == 0 || h == 6 || h == 7, east = h >= 2 && h <= 4, north = h <= 2,
                   south = h >= 4 && h <= 6;
        if (west) left = pos.x();
        if (east) right = pos.x();
        if (north) top = pos.y();
        if (south) bottom = pos.y();
        // The edges being dragged catch on the edges and middles of what's
        // around, as a move does. Alt resizes freely; Shift is busy keeping
        // the proportions, which a caught edge would break.
        clearMarks();
        QRectF caughtX, caughtY;
        const bool catching = !shift && !(modifiers & Qt::AltModifier);
        const bool snappedX = catching && (west || east) && snapEdge(true, west ? left : right, &caughtX);
        const bool snappedY = catching && (north || south) && snapEdge(false, north ? top : bottom, &caughtY);
        if (shift && b.width() > 0 && b.height() > 0) {
            // Shift keeps the proportions: a corner scales both ways, an
            // edge grows the other dimension around its middle.
            if ((west || east) && (north || south)) {
                const qreal scale = qMax(qAbs(right - left) / b.width(), qAbs(bottom - top) / b.height());
                const qreal width = (right < left ? -1 : 1) * b.width() * scale;
                const qreal height = (bottom < top ? -1 : 1) * b.height() * scale;
                if (west) left = right - width; else right = left + width;
                if (north) top = bottom - height; else bottom = top + height;
            } else if (west || east) {
                const qreal height = b.height() * qAbs(right - left) / b.width();
                top = b.center().y() - height / 2;
                bottom = top + height;
            } else {
                const qreal width = b.width() * qAbs(bottom - top) / b.height();
                left = b.center().x() - width / 2;
                right = left + width;
            }
        }
        // Never down to nothing: there'd be no way to drag it back out.
        if (b.width() > 0 && qAbs(right - left) < 1) {
            if (west) left = right - 1; else right = left + 1;
        }
        if (b.height() > 0 && qAbs(bottom - top) < 1) {
            if (north) top = bottom - 1; else bottom = top + 1;
        }
        const QRectF resized = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
        if (snappedX) {
            const qreal x = west ? left : right;
            m_guides << QLineF(x, qMin(resized.top(), caughtX.top()), x, qMax(resized.bottom(), caughtX.bottom()));
        }
        if (snappedY) {
            const qreal y = north ? top : bottom;
            m_guides << QLineF(qMin(resized.left(), caughtY.left()), y, qMax(resized.right(), caughtY.right()), y);
        }
        measure(resized);
        beginChange();
        const RectMap map{b, QRectF(QPointF(left, top), QPointF(right, bottom))};
        for (const QString &id : m_selection) {
            const Item *origin = m_dragOrigin.find(id);
            Item *item = m_doc.find(id);
            if (!origin || !item)
                continue;
            *item = *origin;
            transformItem(*item, map);
            item->autoSize = false;
        }
        break;
    }
    case Drag::Marquee: {
        m_marquee = QRectF(m_press, pos).normalized();
        QStringList ids = m_marqueeBase;
        for (const Item &item : m_doc.items) {
            if (!overlaps(m_doc.bounds(item), m_marquee))
                continue;
            for (const QString &id : unitIds(item))
                if (!ids.contains(id))
                    ids << id;
        }
        m_selection = ids;
        break;
    }
    case Drag::CreateShape: {
        if (!m_dragChanged) {
            if (!moved)
                return;
            beginChange();
            Item item = newItem(toolType(m_tool));
            m_activeId = item.id;
            m_doc.items.append(item);
        }
        Item *item = m_doc.find(m_activeId);
        if (!item)
            return;
        QPointF size = pos - m_press;
        if (shift) {
            const qreal side = qMax(qAbs(size.x()), qAbs(size.y()));
            size = {size.x() < 0 ? -side : side, size.y() < 0 ? -side : side};
        }
        item->rect = QRectF(m_press, m_press + size).normalized();
        break;
    }
    case Drag::CreateLine:
        if (!m_dragChanged) {
            if (!moved)
                return;
            beginChange();
            Item line = newItem(Item::Line);
            line.arrowEnd = m_tool == "arrow";
            line.p1 = line.p2 = m_press;
            line.from = m_startAttach;
            line.fromSide = m_startSide;
            m_activeId = line.id;
            m_lineStart = false;
            m_doc.items.append(line);
        }
        [[fallthrough]];
    case Drag::LineEnd: {
        beginChange();
        Item *line = m_doc.find(m_activeId);
        if (!line)
            return;
        const Route route = m_doc.route(*line);
        const QString other = m_lineStart ? line->to : line->from;
        int side = Item::Auto;
        // Ctrl leaves the end loose where it would otherwise catch a shape.
        const QString target = control ? QString() : attachTarget(pos, other, &side);
        if (target.isEmpty() && shift)
            pos = constrainAngle(m_lineStart ? route.end : route.start, pos);
        (m_lineStart ? line->p1 : line->p2) = pos;
        (m_lineStart ? line->from : line->to) = target;
        (m_lineStart ? line->fromSide : line->toSide) = side;
        m_attachId = target;
        break;
    }
    case Drag::Node: {
        const Item *origin = m_dragOrigin.find(m_nodeEditId);
        if (!origin || m_node < 0 || m_node >= origin->nodes.size() || !moved)
            return;
        beginChange();
        if (Item *path = m_doc.find(m_nodeEditId))
            path->nodes[m_node].p = origin->nodes[m_node].p + (pos - m_press);
        break;
    }
    case Drag::Handle: {
        const Item *origin = m_dragOrigin.find(m_nodeEditId);
        if (!origin || m_node < 0 || m_node >= origin->nodes.size())
            return;
        beginChange();
        Item *path = m_doc.find(m_nodeEditId);
        if (!path)
            return;
        PathNode &node = path->nodes[m_node];
        const QPointF handle = pos - node.p;
        QPointF &dragged = m_handleKind == 1 ? node.in : node.out;
        QPointF &opposite = m_handleKind == 1 ? node.out : node.in;
        dragged = handle;
        // Alt breaks the pair, turning a smooth anchor into a cusp.
        const qreal length = std::hypot(handle.x(), handle.y());
        if (m_mirrorHandles && !(modifiers & Qt::AltModifier) && length > 0)
            opposite = -handle / length * std::hypot(opposite.x(), opposite.y());
        break;
    }
    case Drag::PenHandle: {
        Item *path = m_doc.find(m_penId);
        if (!path || path->nodes.isEmpty() || !moved)
            return;
        // Dragging as a point goes down pulls its curve handles out.
        PathNode &node = path->nodes.last();
        node.out = pos - node.p;
        node.in = -node.out;
        break;
    }
    case Drag::Pencil:
        if (m_pencil.isEmpty() || distance(m_pencil.last(), pos) > 1 / m_zoom)
            m_pencil << pos;
        emit changed();
        return;
    }
    refresh();
}

void Backend::release(qreal x, qreal y, int modifiers, qreal zoom) {
    const QPointF pos(x, y);
    m_zoom = qMax(zoom, 0.01);
    const Drag drag = m_drag;
    m_drag = Drag::None;
    clearMarks();
    m_marquee = QRectF();
    m_attachId.clear();

    switch (drag) {
    case Drag::PendingMove:
        // A click that never became a drag. With Ctrl it reaches the one
        // item inside a group; plain, on one of several selected things, it
        // narrows to that one.
        if (modifiers & Qt::ShiftModifier)
            break;
        if (modifiers & Qt::ControlModifier)
            setSelection({m_clickedId});
        else if (m_clickedWasSelected && m_selection.size() > m_clickedUnit.size())
            setSelection(m_clickedUnit);
        break;
    case Drag::CreateShape:
        finishCreateShape(pos);
        return;
    case Drag::CreateLine:
        if (m_dragChanged) {
            setSelection({m_activeId});
            setTool("select");
        }
        break;
    case Drag::Pencil:
        finishPencil();
        break;
    default:
        break;
    }
    refresh();
}

void Backend::finishCreateShape(const QPointF &pos) {
    const Item::Type type = toolType(m_tool);
    int depth = m_undo.size() - 1;
    if (m_dragChanged) {
        // A drag too small to be a size on purpose counts as a click.
        const Item *item = m_doc.find(m_activeId);
        if (item && item->rect.width() < 4 && item->rect.height() < 4) {
            m_doc = m_dragOrigin;
            m_undo.removeLast();
            m_dragChanged = false;
        }
    }
    if (!m_dragChanged) {
        depth = m_undo.size();
        checkpoint();
        Item item = newItem(type);
        if (type == Item::Text) {
            // Typed text starts on the pointer and grows with what's typed.
            item.autoSize = true;
            fitText(item);
            item.rect.moveTopLeft(pos - QPointF(4, item.rect.height() / 2));
        } else {
            item.rect = QRectF(0, 0, type == Item::Rect ? 160 : 140, type == Item::Triangle ? 120 : 100);
            item.rect.moveCenter(pos);
        }
        m_activeId = item.id;
        m_doc.items.append(item);
    }
    const QString id = m_activeId;
    setSelection({id});
    setTool("select");
    if (type == Item::Text) {
        beginEditing(id);
        m_newItemUndoDepth = depth;
    }
    refresh();
}

void Backend::doubleClick(qreal x, qreal y, int, qreal zoom) {
    const QPointF pos(x, y);
    m_zoom = qMax(zoom, 0.01);
    // The second press of the pair started a drag of its own; drop it.
    m_drag = Drag::None;
    m_marquee = QRectF();
    const qreal tolerance = 7 / m_zoom;

    if (m_tool == "pen") {
        // That second press also put down a point on top of the last one.
        Item *path = m_doc.find(m_penId);
        if (path && path->nodes.size() >= 2
            && distance(path->nodes.last().p, path->nodes[path->nodes.size() - 2].p) < tolerance)
            path->nodes.removeLast();
        finishPen();
        return;
    }
    if (m_tool != "select")
        return;

    if (!m_nodeEditId.isEmpty()) {
        Item *path = m_doc.find(m_nodeEditId);
        if (path) {
            for (int i = 0; i < path->nodes.size(); ++i) {
                if (distance(path->nodes[i].p, pos) > tolerance)
                    continue;
                // Double-clicking an anchor flips it between corner and curve.
                checkpoint();
                path = m_doc.find(m_nodeEditId);
                PathNode &node = path->nodes[i];
                if (!node.in.isNull() || !node.out.isNull()) {
                    node.in = node.out = QPointF();
                } else {
                    const int count = path->nodes.size();
                    const bool hasBefore = i > 0 || path->closed, hasAfter = i < count - 1 || path->closed;
                    const QPointF before = path->nodes[(i - 1 + count) % count].p, after = path->nodes[(i + 1) % count].p;
                    if (hasBefore && hasAfter) {
                        node.out = (after - before) / 6;
                        node.in = -node.out;
                    } else if (hasAfter) {
                        node.out = (after - node.p) / 3;
                    } else if (hasBefore) {
                        node.in = (before - node.p) / 3;
                    }
                }
                m_node = i;
                refresh();
                return;
            }
            if (m_doc.hit(*path, pos, tolerance)) {
                insertNode(*path, pos);
                refresh();
                return;
            }
        }
        leaveNodeEdit();
    }

    const int index = m_doc.hitTest(pos, 5 / m_zoom);
    if (index < 0) {
        // Double-clicking bare board starts a line of text there.
        const QString tool = m_tool;
        m_tool = "text";
        m_dragChanged = false;
        finishCreateShape(pos);
        if (tool != "select")
            setTool(tool);
        return;
    }
    const Item item = m_doc.items[index];
    setSelection({item.id});
    if (item.type == Item::Path) {
        m_nodeEditId = item.id;
        m_node = -1;
        emit modeChanged();
        refresh();
    } else {
        beginEditing(item.id);
        refresh();
    }
}

void Backend::hover(qreal x, qreal y, qreal zoom) {
    const QPointF pos(x, y);
    m_zoom = qMax(zoom, 0.01);
    if (!m_penId.isEmpty()) {
        m_penPreview = pos;
        m_penHasPreview = true;
        emit changed();
        return;
    }
    if (m_tool != "select") {
        setCursor(m_tool == "text" ? Qt::IBeamCursor : Qt::CrossCursor);
        return;
    }
    const qreal tolerance = 7 / m_zoom;
    int cursor = Qt::ArrowCursor;
    QString hoverId;
    bool onControl = false;
    if (!m_nodeEditId.isEmpty()) {
        if (const Item *path = m_doc.find(m_nodeEditId))
            for (const PathNode &node : path->nodes)
                onControl = onControl || distance(node.p, pos) <= tolerance;
        if (onControl)
            cursor = Qt::PointingHandCursor;
    } else {
        const int handle = handleAt(pos, tolerance);
        if (handle >= 0) {
            static const int cursors[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
            cursor = cursors[handle % 4];
            onControl = true;
        } else if (m_selection.size() == 1) {
            const Item *line = m_doc.find(m_selection.first());
            if (line && line->type == Item::Line) {
                const Route route = m_doc.route(*line);
                if (distance(route.start, pos) <= tolerance || distance(route.end, pos) <= tolerance) {
                    cursor = Qt::PointingHandCursor;
                    onControl = true;
                }
            }
        }
        if (!onControl) {
            const int index = m_doc.hitTest(pos, 5 / m_zoom);
            if (index >= 0)
                hoverId = m_doc.items[index].id;
        }
    }
    setCursor(cursor);
    if (hoverId != m_hoverId) {
        m_hoverId = hoverId;
        emit changed();
    }
}

void Backend::contextAt(qreal x, qreal y, qreal zoom) {
    m_zoom = qMax(zoom, 0.01);
    if (!m_penId.isEmpty())
        finishPen();
    m_drag = Drag::None;
    const int index = m_doc.hitTest(QPointF(x, y), 5 / m_zoom);
    if (index < 0) {
        setSelection({});
    } else if (!m_selection.contains(m_doc.items[index].id)) {
        setSelection(unitIds(m_doc.items[index]));
    }
    m_hoverId.clear();
    refresh();
}

void Backend::escape() {
    if (!m_penId.isEmpty()) {
        m_drag = Drag::None;
        finishPen();
        return;
    }
    if (m_drag != Drag::None) {
        // Escape mid-drag puts everything back where it was picked up.
        if (m_dragChanged) {
            // The undo step holds the board as it was at the press, which
            // for a Ctrl-drag is the board before the copy was made.
            m_doc = m_undo.takeLast().doc;
            emit historyChanged();
        }
        m_selection = m_dragOriginSelection;
        m_drag = Drag::None;
        m_dragChanged = false;
        clearMarks();
        m_marquee = QRectF();
        m_attachId.clear();
        m_pencil.clear();
        refresh();
        return;
    }
    if (!m_nodeEditId.isEmpty()) {
        leaveNodeEdit();
        refresh();
        return;
    }
    if (m_tool != "select") {
        setTool("select");
        return;
    }
    if (!m_selection.isEmpty()) {
        setSelection({});
        refresh();
    }
}

void Backend::enter() {
    if (!m_penId.isEmpty()) {
        finishPen();
        return;
    }
    if (!m_nodeEditId.isEmpty()) {
        leaveNodeEdit();
        refresh();
        return;
    }
    if (m_selection.size() != 1)
        return;
    const Item *item = m_doc.find(m_selection.first());
    if (!item)
        return;
    if (item->type == Item::Path) {
        m_nodeEditId = item->id;
        m_node = -1;
        emit modeChanged();
        refresh();
    } else {
        beginEditing(item->id);
    }
}

// --- Drawing ---

Item Backend::newItem(Item::Type type) const {
    Item item = m_defaults;
    item.type = type;
    item.id = m_doc.newId();
    item.arrowStart = item.arrowEnd = false;
    if (type == Item::Text) {
        item.fill = "none";
        item.stroke = "none";
        item.hAlign = 0;
        item.vAlign = 0;
        item.radius = 0;
    }
    if (type != Item::Rect)
        item.radius = 0;
    // Line work is never invisible, whatever the next box is set to.
    if ((type == Item::Line || type == Item::Path) && (item.stroke == "none" || item.strokeWidth <= 0)) {
        item.stroke = "ink";
        item.strokeWidth = qMax(item.strokeWidth, 2.0);
    }
    return item;
}

void Backend::fitText(Item &item) const {
    const QSizeF size = Renderer::textSize(item) + QSizeF(8, 8);
    QRectF rect(item.rect.topLeft(), size);
    if (item.hAlign == 1)
        rect.moveLeft(item.rect.center().x() - size.width() / 2);
    else if (item.hAlign == 2)
        rect.moveLeft(item.rect.right() - size.width());
    item.rect = rect;
}

QString Backend::attachTarget(const QPointF &pos, const QString &exclude, int *side) const {
    for (int i = m_doc.items.size() - 1; i >= 0; --i) {
        const Item &item = m_doc.items[i];
        if (!item.isShape() || item.id == exclude || !m_doc.hit(item, pos, 6 / m_zoom))
            continue;
        // Dropped near the middle of a side, the end stays on that side;
        // anywhere else it floats to wherever faces the other end.
        *side = Item::Auto;
        qreal nearest = 12 / m_zoom;
        for (int candidate = Item::Top; candidate <= Item::Left; ++candidate) {
            const qreal d = distance(m_doc.sidePoint(item, candidate), pos);
            if (d < nearest) {
                nearest = d;
                *side = candidate;
            }
        }
        return item.id;
    }
    return {};
}

void Backend::clearMarks() {
    m_guides.clear();
    m_spacers.clear();
    m_distances.clear();
}

// Everything a move or a resize can catch on and be measured against: what
// isn't selected, lines aside, since a line has no edges to line up with.
QList<QRectF> Backend::neighbours() const {
    const QSet<QString> selected = toSet(m_selection);
    QList<QRectF> boxes;
    for (const Item &item : m_doc.items)
        if (!selected.contains(item.id) && item.type != Item::Line)
            boxes << m_doc.bounds(item);
    return boxes;
}

// Pulls one dragged edge onto the nearest edge or middle of a neighbour, if
// one is within reach, and says which neighbour it was.
bool Backend::snapEdge(bool horizontal, qreal &edge, QRectF *target) const {
    qreal best = 6 / m_zoom, caught = edge;
    bool found = false;
    for (const QRectF &box : neighbours()) {
        const qreal lo = horizontal ? box.left() : box.top(), hi = horizontal ? box.right() : box.bottom();
        for (qreal candidate : {lo, (lo + hi) / 2, hi}) {
            if (qAbs(candidate - edge) >= best)
                continue;
            best = qAbs(candidate - edge);
            caught = candidate;
            *target = box;
            found = true;
        }
    }
    edge = caught;
    return found;
}

// Measures from a box to the nearest neighbour on each of its four sides,
// among the ones it sits alongside, when that's close enough to be worth
// knowing. A gap the snapping has already marked isn't measured twice.
void Backend::measure(const QRectF &box) {
    m_distances.clear();
    const qreal reach = 160 / m_zoom;
    const QList<QRectF> others = neighbours();
    for (const bool horizontal : {true, false}) {
        auto lo = [horizontal](const QRectF &r) { return horizontal ? r.left() : r.top(); };
        auto hi = [horizontal](const QRectF &r) { return horizontal ? r.right() : r.bottom(); };
        auto crossLo = [horizontal](const QRectF &r) { return horizontal ? r.top() : r.left(); };
        auto crossHi = [horizontal](const QRectF &r) { return horizontal ? r.bottom() : r.right(); };
        auto span = [&](const QRectF &before, const QRectF &after) {
            const qreal c = (qMax(crossLo(before), crossLo(after)) + qMin(crossHi(before), crossHi(after))) / 2;
            return horizontal ? QLineF(hi(before), c, lo(after), c) : QLineF(c, hi(before), c, lo(after));
        };
        const QRectF *before = nullptr, *after = nullptr;
        for (const QRectF &other : others) {
            if (crossHi(other) <= crossLo(box) || crossLo(other) >= crossHi(box))
                continue;
            if (hi(other) <= lo(box) + 0.01 && (!before || hi(other) > hi(*before)))
                before = &other;
            if (lo(other) >= hi(box) - 0.01 && (!after || lo(other) < lo(*after)))
                after = &other;
        }
        QList<QLineF> found;
        if (before)
            found << span(*before, box);
        if (after)
            found << span(box, *after);
        for (const QLineF &line : std::as_const(found)) {
            if (line.length() < 0.5 || line.length() > reach)
                continue;
            bool marked = false;
            for (const QLineF &spacer : std::as_const(m_spacers))
                marked = marked || (QLineF(spacer.p1(), line.p1()).length() < 0.5
                                    && QLineF(spacer.p2(), line.p2()).length() < 0.5);
            if (!marked)
                m_distances << line;
        }
    }
}

// Lines the moving box up with what's around it, and leaves a mark wherever
// it caught. Two things can catch it along each direction: an edge or middle
// of a neighbour, or a gap. If two things in its row already sit some
// distance apart, it catches at that same distance from any of them, and
// halfway between two of them. Whichever is nearest wins.
QPointF Backend::snap(const QRectF &moving) {
    const qreal threshold = 6 / m_zoom;
    const QList<QRectF> others = neighbours();

    QPointF result;
    QList<QLineF> guides, spacers;
    for (const bool horizontal : {true, false}) {
        auto lo = [horizontal](const QRectF &r) { return horizontal ? r.left() : r.top(); };
        auto hi = [horizontal](const QRectF &r) { return horizontal ? r.right() : r.bottom(); };
        auto crossLo = [horizontal](const QRectF &r) { return horizontal ? r.top() : r.left(); };
        auto crossHi = [horizontal](const QRectF &r) { return horizontal ? r.bottom() : r.right(); };
        auto shifted = [horizontal, &moving](qreal d) {
            return horizontal ? moving.translated(d, 0) : moving.translated(0, d);
        };

        // Edges and middles.
        qreal best = threshold, delta = 0, line = 0;
        QRectF target;
        const qreal mine[] = {lo(moving), (lo(moving) + hi(moving)) / 2, hi(moving)};
        for (const QRectF &box : std::as_const(others)) {
            const qreal theirs[] = {lo(box), (lo(box) + hi(box)) / 2, hi(box)};
            for (qreal a : mine)
                for (qreal b : theirs)
                    if (qAbs(b - a) < best) {
                        best = qAbs(b - a);
                        delta = b - a;
                        line = b;
                        target = box;
                    }
        }
        bool aligned = best < threshold;

        // Gaps: only among the things this one is passing alongside.
        QList<QRectF> row;
        for (const QRectF &box : std::as_const(others))
            if (crossHi(box) > crossLo(moving) && crossLo(box) < crossHi(moving))
                row << box;
        std::sort(row.begin(), row.end(), [&lo](const QRectF &a, const QRectF &b) { return lo(a) < lo(b); });
        // The span of a gap, drawn through the middle of where the two overlap.
        auto span = [&](const QRectF &before, const QRectF &after) {
            const qreal c = (qMax(crossLo(before), crossLo(after)) + qMin(crossHi(before), crossHi(after))) / 2;
            return horizontal ? QLineF(hi(before), c, lo(after), c) : QLineF(c, hi(before), c, lo(after));
        };
        QList<QLineF> marks;
        auto consider = [&](qreal d, const std::function<QList<QLineF>(const QRectF &)> &make) {
            if (qAbs(d) >= best)
                return;
            best = qAbs(d);
            delta = d;
            aligned = false;
            marks = make(shifted(d));
        };
        for (int i = 0; i + 1 < row.size(); ++i) {
            const QRectF p = row[i], q = row[i + 1];
            const qreal gap = lo(q) - hi(p);
            if (gap < 0)
                continue;
            const qreal size = hi(moving) - lo(moving);
            if (gap > size)
                consider(hi(p) + (gap - size) / 2 - lo(moving),
                         [&, p, q](const QRectF &m) { return QList<QLineF>{span(p, m), span(m, q)}; });
            for (const QRectF &anchor : std::as_const(row)) {
                consider(hi(anchor) + gap - lo(moving), [&, p, q, anchor](const QRectF &m) {
                    return QList<QLineF>{span(p, q), span(anchor, m)};
                });
                consider(lo(anchor) - gap - hi(moving), [&, p, q, anchor](const QRectF &m) {
                    return QList<QLineF>{span(m, anchor), span(p, q)};
                });
            }
        }

        if (best >= threshold)
            continue;
        (horizontal ? result.rx() : result.ry()) = delta;
        if (aligned) {
            const QRectF moved = shifted(delta);
            const qreal from = qMin(crossLo(moved), crossLo(target)), to = qMax(crossHi(moved), crossHi(target));
            guides << (horizontal ? QLineF(line, from, line, to) : QLineF(from, line, to, line));
        } else {
            spacers << marks;
        }
    }
    m_guides = guides;
    m_spacers = spacers;
    return result;
}

void Backend::pressPen(QPointF pos, int modifiers) {
    Item *path = m_doc.find(m_penId);
    if (!path) {
        // The whole path is one undo step, however many points it gets.
        checkpoint();
        setSelection({});
        Item item = newItem(Item::Path);
        item.fill = "none";
        item.nodes = {PathNode{pos, {}, {}}};
        m_penId = item.id;
        m_doc.items.append(item);
        emit modeChanged();
    } else {
        // Back on the first point closes the shape.
        if (path->nodes.size() >= 2 && distance(pos, path->nodes.first().p) <= 8 / m_zoom) {
            path->closed = true;
            path->fill = m_defaults.fill;
            finishPen();
            return;
        }
        if (modifiers & Qt::ShiftModifier)
            pos = constrainAngle(path->nodes.last().p, pos);
        path->nodes.append(PathNode{pos, {}, {}});
    }
    m_penHasPreview = false;
    m_drag = Drag::PenHandle;
}

void Backend::finishPen() {
    const QString id = m_penId;
    m_penId.clear();
    m_penHasPreview = false;
    m_drag = Drag::None;
    if (id.isEmpty())
        return;
    const Item *path = m_doc.find(id);
    if (!path || path->nodes.size() < 2) {
        // One point isn't a path; take it, and its undo step, away.
        const int index = m_doc.indexOf(id);
        if (index >= 0)
            m_doc.items.removeAt(index);
        if (!m_undo.isEmpty())
            m_undo.removeLast();
        emit historyChanged();
    } else {
        setSelection({id});
    }
    emit modeChanged();
    setTool("select");
    refresh();
}

void Backend::finishPencil() {
    QList<QPointF> raw = m_pencil;
    m_pencil.clear();
    if (raw.size() < 2)
        return;
    QList<QPointF> points{raw.first()};
    simplifyInto(raw, 0, raw.size() - 1, 1.5 / m_zoom, points);

    qreal length = 0;
    for (int i = 1; i < points.size(); ++i)
        length += distance(points[i - 1], points[i]);
    if (length < 2 / m_zoom)
        return;
    // A stroke that comes back to where it started is a closed loop.
    const bool closed = points.size() > 3 && length > 60 / m_zoom
                        && distance(points.first(), points.last()) < 12 / m_zoom;
    if (closed)
        points.removeLast();

    checkpoint();
    Item item = newItem(Item::Path);
    item.fill = "none";
    item.closed = closed;
    const int count = points.size();
    for (int i = 0; i < count; ++i) {
        PathNode node{points[i], {}, {}};
        const bool inner = closed || (i > 0 && i < count - 1);
        if (inner && count > 2) {
            // Catmull-Rom tangents turn the kept points into one smooth run.
            node.out = (points[(i + 1) % count] - points[(i - 1 + count) % count]) / 6;
            node.in = -node.out;
        }
        item.nodes << node;
    }
    m_doc.items.append(item);
}

void Backend::leaveNodeEdit() {
    if (m_nodeEditId.isEmpty())
        return;
    m_nodeEditId.clear();
    m_node = -1;
    emit modeChanged();
}

void Backend::deleteNode() {
    const QString id = m_nodeEditId;
    const Item *existing = m_doc.find(id);
    if (!existing)
        return;
    checkpoint();
    Item *path = m_doc.find(id);
    if (m_node >= 0 && m_node < path->nodes.size())
        path->nodes.removeAt(m_node);
    else
        path->nodes.clear();
    m_node = -1;
    if (path->nodes.size() < 2) {
        m_doc.items.removeAt(m_doc.indexOf(id));
        setSelection({});
    }
    refresh();
}

// Splits the segment under the pointer in two without changing its shape.
void Backend::insertNode(Item &target, const QPointF &pos) {
    const QString id = target.id;
    const int count = target.nodes.size();
    const int segments = target.closed ? count : count - 1;
    int bestSegment = -1;
    qreal bestT = 0, bestDistance = 1e18;
    auto pointAt = [](const PathNode &a, const PathNode &b, qreal t) {
        const QPointF p1 = a.p + a.out, p2 = b.p + b.in;
        const QPointF q0 = lerp(a.p, p1, t), q1 = lerp(p1, p2, t), q2 = lerp(p2, b.p, t);
        return lerp(lerp(q0, q1, t), lerp(q1, q2, t), t);
    };
    for (int s = 0; s < segments; ++s) {
        for (int step = 1; step < 40; ++step) {
            const qreal t = step / 40.0;
            const qreal d = distance(pointAt(target.nodes[s], target.nodes[(s + 1) % count], t), pos);
            if (d < bestDistance) {
                bestDistance = d;
                bestSegment = s;
                bestT = t;
            }
        }
    }
    if (bestSegment < 0)
        return;

    checkpoint();
    Item *path = m_doc.find(id);
    PathNode &a = path->nodes[bestSegment];
    PathNode &b = path->nodes[(bestSegment + 1) % count];
    PathNode added{pointAt(a, b, bestT), {}, {}};
    if (!a.out.isNull() || !b.in.isNull()) {
        const QPointF p1 = a.p + a.out, p2 = b.p + b.in;
        const QPointF q0 = lerp(a.p, p1, bestT), q1 = lerp(p1, p2, bestT), q2 = lerp(p2, b.p, bestT);
        const QPointF r0 = lerp(q0, q1, bestT), r1 = lerp(q1, q2, bestT);
        a.out = q0 - a.p;
        b.in = q2 - b.p;
        added.in = r0 - added.p;
        added.out = r1 - added.p;
    }
    path->nodes.insert(bestSegment + 1, added);
    m_node = bestSegment + 1;
}

// --- Text ---

void Backend::beginEditing(const QString &id) {
    const Item *item = m_doc.find(id);
    if (!item || item->type == Item::Path)
        return;
    m_newItemUndoDepth = -1;
    m_editingId = id;
    emit editingChanged();
    emit changed();
}

QVariantMap Backend::editing() const {
    QVariantMap e{{"mode", "box"}, {"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 1.0}, {"text", ""},
                  {"fontSize", 16}, {"bold", false}, {"italic", false}, {"family", "sans-serif"},
                  {"color", "#ffffff"}, {"paper", "#000000"}, {"hAlign", 1}, {"vAlign", 1}};
    const Item *item = m_doc.find(m_editingId);
    if (!item)
        return e;
    e["text"] = item->text;
    e["fontSize"] = item->fontSize;
    e["bold"] = item->bold;
    e["italic"] = item->italic;
    e["family"] = Renderer::family(item->font);
    e["color"] = Renderer::color(item->textColor, m_doc.paper).name(QColor::HexArgb);
    e["paper"] = Renderer::paperColor(m_doc.paper).name();
    e["hAlign"] = item->hAlign;
    e["vAlign"] = item->vAlign;
    if (item->type == Item::Line) {
        // A line's label is typed centered on the middle of the line.
        const QPointF middle = m_doc.route(*item).path.pointAtPercent(0.5);
        e["mode"] = "label";
        e["x"] = middle.x();
        e["y"] = middle.y();
        e["hAlign"] = 1;
        return e;
    }
    const QRectF box = Document::textRect(*item);
    if (item->autoSize) {
        e["mode"] = "auto";
        e["x"] = item->hAlign == 1 ? box.center().x() : item->hAlign == 2 ? box.right() : box.left();
        e["y"] = box.top();
    } else {
        e["x"] = box.x();
        e["y"] = box.y();
    }
    e["w"] = box.width();
    e["h"] = box.height();
    return e;
}

void Backend::commitText(const QString &text) {
    const QString id = m_editingId;
    const int depth = m_newItemUndoDepth;
    if (id.isEmpty())
        return;
    m_editingId.clear();
    m_newItemUndoDepth = -1;

    const Item *existing = m_doc.find(id);
    if (existing && existing->type == Item::Text && text.trimmed().isEmpty()) {
        // Text with nothing in it is nothing. If it was only just made, it
        // leaves no trace in the undo history either.
        if (depth >= 0) {
            while (m_undo.size() > depth)
                m_undo.removeLast();
            emit historyChanged();
        } else {
            checkpoint();
        }
        m_doc.items.removeAt(m_doc.indexOf(id));
        QStringList ids = m_selection;
        ids.removeAll(id);
        setSelection(ids);
    } else if (existing && existing->text != text) {
        if (depth < 0)
            checkpoint();
        Item *item = m_doc.find(id);
        item->text = text;
        if (item->autoSize)
            fitText(*item);
    }
    emit editingChanged();
    refresh();
}

// --- Editing commands ---

void Backend::translate(const QStringList &ids, const QPointF &delta) {
    const QRectF box(0, 0, 1, 1);
    const RectMap map{box, box.translated(delta)};
    for (const QString &id : ids)
        if (Item *item = m_doc.find(id))
            transformItem(*item, map);
}

void Backend::deleteSelection() {
    if (!m_nodeEditId.isEmpty()) {
        deleteNode();
        return;
    }
    if (m_selection.isEmpty())
        return;
    checkpoint();
    const QSet<QString> selected = toSet(m_selection);
    // A line that was holding on to something deleted keeps its place.
    for (int i = 0; i < m_doc.items.size(); ++i) {
        const Item &line = m_doc.items[i];
        if (line.type != Item::Line || selected.contains(line.id)
            || (!selected.contains(line.from) && !selected.contains(line.to)))
            continue;
        const Route route = m_doc.route(line);
        Item &loose = m_doc.items[i];
        if (selected.contains(loose.from)) {
            loose.p1 = route.start;
            loose.from.clear();
            loose.fromSide = Item::Auto;
        }
        if (selected.contains(loose.to)) {
            loose.p2 = route.end;
            loose.to.clear();
            loose.toSide = Item::Auto;
        }
    }
    m_doc.items.removeIf([&selected](const Item &item) { return selected.contains(item.id); });
    setSelection({});
    refresh();
}

// Copies of the given items, with any line end that pointed outside the set
// pinned to where it was.
QList<Item> Backend::clones(const QStringList &ids) const {
    const QSet<QString> picked = toSet(ids);
    QList<Item> result;
    for (const Item &item : m_doc.items) {
        if (!picked.contains(item.id))
            continue;
        Item copy = item;
        if (copy.type == Item::Line) {
            const Route route = m_doc.route(item);
            copy.p1 = route.start;
            copy.p2 = route.end;
            if (!picked.contains(copy.from)) {
                copy.from.clear();
                copy.fromSide = Item::Auto;
            }
            if (!picked.contains(copy.to)) {
                copy.to.clear();
                copy.toSide = Item::Auto;
            }
        }
        result << copy;
    }
    return result;
}

// Puts items on the board under fresh ids, either centered on a point or
// offset by it, and says what they're now called. No undo step of its own.
QStringList Backend::addCopies(QList<Item> items, const QPointF &point, bool centered) {
    if (items.isEmpty())
        return {};
    QHash<QString, QString> ids, groups;
    for (const Item &item : std::as_const(items)) {
        QString id;
        do {
            id = m_doc.newId();
        } while (ids.values().contains(id));
        ids.insert(item.id, id);
    }
    for (Item &item : items) {
        item.id = ids.value(item.id);
        item.from = ids.value(item.from);
        item.to = ids.value(item.to);
        for (QString &group : item.groups) {
            if (!groups.contains(group))
                groups.insert(group, "g" + m_doc.newId());
            group = groups.value(group);
        }
    }
    Document incoming;
    incoming.items = items;
    QPointF delta = point;
    if (centered) {
        QRectF all;
        bool first = true;
        for (const Item &item : std::as_const(incoming.items)) {
            const QRectF box = incoming.bounds(item);
            all = first ? box : all.united(box);
            first = false;
        }
        delta = point - all.center();
    }
    QStringList added;
    for (const Item &item : std::as_const(incoming.items)) {
        m_doc.items.append(item);
        added << item.id;
    }
    translate(added, delta);
    return added;
}

void Backend::insert(QList<Item> items, const QPointF &point, bool centered) {
    if (items.isEmpty())
        return;
    checkpoint();
    setSelection(addCopies(items, point, centered));
    refresh();
}

void Backend::duplicate() { insert(clones(m_selection), QPointF(16, 16), false); }

void Backend::copy() {
    if (m_selection.isEmpty())
        return;
    QJsonArray array;
    for (const Item &item : clones(m_selection))
        array.append(Document::itemToJson(item));
    const QJsonObject clip{{"chalk-clip", 1}, {"items", array}};
    QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(clip).toJson(QJsonDocument::Compact)));
}

void Backend::cut() {
    copy();
    deleteSelection();
}

void Backend::paste(qreal x, qreal y) {
    const QString text = QGuiApplication::clipboard()->text();
    if (text.trimmed().isEmpty())
        return;
    const QJsonObject clip = QJsonDocument::fromJson(text.toUtf8()).object();
    if (clip.contains("chalk-clip")) {
        QList<Item> items;
        for (const QJsonValue &value : clip.value("items").toArray()) {
            Item item;
            QString error;
            if (Document::itemFromJson(value.toObject(), &item, &error))
                items << item;
        }
        insert(items, QPointF(x, y), true);
        return;
    }
    // Anything else on the clipboard arrives as a piece of text.
    Item item = newItem(Item::Text);
    item.text = text.trimmed();
    item.autoSize = true;
    fitText(item);
    insert({item}, QPointF(x, y), true);
}

void Backend::selectAll() {
    QStringList ids;
    for (const Item &item : m_doc.items)
        ids << item.id;
    setSelection(ids);
    refresh();
}

void Backend::nudge(qreal dx, qreal dy) {
    if (!m_nodeEditId.isEmpty()) {
        const Item *existing = m_doc.find(m_nodeEditId);
        if (!existing || m_node < 0 || m_node >= existing->nodes.size())
            return;
        checkpointFor("nudge-node");
        m_doc.find(m_nodeEditId)->nodes[m_node].p += QPointF(dx, dy);
        refresh();
        return;
    }
    if (m_selection.isEmpty())
        return;
    checkpointFor("nudge");
    translate(m_selection, QPointF(dx, dy));
    refresh();
}

void Backend::group() {
    if (m_selection.size() < 2)
        return;
    checkpoint();
    const QString groupId = "g" + m_doc.newId();
    const QSet<QString> selected = toSet(m_selection);
    int topmost = -1;
    for (int i = 0; i < m_doc.items.size(); ++i)
        if (selected.contains(m_doc.items[i].id))
            topmost = i;
    // Grouped things also stack together, where the topmost of them was.
    QList<Item> rest, picked;
    int below = 0;
    for (int i = 0; i < m_doc.items.size(); ++i) {
        Item item = m_doc.items[i];
        if (selected.contains(item.id)) {
            item.groups.append(groupId);
            picked << item;
        } else {
            if (i < topmost)
                ++below;
            rest << item;
        }
    }
    m_doc.items = rest.mid(0, below) + picked + rest.mid(below);
    refresh();
}

void Backend::ungroup() {
    QSet<QString> groups;
    for (const QString &id : m_selection) {
        const Item *item = m_doc.find(id);
        if (item && !item->groups.isEmpty())
            groups.insert(item->groups.last());
    }
    if (groups.isEmpty())
        return;
    checkpoint();
    for (Item &item : m_doc.items)
        if (!item.groups.isEmpty() && groups.contains(item.groups.last()))
            item.groups.removeLast();
    refresh();
}

void Backend::order(const QString &direction) {
    if (m_selection.isEmpty())
        return;
    checkpoint();
    const QSet<QString> selected = toSet(m_selection);
    QList<Item> &items = m_doc.items;
    auto picked = [&](int i) { return selected.contains(items[i].id); };
    if (direction == "front" || direction == "back") {
        QList<Item> chosen, rest;
        for (const Item &item : std::as_const(items))
            (selected.contains(item.id) ? chosen : rest) << item;
        items = direction == "front" ? rest + chosen : chosen + rest;
    } else if (direction == "forward") {
        for (int i = items.size() - 2; i >= 0; --i)
            if (picked(i) && !picked(i + 1))
                items.swapItemsAt(i, i + 1);
    } else if (direction == "backward") {
        for (int i = 1; i < items.size(); ++i)
            if (picked(i) && !picked(i - 1))
                items.swapItemsAt(i, i - 1);
    }
    refresh();
}

void Backend::align(const QString &edge) {
    const QList<QStringList> units = selectedUnits(true);
    if (units.size() < 2)
        return;
    QStringList everything;
    for (const QStringList &unit : units)
        everything << unit;
    const QRectF all = unitBounds(everything);
    checkpoint();
    for (const QStringList &unit : units) {
        const QRectF box = unitBounds(unit);
        QPointF delta;
        if (edge == "left") delta.setX(all.left() - box.left());
        else if (edge == "hcenter") delta.setX(all.center().x() - box.center().x());
        else if (edge == "right") delta.setX(all.right() - box.right());
        else if (edge == "top") delta.setY(all.top() - box.top());
        else if (edge == "vcenter") delta.setY(all.center().y() - box.center().y());
        else if (edge == "bottom") delta.setY(all.bottom() - box.bottom());
        translate(unit, delta);
    }
    refresh();
}

void Backend::distribute(const QString &axis) {
    const bool horizontal = axis == "h";
    QList<QStringList> units = selectedUnits(true);
    if (units.size() < 3)
        return;
    auto start = [&](const QRectF &box) { return horizontal ? box.left() : box.top(); };
    auto size = [&](const QRectF &box) { return horizontal ? box.width() : box.height(); };
    std::sort(units.begin(), units.end(), [&](const QStringList &a, const QStringList &b) {
        return start(unitBounds(a)) < start(unitBounds(b));
    });
    qreal low = 1e18, high = -1e18, total = 0;
    for (const QStringList &unit : units) {
        const QRectF box = unitBounds(unit);
        low = qMin(low, start(box));
        high = qMax(high, start(box) + size(box));
        total += size(box);
    }
    const qreal gap = (high - low - total) / (units.size() - 1);
    checkpoint();
    qreal cursor = low;
    for (const QStringList &unit : units) {
        const QRectF box = unitBounds(unit);
        const qreal delta = cursor - start(box);
        translate(unit, horizontal ? QPointF(delta, 0) : QPointF(0, delta));
        cursor += size(box) + gap;
    }
    refresh();
}

void Backend::setGap(const QString &axis, qreal gap) {
    const bool horizontal = axis == "h";
    QList<QStringList> units = selectedUnits(true);
    if (units.size() < 2 || !std::isfinite(gap))
        return;
    auto start = [&](const QRectF &box) { return horizontal ? box.left() : box.top(); };
    auto size = [&](const QRectF &box) { return horizontal ? box.width() : box.height(); };
    std::sort(units.begin(), units.end(), [&](const QStringList &a, const QStringList &b) {
        return start(unitBounds(a)) < start(unitBounds(b));
    });
    checkpointFor("gap:" + axis);
    // The first one stays put and the rest line up after it.
    qreal cursor = start(unitBounds(units.first()));
    for (const QStringList &unit : units) {
        const QRectF box = unitBounds(unit);
        const qreal delta = cursor - start(box);
        translate(unit, horizontal ? QPointF(delta, 0) : QPointF(0, delta));
        cursor += size(box) + gap;
    }
    refresh();
}

void Backend::applyStyle(Item &item, const QString &key, const QVariant &value, bool force) const {
    const bool line = item.type == Item::Line;
    if (key == "fill") { if (force || !line) item.fill = value.toString(); }
    else if (key == "stroke") item.stroke = value.toString();
    else if (key == "strokeWidth") item.strokeWidth = qBound(0.0, value.toDouble(), 40.0);
    else if (key == "dash") item.dash = qBound(0, value.toInt(), 2);
    else if (key == "radius") { if (force || item.type == Item::Rect || item.type == Item::Text) item.radius = qMax(0.0, value.toDouble()); }
    else if (key == "opacity") item.opacity = qBound(0.05, value.toDouble(), 1.0);
    else if (key == "font") item.font = value.toString();
    else if (key == "fontSize") item.fontSize = qBound(6, value.toInt(), 400);
    else if (key == "bold") item.bold = value.toBool();
    else if (key == "italic") item.italic = value.toBool();
    else if (key == "textColor") item.textColor = value.toString();
    else if (key == "hAlign") item.hAlign = qBound(0, value.toInt(), 2);
    else if (key == "vAlign") item.vAlign = qBound(0, value.toInt(), 2);
    else if (key == "arrowStart") { if (force || line) item.arrowStart = value.toBool(); }
    else if (key == "arrowEnd") { if (force || line) item.arrowEnd = value.toBool(); }
    else if (key == "routing") { if (force || line) item.routing = qBound(0, value.toInt(), 2); }
    if (item.type == Item::Text && item.autoSize && !force)
        fitText(item);
}

void Backend::setStyle(const QString &key, const QVariant &value) {
    if (m_selection.isEmpty()) {
        applyStyle(m_defaults, key, value, true);
        refresh();
        return;
    }
    checkpointFor("style:" + key);
    for (const QString &id : std::as_const(m_selection))
        if (Item *item = m_doc.find(id))
            applyStyle(*item, key, value, false);
    if (!m_editingId.isEmpty())
        emit editingChanged();
    refresh();
}

void Backend::setGeometry(const QString &key, qreal value) {
    if (m_selection.isEmpty() || !std::isfinite(value))
        return;
    const QRectF from = selectionBounds();
    QRectF to = from;
    if (key == "x") to.moveLeft(value);
    else if (key == "y") to.moveTop(value);
    else if (key == "w") to.setWidth(qMax(1.0, value));
    else if (key == "h") to.setHeight(qMax(1.0, value));
    else return;
    checkpointFor("geometry:" + key);
    const RectMap map{from, to};
    for (const QString &id : std::as_const(m_selection)) {
        Item *item = m_doc.find(id);
        if (!item)
            continue;
        transformItem(*item, map);
        if (key == "w" || key == "h")
            item->autoSize = false;
    }
    refresh();
}

// --- Files ---

void Backend::newDocument() {
    m_doc = Document();
    m_doc.paper = m_defaultPaper;
    m_selection.clear();
    m_undo.clear();
    m_redo.clear();
    m_filePath.clear();
    m_editingId.clear();
    m_nodeEditId.clear();
    m_penId.clear();
    m_node = -1;
    m_drag = Drag::None;
    m_tool = "select";
    setDirty(false);
    emit toolChanged();
    emit historyChanged();
    emit fileChanged();
    emit editingChanged();
    emit modeChanged();
    refresh();
    emit loaded();
}

bool Backend::load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        say(tr("Could not open %1: %2").arg(path, file.errorString()), true);
        return false;
    }
    Document document;
    QString error;
    QStringList warnings;
    if (!Document::parse(file.readAll(), &document, &error, &warnings)) {
        say(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), error), true);
        return false;
    }
    newDocument();
    m_doc = document;
    m_filePath = QFileInfo(path).absoluteFilePath();
    emit fileChanged();
    refresh();
    say(warnings.value(0));
    emit loaded();
    return true;
}

bool Backend::saveTo(const QString &path) {
    QString target = path;
    if (QFileInfo(target).suffix().isEmpty())
        target += ".chalk";
    // Written beside the board and renamed over it: a save that fails
    // partway leaves the last good board untouched.
    QSaveFile file(target);
    const QByteArray data = m_doc.serialize();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        say(tr("Could not save %1: %2").arg(target, file.errorString()), true);
        emit saveAbandoned();
        return false;
    }
    m_filePath = QFileInfo(target).absoluteFilePath();
    setDirty(false);
    emit fileChanged();
    say(tr("Saved %1").arg(QFileInfo(target).fileName()));
    emit saved();
    return true;
}

void Backend::save() {
    if (m_filePath.isEmpty())
        chooseSave();
    else
        saveTo(m_filePath);
}

QString Backend::suggestedPath(const QString &extension) const {
    QString folder = m_filePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                          : QFileInfo(m_filePath).absolutePath();
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QDir::homePath();
    return QDir(folder).filePath(fileName() + "." + extension);
}

void Backend::chooseOpen() {
    if (!m_picker) {
        say(tr("No file picker is available."), true);
        return;
    }
    m_picker->openBoard(QFileInfo(suggestedPath("chalk")).absolutePath());
}

void Backend::chooseSave() {
    if (!m_picker) {
        say(tr("No file picker is available."), true);
        emit saveAbandoned();
        return;
    }
    m_picker->saveBoard(QUrl::fromLocalFile(suggestedPath("chalk")));
}

void Backend::chooseExport(const QVariantMap &options) {
    if (!m_picker) {
        say(tr("No file picker is available."), true);
        return;
    }
    if (m_doc.items.isEmpty()) {
        say(tr("There is nothing on the board to export."), true);
        return;
    }
    m_exportOptions = options;
    const QString format = options.value("format", "png").toString();
    m_picker->exportImage(QUrl::fromLocalFile(suggestedPath(format)), format);
}

bool Backend::exportTo(const QString &path, const QVariantMap &options) {
    Exporter::Options exportOptions;
    exportOptions.scale = options.value("scale", 2).toDouble();
    exportOptions.background = options.value("background", true).toBool();
    if (options.value("selectionOnly").toBool())
        exportOptions.only = m_selection;
    QString target = path;
    const QString format = options.value("format").toString();
    if (QFileInfo(target).suffix().isEmpty() && !format.isEmpty())
        target += "." + format;
    QString error;
    if (!Exporter::write(m_doc, target, exportOptions, &error)) {
        say(error, true);
        return false;
    }
    say(tr("Exported %1").arg(QFileInfo(target).fileName()));
    return true;
}

void Backend::copyImage() {
    Exporter::Options options;
    options.only = m_selection;
    const QImage image = Exporter::image(m_doc, options);
    if (image.isNull()) {
        say(tr("There is nothing on the board to export."), true);
        return;
    }
    QGuiApplication::clipboard()->setImage(image);
    say(tr("Copied as an image"));
}
