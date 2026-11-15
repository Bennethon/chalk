#pragma once

#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>

// One anchor of a drawn path. The handles are offsets from the anchor, so
// moving an anchor carries its curve along; a zero handle means a corner.
struct PathNode {
    QPointF p;
    QPointF in;
    QPointF out;
};

// Everything on the board is an Item. Shapes live in `rect`, lines between
// `p1` and `p2` (or the shapes named by `from` and `to`), and paths in
// `nodes`. Colors are "#rrggbb", "#aarrggbb", "none", or one of the two
// names that follow the paper: "ink" and "paper".
struct Item {
    enum Type { Rect, Ellipse, Diamond, Triangle, Text, Line, Path };
    enum Routing { Straight, Elbow, Curved };
    enum Side { Auto, Top, Right, Bottom, Left };

    QString id;
    Type type = Rect;
    QRectF rect;

    QString fill = "paper";
    QString stroke = "ink";
    qreal strokeWidth = 2;
    int dash = 0;
    qreal radius = 0;
    qreal opacity = 1;

    QString text;
    QString font = "sans";
    int fontSize = 16;
    bool bold = false;
    bool italic = false;
    QString textColor = "ink";
    int hAlign = 1;
    int vAlign = 1;
    // A text item made with a click hugs its text until it's resized by hand.
    bool autoSize = false;

    QPointF p1, p2;
    QString from, to;
    int fromSide = Auto, toSide = Auto;
    bool arrowStart = false, arrowEnd = false;
    int routing = Straight;

    QList<PathNode> nodes;
    bool closed = false;

    // Group ids, innermost first. Items sharing their last id select together.
    QStringList groups;

    // A shape has a box that text sits in and that lines attach to.
    bool isShape() const { return type <= Text; }
};

// Where a line actually runs once its attachments are resolved.
struct Route {
    QPointF start, end;
    // Unit directions of travel at each tip.
    QPointF startDir, endDir;
    // Tip to tip, for hit testing and bounds.
    QPainterPath path;
    // The stroke to paint: pulled back under the arrowheads.
    QPainterPath trimmed;
};

class Document {
public:
    // Back to front.
    QList<Item> items;
    QString paper = "dark";

    int indexOf(const QString &id) const;
    const Item *find(const QString &id) const;
    Item *find(const QString &id);
    QString newId() const;

    QPainterPath outline(const Item &item) const;
    QRectF bounds(const Item &item) const;
    Route route(const Item &line) const;
    bool hit(const Item &item, const QPointF &pos, qreal tolerance) const;
    int hitTest(const QPointF &pos, qreal tolerance) const;

    // Where a line bound to this side of a shape touches its outline.
    QPointF sidePoint(const Item &shape, int side) const;
    // Where a line from the shape's center toward a point leaves its outline.
    QPointF edgePoint(const Item &shape, const QPointF &toward) const;
    // Replaces a line's attachments with the points they resolve to.
    void detach(Item &line) const;

    static QRectF textRect(const Item &item);
    static qreal arrowLength(const Item &line);
    static QPolygonF elbow(QPointF a, QPointF da, QPointF b, QPointF db);

    QJsonObject toJson() const;
    QByteArray serialize() const;
    static QJsonObject itemToJson(const Item &item);
    static bool itemFromJson(const QJsonObject &object, Item *item, QString *error);
    // Problems that stop a board from loading go to `error`; ones that were
    // repaired on the way in (a line bound to a shape that isn't there) go
    // to `warnings`.
    static bool fromJson(const QJsonObject &object, Document *document, QString *error,
                         QStringList *warnings = nullptr);
    static bool parse(const QByteArray &data, Document *document, QString *error,
                      QStringList *warnings = nullptr);
};
