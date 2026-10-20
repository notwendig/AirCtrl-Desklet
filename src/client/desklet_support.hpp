/**
 * @file desklet_support.hpp
 * @brief Shared presentation helpers for the Desklet translation units.
 */
#pragma once
#include "preferences.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace desklet_support {

extern const QStringList metricKeys;
extern const QStringList metricNames;

enum class MetricLevel { Unknown, Green, Yellow, Red, Blinking };
std::optional<double> metricValue(const QString& key, const QJsonValue& value);
MetricLevel metricLevel(const QString& key, const QJsonValue& value, const QJsonValue& humidityTarget,
                        const MetricLimits& limits);
QString metricText(const QString& key, const QJsonValue& value);
bool powerKnown(const QJsonObject& state);
QString endpointText(QString host, int port);
QString automationStateLabel(const QJsonObject& state);
QString automationDiagnostics(const QJsonObject& state);

} // namespace desklet_support
