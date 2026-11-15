#pragma once

#include <QString>
#include <QStringList>

// The commands that need no display: checking a board, exporting it, and
// handing coding agents the skill for writing one. The shape follows Hype's
// cli.cpp (MIT, David Heinemeier Hansson).
QString cliSummary();
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
