/**
 * @file controlvalues.hpp
 * @brief Validation and command-line encoding of supported device controls.
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @brief Validate the public control contract shared by UI, Lua, and CLI.
 * @param values Device fields and requested target values.
 * @return An empty string when valid, otherwise a user-facing error message.
 *
 * A request cannot mix integer fields with string or Boolean fields because
 * the Philips wire encoding applies one value family to the whole request.
 */
QString controlValuesError(const QJsonObject& values);

/**
 * @brief Convert validated control values into backend `name=value` arguments.
 * @param values Device fields and requested target values.
 * @param error Optional destination for a validation error.
 * @return Encoded arguments, or an empty list if validation failed.
 */
QStringList controlValueArguments(const QJsonObject& values, QString* error = nullptr);
