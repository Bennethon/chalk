#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>

class Backend;

// The endless board. It paints the document through the same Renderer the
// exporters use, then the editor's working marks over it: selection boxes,
// handles, guides, the path being drawn. It holds the view (where the board
// is scrolled to and how far it's zoomed) and nothing else.
class CanvasView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(qreal originX READ originX NOTIFY viewChanged)
    Q_PROPERTY(qreal originY READ originY NOTIFY viewChanged)

public:
    explicit CanvasView(QQuickItem *parent = nullptr);

    QObject *editor() const;
    void setEditor(QObject *editor);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);
    qreal zoom() const { return m_zoom; }
    qreal originX() const { return m_origin.x(); }
    qreal originY() const { return m_origin.y(); }

    Q_INVOKABLE QPointF toScene(qreal x, qreal y) const;
    // Scrolls by a distance on screen.
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    // Zooms by a factor, keeping the board point under (x, y) where it is.
    Q_INVOKABLE void zoomAt(qreal x, qreal y, qreal factor);
    Q_INVOKABLE void zoomBy(qreal factor);
    Q_INVOKABLE void resetZoom();
    // Brings everything on the board into view, never past full size.
    Q_INVOKABLE void fitContent();

    void paint(QPainter *painter) override;

signals:
    void editorChanged();
    void accentChanged();
    void viewChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void paintGrid(QPainter *painter) const;
    void paintOverlay(QPainter *painter) const;

    QPointer<Backend> m_backend;
    QColor m_accent = QColor("#FFD60A");
    qreal m_zoom = 1;
    // The board point at the view's top left corner.
    QPointF m_origin;
    // Until the view is first sized, or its content first fitted, there's
    // nothing to keep in place.
    bool m_placed = false;
    bool m_fitPending = false;
};
