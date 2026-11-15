#include "document.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QRandomGenerator>
#include <QSet>
#include <cmath>

namespace {
const QStringList typeNames{"rect", "ellipse", "diamond", "triangle", "text", "line", "path"};
const QStringList dashNames{"solid", "dashed", "dotted"};
const QStringList routingNames{"straight", "elbow", "curved"};
const QStringList sideNames{"auto", "top", "right", "bottom", "left"};
const QStringList alignNames{"left", "center", "right"};
const QStringList valignNames{"top", "middle", "bottom"};

QString tr(const char *text) { return QCoreApplication::translate("Document", text); }

QPointF unit(const QPointF &v) {
    const qreal length = std::hypot(v.x(), v.y());
    return length > 1e-6 ? v / length : QPointF();
}

QPointF sideDirection(int side) {
    switch (side) {
    case Item::Top: return {0, -1};
    case Item::Right: return {1, 0};
    case Item::Bottom: return {0, 1};
    case Item::Left: return {-1, 0};
    }
    return {};
}

// The side of a box that a ray from its center toward a point leaves through.
int facingSide(const QRectF &rect, const QPointF &toward) {
    const QPointF d = toward - rect.center();
    const qreal w = qMax(rect.width(), 1.0), h = qMax(rect.height(), 1.0);
    if (qAbs(d.x()) * h >= qAbs(d.y()) * w)
        return d.x() >= 0 ? Item::Right : Item::Left;
    return d.y() >= 0 ? Item::Bottom : Item::Top;
}

// A polyline with its corners eased, so elbow lines don't look machined.
QPainterPath roundedPolyline(const QPolygonF &points) {
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size() - 1; ++i) {
        const QPointF before = points[i] - points[i - 1], after = points[i + 1] - points[i];
        const qreal radius = qMin(8.0, qMin(std::hypot(before.x(), before.y()), std::hypot(after.x(), after.y())) / 2);
        path.lineTo(points[i] - unit(before) * radius);
        path.quadTo(points[i], points[i] + unit(after) * radius);
    }
    if (points.size() > 1)
        path.lineTo(points.last());
    return path;
}

int nameIndex(const QStringList &names, const QJsonValue &value, int fallback) {
    const int index = names.indexOf(value.toString());
    return index < 0 ? fallback : index;
}
}

int Document::indexOf(const QString &id) const {
    for (int i = 0; i < items.size(); ++i)
        if (items[i].id == id)
            return i;
    return -1;
}

const Item *Document::find(const QString &id) const {
    const int index = id.isEmpty() ? -1 : indexOf(id);
    return index < 0 ? nullptr : &items[index];
}

Item *Document::find(const QString &id) {
    const int index = id.isEmpty() ? -1 : indexOf(id);
    return index < 0 ? nullptr : &items[index];
}

QString Document::newId() const {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (;;) {
        QString id;
        for (int i = 0; i < 6; ++i)
            id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(36)]);
        if (indexOf(id) < 0)
            return id;
    }
}

QPainterPath Document::outline(const Item &item) const {
    QPainterPath path;
    const QRectF r = item.rect;
    switch (item.type) {
    case Item::Rect:
    case Item::Text: {
        const qreal radius = qMin(item.radius, qMin(r.width(), r.height()) / 2);
        if (radius > 0)
            path.addRoundedRect(r, radius, radius);
        else
            path.addRect(r);
        break;
    }
    case Item::Ellipse:
        path.addEllipse(r);
        break;
    case Item::Diamond:
        path.addPolygon(QPolygonF{{r.center().x(), r.top()}, {r.right(), r.center().y()},
                                  {r.center().x(), r.bottom()}, {r.left(), r.center().y()}});
        path.closeSubpath();
        break;
    case Item::Triangle:
        path.addPolygon(QPolygonF{{r.center().x(), r.top()}, r.bottomRight(), r.bottomLeft()});
        path.closeSubpath();
        break;
    case Item::Line:
        return route(item).path;
    case Item::Path: {
        if (item.nodes.isEmpty())
            break;
        auto segment = [&path](const PathNode &a, const PathNode &b) {
            if (a.out.isNull() && b.in.isNull())
                path.lineTo(b.p);
            else
                path.cubicTo(a.p + a.out, b.p + b.in, b.p);
        };
        path.moveTo(item.nodes.first().p);
        for (int i = 1; i < item.nodes.size(); ++i)
            segment(item.nodes[i - 1], item.nodes[i]);
        if (item.closed && item.nodes.size() > 1) {
            segment(item.nodes.last(), item.nodes.first());
            path.closeSubpath();
        }
        break;
    }
    }
    return path;
}

QRectF Document::bounds(const Item &item) const {
    if (item.isShape())
        return item.rect;
    if (item.type == Item::Path && item.nodes.size() == 1)
        return QRectF(item.nodes.first().p, QSizeF(0, 0));
    return outline(item).boundingRect();
}

QPointF Document::edgePoint(const Item &shape, const QPointF &toward) const {
    const QPointF center = shape.rect.center();
    const QPointF direction = unit(toward - center);
    if (direction.isNull())
        return center;
    const QLineF ray(center, center + direction * (shape.rect.width() + shape.rect.height() + 10));
    const QPolygonF polygon = outline(shape).toFillPolygon();
    QPointF best = center;
    qreal bestDistance = -1;
    for (int i = 0; i + 1 < polygon.size(); ++i) {
        QPointF point;
        if (ray.intersects(QLineF(polygon[i], polygon[i + 1]), &point) != QLineF::BoundedIntersection)
            continue;
        const qreal distance = QLineF(center, point).length();
        if (distance > bestDistance) {
            best = point;
            bestDistance = distance;
        }
    }
    return best;
}

QPointF Document::sidePoint(const Item &shape, int side) const {
    const QRectF r = shape.rect;
    return edgePoint(shape, r.center() + QPointF(sideDirection(side).x() * r.width(),
                                                 sideDirection(side).y() * r.height()));
}

qreal Document::arrowLength(const Item &line) { return 8 + line.strokeWidth * 2.5; }

// An orthogonal run from a to b. `da` and `db` point out of the shapes the
// ends are attached to; a free end has no direction and takes whichever one
// makes the simplest corner.
QPolygonF Document::elbow(QPointF a, QPointF da, QPointF b, QPointF db) {
    auto toward = [](const QPointF &from, const QPointF &to, bool horizontal) {
        return horizontal ? QPointF(to.x() >= from.x() ? 1 : -1, 0) : QPointF(0, to.y() >= from.y() ? 1 : -1);
    };
    auto infer = [&toward](const QPointF &self, const QPointF &other, const QPointF &otherDir) {
        const bool otherHorizontal = otherDir.x() != 0;
        const qreal offset = otherHorizontal ? other.y() - self.y() : other.x() - self.x();
        if (qAbs(offset) < 1)
            return -otherDir;
        return toward(self, other, !otherHorizontal);
    };
    const bool freeA = da.isNull(), freeB = db.isNull();
    if (freeA && freeB)
        da = toward(a, b, qAbs(b.x() - a.x()) >= qAbs(b.y() - a.y()));
    if (db.isNull())
        db = infer(b, a, da);
    if (da.isNull())
        da = infer(a, b, db);

    // An attached end runs straight out of its shape before it turns.
    const QPointF p = a + da * (freeA ? 0 : 20), q = b + db * (freeB ? 0 : 20);
    QPolygonF points{a, p};
    const bool horizontalA = da.x() != 0, horizontalB = db.x() != 0;
    if (horizontalA && horizontalB) {
        if (da.x() == -db.x() && (q.x() - p.x()) * da.x() >= 0) {
            const qreal middle = (p.x() + q.x()) / 2;
            points << QPointF(middle, p.y()) << QPointF(middle, q.y());
        } else if (da.x() == -db.x()) {
            const qreal middle = (p.y() + q.y()) / 2;
            points << QPointF(p.x(), middle) << QPointF(q.x(), middle);
        } else {
            const qreal edge = da.x() > 0 ? qMax(p.x(), q.x()) : qMin(p.x(), q.x());
            points << QPointF(edge, p.y()) << QPointF(edge, q.y());
        }
    } else if (!horizontalA && !horizontalB) {
        if (da.y() == -db.y() && (q.y() - p.y()) * da.y() >= 0) {
            const qreal middle = (p.y() + q.y()) / 2;
            points << QPointF(p.x(), middle) << QPointF(q.x(), middle);
        } else if (da.y() == -db.y()) {
            const qreal middle = (p.x() + q.x()) / 2;
            points << QPointF(middle, p.y()) << QPointF(middle, q.y());
        } else {
            const qreal edge = da.y() > 0 ? qMax(p.y(), q.y()) : qMin(p.y(), q.y());
            points << QPointF(p.x(), edge) << QPointF(q.x(), edge);
        }
    } else if (horizontalA) {
        if ((q.x() - p.x()) * da.x() >= 0 && (p.y() - q.y()) * db.y() >= 0)
            points << QPointF(q.x(), p.y());
        else
            points << QPointF(p.x(), q.y());
    } else {
        if ((q.y() - p.y()) * da.y() >= 0 && (p.x() - q.x()) * db.x() >= 0)
            points << QPointF(p.x(), q.y());
        else
            points << QPointF(q.x(), p.y());
    }
    points << q << b;

    // Drop repeats, then points that sit on a straight run.
    QPolygonF distinct;
    for (const QPointF &point : points)
        if (distinct.isEmpty() || QLineF(distinct.last(), point).length() > 0.01)
            distinct << point;
    QPolygonF simple;
    for (int i = 0; i < distinct.size(); ++i) {
        if (i > 0 && i < distinct.size() - 1) {
            const QPointF &before = distinct[i - 1], &here = distinct[i], &after = distinct[i + 1];
            const bool vertical = qAbs(before.x() - here.x()) < 0.01 && qAbs(here.x() - after.x()) < 0.01;
            const bool horizontal = qAbs(before.y() - here.y()) < 0.01 && qAbs(here.y() - after.y()) < 0.01;
            if (vertical || horizontal)
                continue;
        }
        simple << distinct[i];
    }
    return simple;
}

Route Document::route(const Item &line) const {
    const Item *a = find(line.from), *b = find(line.to);
    if (a && !a->isShape())
        a = nullptr;
    if (b && !b->isShape())
        b = nullptr;

    const QPointF centerA = a ? a->rect.center() : line.p1, centerB = b ? b->rect.center() : line.p2;
    int sideA = a ? line.fromSide : int(Item::Auto), sideB = b ? line.toSide : int(Item::Auto);
    // Elbows and curves leave a shape square to one of its sides, so a
    // floating end picks the side facing the other end.
    if (line.routing != Item::Straight) {
        if (a && sideA == Item::Auto)
            sideA = facingSide(a->rect, centerB);
        if (b && sideB == Item::Auto)
            sideB = facingSide(b->rect, centerA);
    }

    // Ends bound to a side resolve first; a floating end then aims at them.
    QPointF start = centerA, end = centerB;
    if (a && sideA != Item::Auto)
        start = sidePoint(*a, sideA);
    if (b && sideB != Item::Auto)
        end = sidePoint(*b, sideB);
    const QPointF aimA = end, aimB = start;
    if (a && sideA == Item::Auto)
        start = edgePoint(*a, aimA);
    if (b && sideB == Item::Auto)
        end = edgePoint(*b, aimB);

    Route route;
    route.start = start;
    route.end = end;
    const qreal trim = arrowLength(line) * 0.7;

    if (line.routing == Item::Curved) {
        QPointF da = a ? sideDirection(sideA) : QPointF(), db = b ? sideDirection(sideB) : QPointF();
        if (da.isNull())
            da = unit(end - start);
        if (db.isNull())
            db = unit(start - end);
        const qreal reach = qBound(30.0, QLineF(start, end).length() * 0.4, 160.0);
        route.startDir = da;
        route.endDir = -db;
        route.path.moveTo(start);
        route.path.cubicTo(start + da * reach, end + db * reach, end);
        const QPointF from = line.arrowStart ? start + da * trim : start, to = line.arrowEnd ? end + db * trim : end;
        route.trimmed.moveTo(from);
        route.trimmed.cubicTo(start + da * reach, end + db * reach, to);
        return route;
    }

    QPolygonF points{start, end};
    if (line.routing == Item::Elbow)
        points = elbow(start, a ? sideDirection(sideA) : QPointF(), end, b ? sideDirection(sideB) : QPointF());
    if (points.size() < 2)
        points = {start, end};
    route.startDir = unit(points[1] - points[0]);
    route.endDir = unit(points[points.size() - 1] - points[points.size() - 2]);
    route.path = roundedPolyline(points);

    QPolygonF shortened = points;
    if (line.arrowStart && QLineF(points[0], points[1]).length() > trim + 1)
        shortened[0] += route.startDir * trim;
    const int last = points.size() - 1;
    if (line.arrowEnd && QLineF(points[last - 1], points[last]).length() > trim + 1)
        shortened[last] -= route.endDir * trim;
    route.trimmed = roundedPolyline(shortened);
    return route;
}

void Document::detach(Item &line) const {
    const Route resolved = route(line);
    line.p1 = resolved.start;
    line.p2 = resolved.end;
    line.from.clear();
    line.to.clear();
    line.fromSide = line.toSide = Item::Auto;
}

bool Document::hit(const Item &item, const QPointF &pos, qreal tolerance) const {
    const qreal reach = tolerance + item.strokeWidth;
    if (!bounds(item).adjusted(-reach, -reach, reach, reach).contains(pos))
        return false;
    const QPainterPath shape = outline(item);
    // Closed shapes are caught anywhere inside, filled or not: a box with
    // only a border is still one thing to grab.
    if ((item.isShape() || (item.type == Item::Path && item.closed)) && shape.contains(pos))
        return true;
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(item.strokeWidth, 0.0) + tolerance * 2);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(shape).contains(pos);
}

int Document::hitTest(const QPointF &pos, qreal tolerance) const {
    for (int i = items.size() - 1; i >= 0; --i)
        if (hit(items[i], pos, tolerance))
            return i;
    return -1;
}

// Where a shape's text goes: inside the part of the outline that is as wide
// as it gets, so words in a circle or a diamond don't run over the border.
QRectF Document::textRect(const Item &item) {
    const QRectF r = item.rect;
    QRectF inner = r;
    qreal padding = item.type == Item::Text ? 4 : 8;
    if (item.type == Item::Ellipse) {
        inner.setSize(r.size() * 0.7071);
        inner.moveCenter(r.center());
        padding = 2;
    } else if (item.type == Item::Diamond) {
        inner.setSize(r.size() * 0.5);
        inner.moveCenter(r.center());
        padding = 2;
    } else if (item.type == Item::Triangle) {
        inner = QRectF(r.left() + r.width() / 4, r.center().y(), r.width() / 2, r.height() / 2);
        padding = 2;
    }
    inner.adjust(padding, padding, -padding, -padding);
    if (inner.width() < 1)
        inner.setWidth(1);
    if (inner.height() < 1)
        inner.setHeight(1);
    return inner;
}

QJsonObject Document::itemToJson(const Item &item) {
    QJsonObject o;
    o["id"] = item.id;
    o["type"] = typeNames.value(item.type);
    if (item.isShape()) {
        o["x"] = item.rect.x();
        o["y"] = item.rect.y();
        o["w"] = item.rect.width();
        o["h"] = item.rect.height();
        if (item.radius > 0)
            o["radius"] = item.radius;
        if (item.autoSize)
            o["autoSize"] = true;
    } else if (item.type == Item::Line) {
        o["x1"] = item.p1.x();
        o["y1"] = item.p1.y();
        o["x2"] = item.p2.x();
        o["y2"] = item.p2.y();
        if (!item.from.isEmpty()) {
            o["from"] = item.from;
            if (item.fromSide != Item::Auto)
                o["fromSide"] = sideNames.value(item.fromSide);
        }
        if (!item.to.isEmpty()) {
            o["to"] = item.to;
            if (item.toSide != Item::Auto)
                o["toSide"] = sideNames.value(item.toSide);
        }
        o["arrowStart"] = item.arrowStart;
        o["arrowEnd"] = item.arrowEnd;
        o["routing"] = routingNames.value(item.routing);
    } else {
        QJsonArray nodes;
        for (const PathNode &node : item.nodes) {
            QJsonObject n{{"x", node.p.x()}, {"y", node.p.y()}};
            if (!node.in.isNull())
                n["in"] = QJsonArray{node.in.x(), node.in.y()};
            if (!node.out.isNull())
                n["out"] = QJsonArray{node.out.x(), node.out.y()};
            nodes.append(n);
        }
        o["nodes"] = nodes;
        o["closed"] = item.closed;
    }
    if (item.type != Item::Line)
        o["fill"] = item.fill;
    o["stroke"] = item.stroke;
    o["strokeWidth"] = item.strokeWidth;
    if (item.dash != 0)
        o["dash"] = dashNames.value(item.dash);
    if (item.opacity < 1)
        o["opacity"] = item.opacity;
    if (item.type != Item::Path) {
        if (!item.text.isEmpty())
            o["text"] = item.text;
        o["font"] = item.font;
        o["fontSize"] = item.fontSize;
        if (item.bold)
            o["bold"] = true;
        if (item.italic)
            o["italic"] = true;
        o["textColor"] = item.textColor;
        if (item.isShape()) {
            o["align"] = alignNames.value(item.hAlign);
            o["valign"] = valignNames.value(item.vAlign);
        }
    }
    if (!item.groups.isEmpty())
        o["groups"] = QJsonArray::fromStringList(item.groups);
    return o;
}

bool Document::itemFromJson(const QJsonObject &o, Item *item, QString *error) {
    Item result;
    const int type = typeNames.indexOf(o.value("type").toString());
    if (type < 0) {
        *error = tr("unknown type \"%1\"").arg(o.value("type").toString());
        return false;
    }
    result.type = Item::Type(type);
    result.id = o.value("id").toString();

    auto number = [&o](const char *key, qreal fallback) {
        const double value = o.value(QLatin1String(key)).toDouble(fallback);
        return std::isfinite(value) ? value : fallback;
    };
    if (result.isShape()) {
        result.rect = QRectF(number("x", 0), number("y", 0), number("w", 160), number("h", 100)).normalized();
        result.radius = qMax(0.0, number("radius", 0));
        result.autoSize = o.value("autoSize").toBool(false);
    } else if (result.type == Item::Line) {
        result.p1 = {number("x1", 0), number("y1", 0)};
        result.p2 = {number("x2", 0), number("y2", 0)};
        result.from = o.value("from").toString();
        result.to = o.value("to").toString();
        result.fromSide = nameIndex(sideNames, o.value("fromSide"), Item::Auto);
        result.toSide = nameIndex(sideNames, o.value("toSide"), Item::Auto);
        result.arrowStart = o.value("arrowStart").toBool(false);
        result.arrowEnd = o.value("arrowEnd").toBool(false);
        result.routing = nameIndex(routingNames, o.value("routing"), Item::Straight);
    } else {
        const QJsonArray nodes = o.value("nodes").toArray();
        if (nodes.isEmpty()) {
            *error = tr("a path needs at least one node");
            return false;
        }
        auto offset = [](const QJsonValue &value) {
            const QJsonArray pair = value.toArray();
            return pair.size() == 2 ? QPointF(pair[0].toDouble(), pair[1].toDouble()) : QPointF();
        };
        for (const QJsonValue &value : nodes) {
            const QJsonObject n = value.toObject();
            result.nodes.append({{n.value("x").toDouble(), n.value("y").toDouble()}, offset(n.value("in")),
                                 offset(n.value("out"))});
        }
        result.closed = o.value("closed").toBool(false);
    }

    const bool text = result.type == Item::Text;
    result.fill = o.value("fill").toString(text || result.type == Item::Path ? "none" : "paper");
    result.stroke = o.value("stroke").toString(text ? "none" : "ink");
    result.strokeWidth = qBound(0.0, number("strokeWidth", 2), 40.0);
    result.dash = nameIndex(dashNames, o.value("dash"), 0);
    result.opacity = qBound(0.0, number("opacity", 1), 1.0);
    result.text = o.value("text").toString();
    result.font = o.value("font").toString("sans");
    result.fontSize = qBound(6, int(number("fontSize", 16)), 400);
    result.bold = o.value("bold").toBool(false);
    result.italic = o.value("italic").toBool(false);
    result.textColor = o.value("textColor").toString("ink");
    result.hAlign = nameIndex(alignNames, o.value("align"), text ? 0 : 1);
    result.vAlign = nameIndex(valignNames, o.value("valign"), text ? 0 : 1);
    for (const QJsonValue &group : o.value("groups").toArray())
        if (!group.toString().isEmpty())
            result.groups << group.toString();
    *item = result;
    return true;
}

QJsonObject Document::toJson() const {
    QJsonArray array;
    for (const Item &item : items)
        array.append(itemToJson(item));
    return {{"chalk", 1}, {"paper", paper}, {"items", array}};
}

QByteArray Document::serialize() const { return QJsonDocument(toJson()).toJson(QJsonDocument::Indented); }

bool Document::fromJson(const QJsonObject &object, Document *document, QString *error, QStringList *warnings) {
    if (!object.contains("chalk") || !object.value("items").isArray()) {
        *error = tr("This is not a Chalk board.");
        return false;
    }
    if (object.value("chalk").toInt() > 1) {
        *error = tr("This board was made by a newer Chalk.");
        return false;
    }
    Document result;
    result.paper = object.value("paper").toString() == "light" ? "light" : "dark";
    QSet<QString> seen;
    const QJsonArray array = object.value("items").toArray();
    for (int i = 0; i < array.size(); ++i) {
        Item item;
        QString problem;
        if (!array[i].isObject() || !itemFromJson(array[i].toObject(), &item, &problem)) {
            *error = tr("Item %1: %2").arg(i + 1).arg(problem.isEmpty() ? tr("not an object") : problem);
            return false;
        }
        if (item.id.isEmpty())
            item.id = result.newId();
        if (seen.contains(item.id)) {
            *error = tr("Item %1: the id \"%2\" is used twice").arg(i + 1).arg(item.id);
            return false;
        }
        seen.insert(item.id);
        result.items.append(item);
    }
    // A line bound to something that isn't a shape here keeps its own points.
    for (Item &item : result.items) {
        if (item.type != Item::Line)
            continue;
        for (QString *end : {&item.from, &item.to}) {
            const Item *target = result.find(*end);
            if (end->isEmpty() || (target && target->isShape()))
                continue;
            if (warnings)
                warnings->append(tr("Line \"%1\" is attached to \"%2\", which is not a shape on this board.")
                                     .arg(item.id, *end));
            end->clear();
        }
    }
    *document = result;
    return true;
}

bool Document::parse(const QByteArray &data, Document *document, QString *error, QStringList *warnings) {
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = tr("Not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!json.isObject()) {
        *error = tr("This is not a Chalk board.");
        return false;
    }
    return fromJson(json.object(), document, error, warnings);
}
