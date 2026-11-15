#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // Light or dark. It follows the Omarchy theme until the person picks
    // one themselves, and every color below follows it.
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
    Q_PROPERTY(QString accentInk READ accentInk NOTIFY changed)
    Q_PROPERTY(QString window READ window NOTIFY changed)
    Q_PROPERTY(QString panel READ panel NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString field READ field NOTIFY changed)
    Q_PROPERTY(QString fieldBorder READ fieldBorder NOTIFY changed)
    Q_PROPERTY(QString hover READ hover NOTIFY changed)
    Q_PROPERTY(QString pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString strong READ strong NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString dialog READ dialog NOTIFY changed)
    Q_PROPERTY(QString dialogBorder READ dialogBorder NOTIFY changed)
    Q_PROPERTY(QString button READ button NOTIFY changed)
    Q_PROPERTY(QString buttonHover READ buttonHover NOTIFY changed)
    Q_PROPERTY(QString buttonDown READ buttonDown NOTIFY changed)
    Q_PROPERTY(QString scrim READ scrim NOTIFY changed)
    Q_PROPERTY(QString helpScrim READ helpScrim NOTIFY changed)
    Q_PROPERTY(QString errorFill READ errorFill NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    bool dark() const { return m_chosen < 0 ? m_themeDark : m_chosen == 1; }
    void setDark(bool dark);
    QString accentInk() const;
    QString window() const { return dark() ? "#0e0e10" : "#dedcd6"; }
    QString panel() const { return dark() ? "#161618" : "#f1efea"; }
    QString border() const { return dark() ? "#2a2a2e" : "#d4d2cb"; }
    QString field() const { return dark() ? "#1f1f23" : "#fbfaf7"; }
    QString fieldBorder() const { return dark() ? "#333338" : "#c6c4bd"; }
    QString hover() const { return dark() ? "#2a2a2f" : "#e4e2dc"; }
    QString pressed() const { return dark() ? "#3a3a40" : "#d4d2cb"; }
    QString text() const { return dark() ? "#e6e6ea" : "#2a2a2e"; }
    QString strong() const { return dark() ? "#ffffff" : "#111114"; }
    QString muted() const { return dark() ? "#8a8a93" : "#7a7a82"; }
    QString label() const { return dark() ? "#c8c8ce" : "#4a4a50"; }
    QString dialog() const { return dark() ? "#1c1c1e" : "#f7f6f2"; }
    QString dialogBorder() const { return dark() ? "#3a3a3e" : "#c6c4bd"; }
    QString button() const { return dark() ? "#2c2c2f" : "#e6e4de"; }
    QString buttonHover() const { return dark() ? "#35353a" : "#dad8d1"; }
    QString buttonDown() const { return dark() ? "#3c3c40" : "#cccac3"; }
    QString scrim() const { return dark() ? "#cc000000" : "#73000000"; }
    QString helpScrim() const { return dark() ? "#f2000000" : "#f5f1efea"; }
    QString errorFill() const { return dark() ? "#3a1518" : "#fbe4e4"; }
    QString errorText() const { return dark() ? "#ffd7d9" : "#8a1c20"; }

    static QString readAccent(const QString &path);
    static bool readDark(const QString &path);

signals:
    void changed();
    void darkChosen(bool dark);

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    bool m_themeDark = true;
    // -1 until the person picks: then 0 for light, 1 for dark.
    int m_chosen = -1;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
