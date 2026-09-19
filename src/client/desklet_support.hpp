/**
 * @file desklet_support.hpp
 * @brief Shared presentation helpers for the Desklet translation units.
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace desklet_support {

extern const QStringList metricKeys;
extern const QStringList metricNames;

QString metricText(const QString& key, const QJsonValue& value);
bool powerKnown(const QJsonObject& state);
QString endpointText(QString host, int port);
QString automationStateLabel(const QJsonObject& state);
QString automationDiagnostics(const QJsonObject& state);

} // namespace desklet_support
