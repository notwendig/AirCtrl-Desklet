#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Validates the public device-control contract shared by the UI and Lua.
// An empty string means valid. A single backend request cannot mix integer
// fields with string/boolean fields because the Philips wire encoding applies
// one value family to the complete request.
QString controlValuesError(const QJsonObject& values);
QStringList controlValueArguments(const QJsonObject& values, QString* error = nullptr);
