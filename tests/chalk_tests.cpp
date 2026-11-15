#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "document.h"
#include "exporter.h"
#include "filepicker.h"
#include "renderer.h"
#include "theme.h"
#include "translator.h"

// Answers the file dialogs with whatever the test put in `answer`.
class FakePicker : public FilePicker {
    Q_OBJECT

public:
    QString answer;
    QString lastFormat;
    void openBoard(const QString &) override { reply(&FilePicker::openSelected); }
    void saveBoard(const QUrl &) override { reply(&FilePicker::saveSelected); }
    void exportImage(const QUrl &, const QString &format) override {
        lastFormat = format;
        reply(&FilePicker::exportSelected);
    }

private:
    void reply(void (FilePicker::*chosen)(const QUrl &)) {
        if (answer.isEmpty())
            emit canceled();
        else
            emit (this->*chosen)(QUrl::fromLocalFile(answer));
    }
};

class ChalkTests : public QObject {
    Q_OBJECT

private slots:
    // --- The board ---

    void drawsAShapeByDragging()
    {
        Backend backend;
        drag(backend, "rect", {10, 20}, {210, 120});
        QCOMPARE(backend.document().items.size(), 1);
        QCOMPARE(backend.document().items[0].rect, QRectF(10, 20, 200, 100));
        QCOMPARE(backend.tool(), QString("select"));
        QCOMPARE(backend.selectionCount(), 1);
        QVERIFY(backend.dirty());
    }

    void aClickPlacesAShapeUnderThePointer()
    {
        Backend backend;
        backend.setTool("ellipse");
        click(backend, {300, 200});
        QCOMPARE(backend.document().items.size(), 1);
        QCOMPARE(backend.document().items[0].type, Item::Ellipse);
        QCOMPARE(backend.document().items[0].rect.center(), QPointF(300, 200));
    }

    void shiftDrawsASquare()
    {
        Backend backend;
        backend.setTool("rect");
        backend.press(0, 0, 0, 1);
        backend.move(120, 50, Qt::ShiftModifier, 1);
        backend.release(120, 50, Qt::ShiftModifier, 1);
        QCOMPARE(backend.document().items[0].rect.size(), QSizeF(120, 120));
    }

    void movesTheSelectionAndUndoes()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        // Alt keeps the move free of snapping, so the numbers are exact.
        backend.press(50, 50, 0, 1);
        backend.move(80, 90, Qt::AltModifier, 1);
        backend.release(80, 90, Qt::AltModifier, 1);
        QCOMPARE(backend.document().items[0].rect, QRectF(30, 40, 100, 100));
        backend.undo();
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 100, 100));
        backend.redo();
        QCOMPARE(backend.document().items[0].rect, QRectF(30, 40, 100, 100));
        backend.undo();
        backend.undo();
        QVERIFY(backend.empty());
        QVERIFY(!backend.canUndo());
    }

    void escapeMidDragPutsThingsBack()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        backend.press(50, 50, 0, 1);
        backend.move(250, 250, Qt::AltModifier, 1);
        backend.escape();
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 100, 100));
        backend.undo();
        QVERIFY(backend.empty());
    }

    void ctrlDragCarriesOffACopy()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        const QString original = backend.document().items[0].id;
        backend.press(50, 50, Qt::ControlModifier, 1);
        backend.move(350, 250, Qt::ControlModifier | Qt::AltModifier, 1);
        backend.release(350, 250, Qt::ControlModifier | Qt::AltModifier, 1);
        QCOMPARE(backend.document().items.size(), 2);
        QCOMPARE(backend.document().items[0].id, original);
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 100, 100));
        QCOMPARE(backend.document().items[1].rect, QRectF(300, 200, 100, 100));
        // The copy is what's selected, and one step back takes it away.
        QCOMPARE(backend.selection(), QStringList{backend.document().items[1].id});
        backend.undo();
        QCOMPARE(backend.document().items.size(), 1);
        QCOMPARE(backend.selection(), QStringList{original});

        // Escape halfway leaves no copy behind.
        backend.press(50, 50, Qt::ControlModifier, 1);
        backend.move(350, 250, Qt::ControlModifier, 1);
        QCOMPARE(backend.document().items.size(), 2);
        backend.escape();
        QCOMPARE(backend.document().items.size(), 1);
        QVERIFY(!backend.canRedo());
    }

    void aMoveCatchesOnTheGapItsNeighboursKeep()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {120, 0}, {220, 100});
        // A third, copied off the second and let go a few units shy of the
        // same 20 gap: it lands exactly on it.
        backend.press(170, 50, Qt::ControlModifier, 1);
        backend.move(290, 55, Qt::ControlModifier, 1);
        QCOMPARE(backend.spacers().size(), 2);
        QCOMPARE(backend.spacers()[0].length(), 20.0);
        QCOMPARE(backend.spacers()[1].length(), 20.0);
        backend.release(290, 55, Qt::ControlModifier, 1);
        QVERIFY(backend.spacers().isEmpty());
        QCOMPARE(backend.document().items.size(), 3);
        QCOMPARE(backend.document().items[2].rect, QRectF(240, 0, 100, 100));
        QCOMPARE(backend.style().value("gapH").toDouble(), 0.0);
        backend.selectAll();
        QCOMPARE(backend.style().value("gapH").toDouble(), 20.0);

        // To the left of the first, the same gap catches too.
        backend.escape();
        backend.press(290, 50, 0, 1);
        backend.move(-73, 50, 0, 1);
        backend.release(-73, 50, 0, 1);
        QCOMPARE(backend.document().items[2].rect, QRectF(-120, 0, 100, 100));

        // And halfway between two that are far enough apart.
        backend.press(-70, 50, 0, 1);
        backend.move(-70, 400, Qt::AltModifier, 1);
        backend.release(-70, 400, Qt::AltModifier, 1);
        backend.escape();
        backend.press(170, 50, 0, 1);
        backend.move(450, 50, Qt::AltModifier, 1);
        backend.release(450, 50, Qt::AltModifier, 1);
        QCOMPARE(backend.document().items[1].rect, QRectF(400, 0, 100, 100));
        backend.press(-70, 400, 0, 1);
        backend.move(247, 80, 0, 1);
        backend.release(247, 80, 0, 1);
        QCOMPARE(backend.document().items[2].rect.left(), 200.0);
    }

    void aResizedEdgeCatchesOnItsNeighbours()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {300, 20}, {400, 160});
        backend.escape();
        click(backend, {50, 50});
        // The right edge, let go a few units shy of the other one's left.
        backend.press(100, 50, 0, 1);
        backend.move(296, 50, 0, 1);
        QCOMPARE(backend.guides().size(), 1);
        QCOMPARE(backend.guides()[0].x1(), 300.0);
        backend.release(296, 50, 0, 1);
        QVERIFY(backend.guides().isEmpty());
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 300, 100));

        // A corner catches both ways at once: its bottom on the other's.
        backend.press(300, 100, 0, 1);
        backend.move(204, 157, 0, 1);
        QCOMPARE(backend.guides().size(), 1);
        backend.release(204, 157, 0, 1);
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 204, 160));

        // Alt resizes freely.
        backend.press(204, 80, 0, 1);
        backend.move(296, 80, Qt::AltModifier, 1);
        QVERIFY(backend.guides().isEmpty());
        backend.release(296, 80, Qt::AltModifier, 1);
        QCOMPARE(backend.document().items[0].rect.right(), 296.0);
    }

    void aMoveShowsHowFarItIsFromWhatsNear()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {600, 0}, {700, 100});
        // Far apart, there's nothing worth measuring.
        backend.press(650, 50, 0, 1);
        backend.move(640, 50, Qt::AltModifier, 1);
        QVERIFY(backend.distances().isEmpty());
        // Close, the gap between them is.
        backend.move(187, 50, Qt::AltModifier, 1);
        QCOMPARE(backend.distances().size(), 1);
        QCOMPARE(backend.distances()[0].length(), 37.0);
        QCOMPARE(backend.distances()[0].p1(), QPointF(100, 50));
        // It follows the move, and goes when the move ends.
        backend.move(200, 50, Qt::AltModifier, 1);
        QCOMPARE(backend.distances()[0].length(), 50.0);
        backend.release(200, 50, Qt::AltModifier, 1);
        QVERIFY(backend.distances().isEmpty());
        QCOMPARE(backend.document().items[1].rect.left(), 150.0);

        // Below as well as beside, each side to its nearest.
        drag(backend, "rect", {150, 400}, {250, 500});
        backend.press(200, 450, 0, 1);
        backend.move(200, 180, Qt::AltModifier, 1);
        QCOMPARE(backend.distances().size(), 1);
        QCOMPARE(backend.distances()[0].length(), 30.0);
        QCOMPARE(backend.distances()[0].dx(), 0.0);
        backend.release(200, 180, Qt::AltModifier, 1);

        // A gap the snapping has marked as matching isn't measured twice.
        backend.selectAll();
        backend.deleteSelection();
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {120, 0}, {220, 100});
        backend.press(170, 50, Qt::ControlModifier, 1);
        backend.move(290, 50, Qt::ControlModifier, 1);
        QCOMPARE(backend.spacers().size(), 2);
        QVERIFY(backend.distances().isEmpty());
        backend.release(290, 50, Qt::ControlModifier, 1);
    }

    void aRightClickPicksWhatTheMenuActsOn()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {200, 0}, {300, 100});
        backend.selectAll();
        // On something already selected, the selection stays whole.
        backend.contextAt(50, 50, 1);
        QCOMPARE(backend.selectionCount(), 2);
        backend.escape();
        backend.contextAt(250, 50, 1);
        QCOMPARE(backend.selection(), QStringList{backend.document().items[1].id});
        QCOMPARE(backend.style().value("single").toString(), QString("shape"));
        backend.contextAt(500, 500, 1);
        QCOMPARE(backend.selectionCount(), 0);
    }

    void aNewBoardStartsOnTheAppsPaper()
    {
        Backend backend;
        QCOMPARE(backend.paper(), QString("dark"));
        backend.setDefaultPaper("light");
        QCOMPARE(backend.paper(), QString("light"));
        QVERIFY(!backend.dirty());
        // A board with work on it keeps its paper until it's changed on purpose.
        drag(backend, "rect", {0, 0}, {100, 100});
        backend.setDefaultPaper("dark");
        QCOMPARE(backend.paper(), QString("light"));
        backend.newDocument();
        QCOMPARE(backend.paper(), QString("dark"));
    }

    void resizesSeveralAtOnce()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "ellipse", {100, 100}, {200, 200});
        backend.selectAll();
        QCOMPARE(backend.selectionBounds(), QRectF(0, 0, 200, 200));
        // The bottom right handle, dragged out to twice the size.
        backend.press(200, 200, 0, 1);
        backend.move(400, 400, 0, 1);
        backend.release(400, 400, 0, 1);
        QCOMPARE(backend.document().items[0].rect, QRectF(0, 0, 200, 200));
        QCOMPARE(backend.document().items[1].rect, QRectF(200, 200, 200, 200));
    }

    void marqueeSelectsWhatItTouches()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {300, 0}, {400, 100});
        drag(backend, "rect", {0, 300}, {100, 400});
        backend.escape();
        backend.press(-20, -20, 0, 1);
        backend.move(350, 150, 0, 1);
        backend.release(350, 150, 0, 1);
        QCOMPARE(backend.selectionCount(), 2);
    }

    void alignsAndSpacesOut()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 50});
        drag(backend, "rect", {130, 20}, {230, 70});
        drag(backend, "rect", {400, 40}, {500, 90});
        backend.selectAll();
        backend.align("top");
        for (const Item &item : backend.document().items)
            QCOMPARE(item.rect.top(), 0.0);

        backend.distribute("h");
        QCOMPARE(backend.document().items[0].rect.left(), 0.0);
        QCOMPARE(backend.document().items[1].rect.left(), 200.0);
        QCOMPARE(backend.document().items[2].rect.left(), 400.0);
        QCOMPARE(backend.style().value("gapH").toDouble(), 100.0);

        backend.setGap("h", 24);
        QCOMPARE(backend.document().items[1].rect.left(), 124.0);
        QCOMPARE(backend.document().items[2].rect.left(), 248.0);

        backend.align("right");
        for (const Item &item : backend.document().items)
            QCOMPARE(item.rect.right(), 348.0);
        // Unevenly spaced things have no one gap to report.
        backend.undo();
        backend.nudge(0, 0);
        backend.escape();
        backend.press(50, 25, 0, 1);
        backend.move(57, 25, Qt::AltModifier, 1);
        backend.release(57, 25, Qt::AltModifier, 1);
        backend.selectAll();
        QVERIFY(!backend.style().contains("gapH"));
    }

    void aGroupAlignsAsOneThing()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {50, 50});
        drag(backend, "rect", {60, 60}, {110, 110});
        backend.selectAll();
        backend.group();
        drag(backend, "rect", {300, 300}, {400, 400});
        backend.selectAll();
        QCOMPARE(backend.style().value("units").toInt(), 2);
        backend.align("left");
        // The pair keeps its own arrangement and moves together.
        QCOMPARE(backend.document().items[0].rect.left(), 0.0);
        QCOMPARE(backend.document().items[1].rect.left(), 60.0);
        QCOMPARE(backend.document().items[2].rect.left(), 0.0);
    }

    void groupsSelectTogetherAndUngroup()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {50, 50});
        drag(backend, "rect", {100, 0}, {150, 50});
        backend.selectAll();
        backend.group();
        backend.escape();
        QCOMPARE(backend.selectionCount(), 0);
        click(backend, {25, 25});
        QCOMPARE(backend.selectionCount(), 2);
        // Ctrl reaches the one inside.
        backend.escape();
        backend.press(25, 25, Qt::ControlModifier, 1);
        backend.release(25, 25, Qt::ControlModifier, 1);
        QCOMPARE(backend.selectionCount(), 1);

        backend.selectAll();
        backend.ungroup();
        backend.escape();
        click(backend, {25, 25});
        QCOMPARE(backend.selectionCount(), 1);
    }

    void stacksForwardAndBack()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {20, 20}, {120, 120});
        drag(backend, "rect", {40, 40}, {140, 140});
        const QString bottom = backend.document().items[0].id, top = backend.document().items[2].id;
        backend.escape();
        click(backend, {5, 5});
        QCOMPARE(backend.selection(), QStringList{bottom});
        backend.order("forward");
        QCOMPARE(backend.document().items[1].id, bottom);
        backend.order("front");
        QCOMPARE(backend.document().items[2].id, bottom);
        backend.order("back");
        QCOMPARE(backend.document().items[0].id, bottom);
        QCOMPARE(backend.document().items[2].id, top);
    }

    void stylesTheSelectionAndTheNextShape()
    {
        Backend backend;
        backend.setStyle("stroke", "#e5484d");
        drag(backend, "rect", {0, 0}, {100, 100});
        QCOMPARE(backend.document().items[0].stroke, QString("#e5484d"));
        backend.setStyle("fill", "#3e8ef7");
        backend.setStyle("radius", 12);
        backend.setStyle("hAlign", 0);
        const Item &item = backend.document().items[0];
        QCOMPARE(item.fill, QString("#3e8ef7"));
        QCOMPARE(item.radius, 12.0);
        QCOMPARE(item.hAlign, 0);
        QCOMPARE(backend.style().value("fill").toString(), QString("#3e8ef7"));
    }

    // --- Text ---

    void doubleClickPutsTextInAShape()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {160, 100});
        backend.doubleClick(80, 50, 0, 1);
        QCOMPARE(backend.editingId(), backend.document().items[0].id);
        QCOMPARE(backend.editing().value("mode").toString(), QString("box"));
        backend.commitText("Hello");
        QCOMPARE(backend.document().items[0].text, QString("Hello"));
        QVERIFY(backend.editingId().isEmpty());
        backend.undo();
        QVERIFY(backend.document().items[0].text.isEmpty());
    }

    void typedTextHugsItsWordsAndEmptyTextVanishes()
    {
        Backend backend;
        backend.setTool("text");
        click(backend, {100, 100});
        QVERIFY(!backend.editingId().isEmpty());
        backend.commitText("A line of text");
        QCOMPARE(backend.document().items.size(), 1);
        const Item &text = backend.document().items[0];
        QVERIFY(text.autoSize);
        QVERIFY(text.rect.width() > 40);
        // Making and typing it is one step back.
        backend.undo();
        QVERIFY(backend.empty());

        backend.setTool("text");
        click(backend, {100, 100});
        backend.commitText("  ");
        QVERIFY(backend.empty());
        QVERIFY(!backend.canUndo());
    }

    // --- Lines ---

    void connectsTwoShapes()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {300, 0}, {400, 100});
        const QString a = backend.document().items[0].id, b = backend.document().items[1].id;
        // Dragged from well inside one to well inside the other, away from
        // the side midpoints, so both ends float.
        backend.setTool("arrow");
        backend.press(50, 30, 0, 1);
        backend.move(200, 40, 0, 1);
        QVERIFY(backend.attachId().isEmpty());
        backend.move(350, 30, 0, 1);
        QCOMPARE(backend.attachId(), b);
        backend.release(350, 30, 0, 1);

        const Item line = backend.document().items[2];
        QCOMPARE(line.type, Item::Line);
        QCOMPARE(line.from, a);
        QCOMPARE(line.to, b);
        QVERIFY(line.arrowEnd);
        const Route route = backend.document().route(line);
        QCOMPARE(route.start, QPointF(100, 50));
        QCOMPARE(route.end, QPointF(300, 50));

        // Move a shape and the line follows it.
        backend.escape();
        backend.press(350, 80, 0, 1);
        backend.move(350, 280, Qt::AltModifier, 1);
        backend.release(350, 280, Qt::AltModifier, 1);
        QCOMPARE(backend.document().items[1].rect.top(), 200.0);
        const Route moved = backend.document().route(backend.document().items[2]);
        QVERIFY(moved.end.y() > 150);
        QVERIFY(backend.document().outline(backend.document().items[1]).boundingRect()
                    .adjusted(-1, -1, 1, 1).contains(moved.end));
    }

    void aLineSurvivesItsShapeBeingDeleted()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {300, 0}, {400, 100});
        backend.setTool("line");
        backend.press(50, 30, 0, 1);
        backend.move(350, 30, 0, 1);
        backend.release(350, 30, 0, 1);
        backend.escape();
        click(backend, {350, 80});
        backend.deleteSelection();
        QCOMPARE(backend.document().items.size(), 2);
        const Item line = backend.document().items[1];
        QVERIFY(line.to.isEmpty());
        QCOMPARE(line.p2, QPointF(300, 50));
    }

    void elbowsRunSquare()
    {
        const QPolygonF route = Document::elbow({100, 50}, {1, 0}, {300, 250}, {-1, 0});
        QCOMPARE(route.first(), QPointF(100, 50));
        QCOMPARE(route.last(), QPointF(300, 250));
        for (int i = 1; i < route.size(); ++i)
            QVERIFY(qFuzzyCompare(route[i].x(), route[i - 1].x()) || qFuzzyCompare(route[i].y(), route[i - 1].y()));
        // A loose end takes one corner.
        QCOMPARE(Document::elbow({0, 0}, {}, {100, 60}, {}).size(), 3);
    }

    // --- Paths ---

    void thePenDrawsCornersAndCurves()
    {
        Backend backend;
        backend.setTool("pen");
        click(backend, {0, 0});
        QVERIFY(backend.penActive());
        click(backend, {100, 0});
        backend.press(100, 100, 0, 1);
        backend.move(140, 100, 0, 1);
        backend.release(140, 100, 0, 1);
        backend.enter();
        QVERIFY(!backend.penActive());
        QCOMPARE(backend.tool(), QString("select"));
        const Item path = backend.document().items[0];
        QCOMPARE(path.nodes.size(), 3);
        QVERIFY(path.nodes[1].out.isNull());
        QCOMPARE(path.nodes[2].out, QPointF(40, 0));
        QCOMPARE(path.nodes[2].in, QPointF(-40, 0));
        // One step back takes the whole path.
        backend.undo();
        QVERIFY(backend.empty());
    }

    void aPathOfOnePointLeavesNothing()
    {
        Backend backend;
        backend.setTool("pen");
        click(backend, {0, 0});
        backend.escape();
        QVERIFY(backend.empty());
        QVERIFY(!backend.canUndo());
    }

    void closingAPathAndMovingItsPoints()
    {
        Backend backend;
        backend.setTool("pen");
        click(backend, {0, 0});
        click(backend, {100, 0});
        click(backend, {100, 100});
        click(backend, {0, 0});
        QVERIFY(!backend.penActive());
        QVERIFY(backend.document().items[0].closed);
        QCOMPARE(backend.document().items[0].nodes.size(), 3);

        backend.enter();
        QVERIFY(backend.nodeEditing());
        backend.press(100, 100, 0, 1);
        backend.move(150, 160, 0, 1);
        backend.release(150, 160, 0, 1);
        QCOMPARE(backend.document().items[0].nodes[2].p, QPointF(150, 160));
        // Double-clicking a side adds a point there.
        backend.doubleClick(50, 0, 0, 1);
        QCOMPARE(backend.document().items[0].nodes.size(), 4);
        QCOMPARE(backend.document().items[0].nodes[1].p, QPointF(50, 0));
        backend.deleteSelection();
        QCOMPARE(backend.document().items[0].nodes.size(), 3);
        backend.escape();
        QVERIFY(!backend.nodeEditing());
    }

    void thePencilSmoothsAStroke()
    {
        Backend backend;
        backend.setTool("pencil");
        backend.press(0, 0, 0, 1);
        for (int x = 5; x <= 200; x += 5)
            backend.move(x, 40 * std::sin(x / 30.0), 0, 1);
        backend.release(200, 40 * std::sin(200 / 30.0), 0, 1);
        QCOMPARE(backend.document().items.size(), 1);
        const Item path = backend.document().items[0];
        QCOMPARE(path.type, Item::Path);
        QVERIFY(path.nodes.size() >= 3);
        QVERIFY(path.nodes.size() < 30);
        QCOMPARE(backend.tool(), QString("pencil"));
    }

    // --- Copies ---

    void duplicatesKeepTheirConnections()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {300, 0}, {400, 100});
        backend.setTool("line");
        backend.press(50, 30, 0, 1);
        backend.move(350, 30, 0, 1);
        backend.release(350, 30, 0, 1);
        backend.selectAll();
        backend.duplicate();
        QCOMPARE(backend.document().items.size(), 6);
        const Item line = backend.document().items[5];
        QCOMPARE(line.from, backend.document().items[3].id);
        QCOMPARE(line.to, backend.document().items[4].id);
        QCOMPARE(backend.document().items[3].rect.topLeft(), QPointF(16, 16));
    }

    void pastesWhereItIsToldTo()
    {
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        backend.copy();
        backend.paste(500, 500);
        QCOMPARE(backend.document().items.size(), 2);
        QCOMPARE(backend.document().items[1].rect.center(), QPointF(500, 500));
        QVERIFY(backend.document().items[0].id != backend.document().items[1].id);
    }

    // --- Files ---

    void boardsRoundTrip()
    {
        Document document;
        QString error;
        QVERIFY2(Document::parse(sample(), &document, &error), qPrintable(error));
        QCOMPARE(document.items.size(), 5);
        QCOMPARE(document.paper, QString("light"));

        Document again;
        QVERIFY(Document::parse(document.serialize(), &again, &error));
        QCOMPARE(again.serialize(), document.serialize());
        QCOMPARE(again.items[3].nodes[1].in, QPointF(-10, 5));
        QCOMPARE(again.items[4].routing, int(Item::Elbow));
        QCOMPARE(again.items[0].groups, QStringList{"g1"});
    }

    void badBoardsSayWhy()
    {
        Document document;
        QString error;
        QVERIFY(!Document::parse("{", &document, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Document::parse(R"({"items": []})", &document, &error));
        QVERIFY(!Document::parse(R"({"chalk": 1, "items": [{"type": "blob"}]})", &document, &error));
        QVERIFY(error.contains("blob"));
        QVERIFY(!Document::parse(R"({"chalk": 1, "items": [{"id": "a", "type": "rect"}, {"id": "a", "type": "rect"}]})",
                                 &document, &error));
        QVERIFY(!Document::parse(R"({"chalk": 2, "items": []})", &document, &error));

        QStringList warnings;
        QVERIFY(Document::parse(R"({"chalk": 1, "items": [{"id": "l", "type": "line", "from": "gone"}]})",
                                &document, &error, &warnings));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(document.items[0].from.isEmpty());
    }

    void savesAndOpensThroughThePicker()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        drag(backend, "rect", {0, 0}, {100, 100});
        QVERIFY(backend.dirty());

        // A closed dialog saves nothing and says the save was dropped.
        QSignalSpy abandoned(&backend, &Backend::saveAbandoned);
        backend.save();
        QCOMPARE(abandoned.count(), 1);
        QVERIFY(backend.dirty());

        picker.answer = dir.filePath("board");
        QSignalSpy saved(&backend, &Backend::saved);
        backend.save();
        QCOMPARE(saved.count(), 1);
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.filePath(), dir.filePath("board.chalk"));
        QCOMPARE(backend.fileName(), QString("board"));
        // No stray temporary file left beside the board.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"board.chalk"});

        Backend other(&picker);
        picker.answer = dir.filePath("board.chalk");
        other.chooseOpen();
        QCOMPARE(other.document().items.size(), 1);
        QCOMPARE(other.document().items[0].rect, QRectF(0, 0, 100, 100));
        QVERIFY(!other.dirty());
    }

    void aBoardThatWontOpenLeavesTheOneInHandAlone()
    {
        QTemporaryDir dir;
        write(dir.filePath("broken.chalk"), "not a board");
        Backend backend;
        drag(backend, "rect", {0, 0}, {100, 100});
        QVERIFY(!backend.load(dir.filePath("broken.chalk")));
        QVERIFY(backend.messageIsError());
        QCOMPARE(backend.document().items.size(), 1);
        QVERIFY(!backend.load(dir.filePath("missing.chalk")));
    }

    void exportsEveryFormat()
    {
        QTemporaryDir dir;
        Document document;
        QString error;
        QVERIFY(Document::parse(sample(), &document, &error));
        for (const QString &format : Exporter::formats()) {
            const QString path = dir.filePath("out." + format);
            QVERIFY2(Exporter::write(document, path, {}, &error), qPrintable(error));
            QVERIFY(QFileInfo(path).size() > 200);
        }
        const QImage image(dir.filePath("out.png"));
        const QRectF area = Exporter::area(document, {});
        QCOMPARE(image.size(), (area.size() * 2).toSize());
        // Light paper in the corner, and nothing see-through.
        QCOMPARE(image.pixelColor(2, 2), Renderer::paperColor("light"));

        Exporter::Options clear;
        clear.background = false;
        clear.scale = 1;
        QCOMPARE(Exporter::image(document, clear).pixelColor(2, 2).alpha(), 0);
        QVERIFY(QFile(dir.filePath("out.svg")).open(QIODevice::ReadOnly));

        QVERIFY(!Exporter::write(Document(), dir.filePath("empty.png"), {}, &error));
        QVERIFY(!QFileInfo::exists(dir.filePath("empty.png")));
        QVERIFY(!Exporter::write(document, dir.filePath("out.gif"), {}, &error));
    }

    void exportsJustTheSelectionThroughThePicker()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        drag(backend, "rect", {0, 0}, {100, 100});
        drag(backend, "rect", {900, 900}, {1000, 1000});
        picker.answer = dir.filePath("part");
        backend.chooseExport({{"format", "png"}, {"scale", 1}, {"selectionOnly", true}});
        QCOMPARE(picker.lastFormat, QString("png"));
        const QImage image(dir.filePath("part.png"));
        QVERIFY(!image.isNull());
        QVERIFY(image.width() < 200);
    }

    void inkAndPaperFollowThePaper()
    {
        QCOMPARE(Renderer::color("ink", "dark").lightness() > 200, true);
        QCOMPARE(Renderer::color("ink", "light").lightness() < 60, true);
        QCOMPARE(Renderer::color("paper", "light"), Renderer::paperColor("light"));
        QCOMPARE(Renderer::color("none", "dark").alpha(), 0);
        QCOMPARE(Renderer::color("#403e8ef7", "dark").alpha(), 0x40);
    }

    // --- Theme and language ---

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeIsLightOrDark()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        Theme fallback(dir.path());
        QVERIFY(fallback.dark());
        write(dir.path() + "/theme/colors.toml", "mode = \"light\"\naccent = \"#FFD60A\"\n");

        Theme theme(dir.path());
        QVERIFY(!theme.dark());
        const QString lightPanel = theme.panel();
        // A bright accent is darkened where it's used as ink on a light window.
        QVERIFY(QColor(theme.accentInk()).lightness() < QColor(theme.accent()).lightness());

        QSignalSpy chosen(&theme, &Theme::darkChosen);
        theme.setDark(true);
        QVERIFY(theme.dark());
        QCOMPARE(chosen.count(), 1);
        QVERIFY(theme.panel() != lightPanel);
        QCOMPARE(theme.accentInk(), theme.accent());
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    // Every catalog shipped in src/i18n is valid JSON, and translates only
    // strings the English side still has.
    void catalogsAreWellFormed()
    {
        // Everything the sources pass to qsTr(), tr() or translate().
        const QDir sources(QStringLiteral(SOURCE_DIR) + "/src");
        // An ordinary string, not a raw one: moc loses its place in a raw
        // string that holds quotes.
        const QRegularExpression call("(?:qsTr\\(|\\btr\\(|translate\\(\\s*\"[A-Za-z]+\",)\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        QSet<QString> known;
        for (const QString &name : sources.entryList({"*.qml", "*.cpp"}, QDir::Files)) {
            QFile file(sources.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto matches = call.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext())
                known.insert(matches.next().captured(1).replace("\\n", "\n").replace("\\\"", "\""));
        }
        QVERIFY(known.contains("Save"));

        const QDir catalogs(sources.filePath("i18n"));
        const QStringList files = catalogs.entryList({"*.json"}, QDir::Files);
        QVERIFY(files.contains("en.json"));
        for (const QString &name : files) {
            QFile file(catalogs.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            QVERIFY2(json.isObject(), qPrintable(name));
            const QJsonObject catalog = json.object();
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                QVERIFY2(known.contains(it.key()), qPrintable(name + " translates a string the app no longer has: " + it.key()));
                // A translation keeps every placeholder its English has.
                for (const QString &placeholder : {QString("%1"), QString("%2"), QString("%n")}) {
                    if (!it.key().contains(placeholder) || !it.value().isString())
                        continue;
                    QVERIFY2(it.value().toString().contains(placeholder), qPrintable(name + " drops " + placeholder + " from: " + it.key()));
                }
            }
        }
    }

private:
    static void click(Backend &backend, const QPointF &at)
    {
        backend.press(at.x(), at.y(), 0, 1);
        backend.release(at.x(), at.y(), 0, 1);
    }

    static void drag(Backend &backend, const QString &tool, const QPointF &from, const QPointF &to)
    {
        backend.setTool(tool);
        backend.press(from.x(), from.y(), 0, 1);
        backend.move(to.x(), to.y(), 0, 1);
        backend.release(to.x(), to.y(), 0, 1);
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    static QByteArray sample()
    {
        return R"({
            "chalk": 1, "paper": "light",
            "items": [
                {"id": "a", "type": "rect", "x": 0, "y": 0, "w": 160, "h": 80, "radius": 6, "text": "One", "groups": ["g1"]},
                {"id": "b", "type": "diamond", "x": 300, "y": 200, "w": 160, "h": 110, "text": "Two?", "fill": "#403e8ef7"},
                {"id": "t", "type": "text", "x": 0, "y": 200, "w": 120, "h": 30, "text": "A note", "autoSize": true},
                {"id": "p", "type": "path", "closed": false, "nodes": [
                    {"x": 0, "y": 300, "out": [10, -5]}, {"x": 60, "y": 320, "in": [-10, 5]}, {"x": 120, "y": 300}]},
                {"id": "l", "type": "line", "from": "a", "to": "b", "arrowEnd": true, "routing": "elbow", "text": "then"}
            ]
        })";
    }
};

QTEST_MAIN(ChalkTests)
#include "chalk_tests.moc"
