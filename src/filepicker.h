#pragma once

#include <QObject>
#include <QUrl>

// The open, save and export dialogs, behind an interface so tests can answer
// them without a desktop. The pattern follows Omacut (MIT, David Heinemeier
// Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openBoard(const QString &folder) = 0;
    virtual void saveBoard(const QUrl &suggestedUrl) = 0;
    // format is a file extension: png, svg, pdf or jpg.
    virtual void exportImage(const QUrl &suggestedUrl, const QString &format) = 0;

signals:
    void openSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void exportSelected(const QUrl &url);
    void canceled();
    void failed(const QString &message);
};
