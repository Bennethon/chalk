#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QLineF>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

#include "document.h"

class FilePicker;

// The editor: the board, what's selected, the tool in hand, and everything
// that changes them. QML forwards the pointer here in board coordinates and
// draws what this reports; nothing about the board is decided in QML.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QVariantMap style READ style NOTIFY styleChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY styleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY styleChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QString paper READ paper WRITE setPaper NOTIFY styleChanged)
    // The paper a new board starts on: the app's own light or dark.
    Q_PROPERTY(QString defaultPaper READ defaultPaper WRITE setDefaultPaper NOTIFY styleChanged)
    Q_PROPERTY(QString editingId READ editingId NOTIFY editingChanged)
    Q_PROPERTY(QVariantMap editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool nodeEditing READ nodeEditing NOTIFY modeChanged)
    Q_PROPERTY(bool penActive READ penActive NOTIFY modeChanged)
    Q_PROPERTY(int cursor READ cursor NOTIFY cursorChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);

    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    QVariantMap style() const { return m_style; }
    int selectionCount() const { return m_selection.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool dirty() const { return m_dirty; }
    bool empty() const { return m_doc.items.isEmpty(); }
    QString filePath() const { return m_filePath; }
    QString fileName() const;
    QString paper() const { return m_doc.paper; }
    void setPaper(const QString &paper);
    QString defaultPaper() const { return m_defaultPaper; }
    void setDefaultPaper(const QString &paper);
    QString editingId() const { return m_editingId; }
    QVariantMap editing() const;
    bool nodeEditing() const { return !m_nodeEditId.isEmpty(); }
    bool penActive() const { return !m_penId.isEmpty(); }
    int cursor() const { return m_cursor; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }

    // What the canvas paints over the board.
    const Document &document() const { return m_doc; }
    QStringList selection() const { return m_selection; }
    QRectF selectionBounds() const;
    QRectF contentBounds() const;
    bool resizable() const;
    QRectF marquee() const { return m_marquee; }
    QList<QLineF> guides() const { return m_guides; }
    // The equal gaps a move has caught on, each drawn as a measured span.
    QList<QLineF> spacers() const { return m_spacers; }
    // How far the thing being moved or resized is from its nearest
    // neighbour on each side, while that's close enough to matter.
    QList<QLineF> distances() const { return m_distances; }
    QString hoverId() const { return m_hoverId; }
    QString attachId() const { return m_attachId; }
    QString nodeEditId() const { return m_nodeEditId; }
    int activeNode() const { return m_node; }
    QString penId() const { return m_penId; }
    bool hasPenPreview() const { return m_penHasPreview; }
    QPointF penPreview() const { return m_penPreview; }
    QList<QPointF> pencilPoints() const { return m_pencil; }
    static QList<QPointF> handlePoints(const QRectF &bounds);

    // The pointer, in board coordinates. `zoom` sizes the grab distances,
    // which are constant on screen.
    Q_INVOKABLE void press(qreal x, qreal y, int modifiers, qreal zoom);
    Q_INVOKABLE void move(qreal x, qreal y, int modifiers, qreal zoom);
    Q_INVOKABLE void release(qreal x, qreal y, int modifiers, qreal zoom);
    Q_INVOKABLE void doubleClick(qreal x, qreal y, int modifiers, qreal zoom);
    Q_INVOKABLE void hover(qreal x, qreal y, qreal zoom);
    // A right-click: what's under it becomes the selection the menu acts on.
    Q_INVOKABLE void contextAt(qreal x, qreal y, qreal zoom);
    // Escape backs out of one thing at a time; Enter goes into one.
    Q_INVOKABLE void escape();
    Q_INVOKABLE void enter();

    Q_INVOKABLE void beginEditing(const QString &id);
    Q_INVOKABLE void commitText(const QString &text);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste(qreal x, qreal y);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void nudge(qreal dx, qreal dy);
    Q_INVOKABLE void group();
    Q_INVOKABLE void ungroup();
    // front, forward, backward, back
    Q_INVOKABLE void order(const QString &direction);
    // left, hcenter, right, top, vcenter, bottom
    Q_INVOKABLE void align(const QString &edge);
    // Evens out the gaps along "h" or "v", keeping the outer two in place.
    Q_INVOKABLE void distribute(const QString &axis);
    // Sets every gap along "h" or "v" to an exact size.
    Q_INVOKABLE void setGap(const QString &axis, qreal gap);
    Q_INVOKABLE void setStyle(const QString &key, const QVariant &value);
    // x, y, w, h of the selection as a whole.
    Q_INVOKABLE void setGeometry(const QString &key, qreal value);
    Q_INVOKABLE QColor colorOf(const QString &name) const;

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool load(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE bool saveTo(const QString &path);
    Q_INVOKABLE void chooseOpen();
    Q_INVOKABLE void chooseSave();
    // options: format, scale, background, selectionOnly
    Q_INVOKABLE void chooseExport(const QVariantMap &options);
    Q_INVOKABLE bool exportTo(const QString &path, const QVariantMap &options);
    Q_INVOKABLE void copyImage();
    Q_INVOKABLE void clearMessage();

signals:
    // Something the canvas shows moved; repaint.
    void changed();
    void styleChanged();
    void toolChanged();
    void historyChanged();
    void dirtyChanged();
    void fileChanged();
    void editingChanged();
    void modeChanged();
    void cursorChanged();
    void messageChanged();
    void loaded();
    void saved();
    // A save that was asked for didn't happen: it failed, or its dialog was closed.
    void saveAbandoned();

private:
    enum class Drag {
        None, PendingMove, Move, Resize, Marquee, CreateShape, CreateLine, LineEnd, Node, Handle, PenHandle, Pencil
    };
    struct Snapshot {
        Document doc;
        QStringList selection;
    };

    void pressSelect(const QPointF &pos, int modifiers);
    void pressPen(QPointF pos, int modifiers);
    void finishPen();
    void finishPencil();
    void finishCreateShape(const QPointF &pos);
    void leaveNodeEdit();
    void deleteNode();
    void insertNode(Item &path, const QPointF &pos);

    Item newItem(Item::Type type) const;
    void fitText(Item &item) const;
    void applyStyle(Item &item, const QString &key, const QVariant &value, bool force) const;
    QStringList unitIds(const Item &item) const;
    QList<QStringList> selectedUnits(bool movableOnly) const;
    QRectF unitBounds(const QStringList &ids) const;
    void translate(const QStringList &ids, const QPointF &delta);
    QList<Item> clones(const QStringList &ids) const;
    void insert(QList<Item> items, const QPointF &center, bool centered);
    QStringList addCopies(QList<Item> items, const QPointF &point, bool centered);
    QString attachTarget(const QPointF &pos, const QString &exclude, int *side) const;
    QPointF snap(const QRectF &moving);
    QList<QRectF> neighbours() const;
    bool snapEdge(bool horizontal, qreal &edge, QRectF *target) const;
    void measure(const QRectF &box);
    void clearMarks();
    int handleAt(const QPointF &pos, qreal tolerance) const;

    void setSelection(const QStringList &ids);
    void pushUndo(const Snapshot &snapshot);
    void checkpoint();
    // One undo step for a burst of the same change (a held arrow key, a
    // number being stepped), rather than one per repeat.
    void checkpointFor(const QString &key);
    void beginChange();
    void setDirty(bool dirty);
    void setCursor(int cursor);
    void say(const QString &message, bool error = false);
    void refresh();
    void restore(const Snapshot &snapshot);
    QString suggestedPath(const QString &extension) const;

    FilePicker *m_picker = nullptr;
    Document m_doc;
    QStringList m_selection;
    QString m_tool = "select";
    QString m_defaultPaper = "dark";
    Item m_defaults;
    QVariantMap m_style;

    QList<Snapshot> m_undo, m_redo;
    QString m_coalesceKey;
    QElapsedTimer m_coalesceTimer;
    bool m_dirty = false;
    QString m_filePath;

    Drag m_drag = Drag::None;
    qreal m_zoom = 1;
    QPointF m_press;
    Document m_dragOrigin;
    QStringList m_dragOriginSelection;
    bool m_dragChanged = false;
    QRectF m_originBounds;
    int m_handle = -1;
    QStringList m_clickedUnit;
    QString m_clickedId;
    bool m_clickedWasSelected = false;
    QString m_activeId;
    bool m_lineStart = false;
    QString m_startAttach;
    int m_startSide = 0;
    QRectF m_marquee;
    QStringList m_marqueeBase;
    QList<QLineF> m_guides, m_spacers, m_distances;
    QString m_hoverId, m_attachId;

    QString m_nodeEditId;
    int m_node = -1;
    int m_handleKind = 0;
    bool m_mirrorHandles = false;

    QString m_penId;
    QPointF m_penPreview;
    bool m_penHasPreview = false;
    QList<QPointF> m_pencil;

    QString m_editingId;
    // Set while the text of a just-made item is being typed: committing it
    // empty takes the item, and its undo step, away again.
    int m_newItemUndoDepth = -1;

    int m_cursor = Qt::ArrowCursor;
    QString m_message;
    bool m_messageIsError = false;
    QVariantMap m_exportOptions;
};
