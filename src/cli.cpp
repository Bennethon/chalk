#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "document.h"
#include "exporter.h"
#include "renderer.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) {
    fputs(QJsonDocument(object).toJson(QJsonDocument::Indented).constData(), stdout);
}

// Errors go to stderr either way; with --json the same message is also the
// result on stdout, so a caller reading one stream gets the whole answer.
int fail(const QString &message, bool json = false) {
    print(stderr, message);
    if (json)
        print(QJsonObject{{"ok", false}, {"error", message}});
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, bool board = true) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        if (board)
            parser.addPositionalArgument("board", "Chalk board", "<board>");
        parser.addOption({"json", "Print the result as JSON"});
    }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    bool json() const { return parser.isSet("json"); }
    bool load(Document *document, QStringList *warnings, QString *error) const {
        if (argument(1).isEmpty()) {
            *error = "Name a Chalk board.";
            return false;
        }
        QFile file(argument(1));
        if (!file.open(QIODevice::ReadOnly)) {
            *error = argument(1) + ": " + file.errorString();
            return false;
        }
        QString problem;
        if (!Document::parse(file.readAll(), document, &problem, warnings)) {
            *error = argument(1) + ": " + problem;
            return false;
        }
        return true;
    }
};

int check(const QStringList &arguments) {
    Command command("check", "Check that a board loads, and report what's on it.");
    command.parser.process(arguments);
    Document document;
    QStringList warnings;
    QString error;
    if (!command.load(&document, &warnings, &error))
        return fail(error, command.json());

    int shapes = 0, lines = 0, paths = 0;
    QRectF area;
    bool first = true;
    for (const Item &item : document.items) {
        (item.type == Item::Line ? lines : item.type == Item::Path ? paths : shapes)++;
        const QRectF box = Renderer::paintBounds(document, item);
        area = first ? box : area.united(box);
        first = false;
    }
    if (command.json()) {
        print(QJsonObject{{"ok", true}, {"board", command.argument(1)}, {"paper", document.paper},
                          {"items", int(document.items.size())}, {"shapes", shapes}, {"lines", lines},
                          {"paths", paths}, {"x", area.x()}, {"y", area.y()}, {"width", area.width()},
                          {"height", area.height()}, {"warnings", QJsonArray::fromStringList(warnings)}});
    } else {
        print(stdout, QString("%1: %2 items (%3 shapes, %4 lines, %5 paths), %6 x %7, %8 paper")
                          .arg(command.argument(1)).arg(document.items.size()).arg(shapes).arg(lines).arg(paths)
                          .arg(qRound(area.width())).arg(qRound(area.height())).arg(document.paper));
        for (const QString &warning : warnings)
            print(stdout, "warning: " + warning);
    }
    return 0;
}

int exportBoard(const QStringList &arguments) {
    Command command("export", "Export a board as PNG, SVG, PDF or JPG. The output's extension picks the format.");
    command.parser.addPositionalArgument("output", "File to write", "<output>");
    command.parser.addOption({"scale", "Pixels per board unit for PNG and JPG (default 2)", "factor", "2"});
    command.parser.addOption({"transparent", "Leave out the paper behind the drawing (PNG, SVG, PDF)"});
    command.parser.process(arguments);
    Document document;
    QStringList warnings;
    QString error;
    if (!command.load(&document, &warnings, &error))
        return fail(error, command.json());
    if (command.argument(2).isEmpty())
        return fail("Name a file to export to, such as board.png.", command.json());

    Exporter::Options options;
    bool number = false;
    options.scale = command.parser.value("scale").toDouble(&number);
    if (!number || options.scale <= 0)
        return fail("--scale takes a number above zero.", command.json());
    options.background = !command.parser.isSet("transparent");
    if (!Exporter::write(document, command.argument(2), options, &error))
        return fail(error, command.json());

    const QRectF area = Exporter::area(document, options);
    if (command.json())
        print(QJsonObject{{"ok", true}, {"output", QFileInfo(command.argument(2)).absoluteFilePath()},
                          {"width", area.width()}, {"height", area.height()},
                          {"warnings", QJsonArray::fromStringList(warnings)}});
    else
        print(stdout, "Exported " + command.argument(2));
    return 0;
}

int printResource(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    fputs(file.readAll().constData(), stdout);
    return 0;
}

// One shared copy, linked into Claude Code and found directly by other agents.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.", false);
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/chalk and link into ~/.claude/skills", "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use chalk skill to print the skill, or chalk skill install.", command.json());
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/chalk");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName(), command.json());
    QString linked;
    if (home.exists(".claude")) {
        const QString link = home.filePath(".claude/skills/chalk");
        const QFileInfo existing(link);
        if (existing.isSymLink())
            QFile::remove(link);
        else if (existing.exists())
            return fail(link + " exists and is not a link; leaving it alone.", command.json());
        if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/chalk", link))
            return fail("Could not link " + link, command.json());
        linked = link;
    }
    if (command.json()) {
        print(QJsonObject{{"ok", true}, {"installed", file.fileName()}, {"linked", linked}});
    } else {
        print(stdout, "Installed " + file.fileName());
        if (!linked.isEmpty())
            print(stdout, "Linked " + linked);
    }
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (topic == "format")
        return printResource(":/format.md");
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments.value(0), topic, "--help"});
    print(stdout, "Usage: chalk <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Editor:\n"
           "  open [board]              Open the editor, on the last board by default\n\n"
           "Commands that need no display (chalk help <command> for options):\n"
           "  check <board>             Check that a board loads, and report what's on it\n"
           "  export <board> <output>   Export PNG, SVG, PDF or JPG\n"
           "  help format               How a board file is written\n"
           "  skill [install]           Print the skill for coding agents, or install it\n\n"
           "Every command takes --json, exits 0 on success and 1 on failure, and\n"
           "writes errors to stderr.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"check", "export", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "check"    ? check(arguments)
           : command == "export" ? exportBoard(arguments)
           : command == "skill"  ? skill(arguments)
                                 : help(arguments);
}
