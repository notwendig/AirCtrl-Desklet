#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

struct DiagnosticField {
    QString tag;
    QString value;
    QString description;
    QString hex = {};
};

QList<DiagnosticField> describeDeviceFields(const QJsonObject& status);
QString diagnosticFieldReport(const QList<DiagnosticField>& fields);
