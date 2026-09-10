/**
 * @file controlvalues.cpp
 * @brief Shared allow-list and value-family validation for control commands.
 */
#include "controlvalues.hpp"

#include <QJsonValue>

QString controlValuesError(const QJsonObject& values) {
    if (values.isEmpty()) return "Leerer Steuerauftrag.";
    const bool integers=values.begin().value().isDouble();
    for(auto i=values.begin();i!=values.end();++i) {
        const auto value=i.value();
        const auto text=value.toString();
        const auto number=value.toDouble(-1);
        const bool valid=
            (i.key()=="pwr" && value.isString() && (text=="0" || text=="1")) ||
            (i.key()=="cl" && value.isBool()) ||
            (i.key()=="mode" && value.isString() && QStringList{"P","A","S","M"}.contains(text)) ||
            (i.key()=="om" && value.isString() && QStringList{"1","2","3","s","t"}.contains(text)) ||
            (i.key()=="func" && value.isString() && (text=="P" || text=="PH")) ||
            (i.key()=="uil" && value.isString() && (text=="0" || text=="1")) ||
            (i.key()=="rhset" && value.isDouble() && (number==40 || number==50 || number==60 || number==70)) ||
            (i.key()=="aqil" && value.isDouble() && (number==0 || number==25 || number==50 || number==75 || number==100)) ||
            (i.key()=="dt" && value.isDouble() && number>=0 && number<=12 && number==int(number));
        if(!valid) return "Ungültiger Steuerwert: "+i.key();
        if(value.isDouble()!=integers)
            return "Ein Steuerauftrag darf Ganzzahlen nicht mit Text- oder Boolean-Werten mischen.";
    }
    return {};
}

QStringList controlValueArguments(const QJsonObject& values, QString* error) {
    const auto problem=controlValuesError(values);
    if(error) *error=problem;
    if(!problem.isEmpty()) return {};
    const bool integers=values.begin().value().isDouble();
    QStringList args{"set"};
    if(integers) args<<"-I";
    for(auto i=values.begin();i!=values.end();++i) {
        const auto value=i.value();
        const auto encoded=value.isBool() ? (value.toBool() ? "true" : "false") :
            integers ? QString::number(value.toInt()) : value.toString();
        args<<i.key()+"="+encoded;
    }
    return args;
}
