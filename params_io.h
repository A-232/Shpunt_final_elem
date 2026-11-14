#ifndef PARAMS_IO_H
#define PARAMS_IO_H

#include <QString>
#include <string>
#include "shpunt_calc.h"

// Полный комплект исходных данных МКЭ-расчёта: Params + схема + параметры
// конкретной шпунтовой конструкции + границы перебора d.
struct FullParams {
    Params      P;                // общие данные шпунтовой стенки, грунта, опор
    std::string scheme;           // console / strut / ring / frame
    Strut       strut;
    Ring        ring;
    Frame       frame;
    double      d_min = 0.25;
    double      d_max = 12.0;
    double      d_step = 0.25;
};

namespace ParamsIO {
FullParams defaults();
bool loadFromFile(const QString& filename, FullParams& fp, QString& err);
bool saveToFile (const QString& filename, const FullParams& fp, QString& err);
}

#endif