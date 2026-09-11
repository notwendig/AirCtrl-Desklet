/**
 * @file diagnostics.hpp
 * @brief Human-readable descriptions of raw AC2729 status fields.
 */
#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

/** @brief One unchanged device value plus its local interpretation. */
struct DiagnosticField {
    QString tag;
    QString value;
    QString description;
    QString hex = {};
};

/** @brief Describe every field while preserving its original value. */
QList<DiagnosticField> describeDeviceFields(const QJsonObject& status);

/** @brief Render diagnostic fields as a copyable plain-text report. */
QString diagnosticFieldReport(const QList<DiagnosticField>& fields);
