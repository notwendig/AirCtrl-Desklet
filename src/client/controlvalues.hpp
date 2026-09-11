/**
 * @file controlvalues.hpp
 * @brief Qt-side validation and command-line encoding of device controls.
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

QString controlValuesError(const QJsonObject& values);
QStringList controlValueArguments(const QJsonObject& values, QString* error = nullptr);
