#ifndef SHPUNT_CALC_H
#define SHPUNT_CALC_H

#include <QString>
#include <string>
#include <vector>

// Опорная связь (распорка / обвязка), которая прикладывается в узле стенки
struct Support {
    double z;          // глубина установки от верха стенки, м
    double k;          // жёсткость опоры (условная), кН/м
    double lim;        // предельное усилие, кН/м
    bool oneSided;     // работает только на сжатие (отпор), не на растяжение
};

// Все исходные данные расчёта
struct Params {
    double t;          // шаг разбиения МКЭ, м
    double E;          // модуль упругости стали, кПа
    double I;          // момент инерции профиля стенки, м^4/м
    double W;          // момент сопротивления профиля стенки, м^3/м
    double Ry;         // расчётное сопротивление стали, кПа
    double gamma;      // удельный вес грунта, кН/м³
    double phi;        // угол внутреннего трения, град
    double Kpost;      // коэффициент постели, кН/м⁴
    double q;          // пригрузка на поверхности, кПа
    double h;          // глубина котлована (высота стенки над дном), м
    int steps;         // число шагов нагружения
    std::vector<Support> supports; // список опор (пусто для консоли!)
};

// Параметры распорки (для scheme = strut)
struct Strut { double A, I, L, s; int nMid; };

// Параметры круговой обвязки (для scheme = ring)
struct Ring { double A, R; };

// Параметры прямоугольной обвязки (для scheme = frame)
// ВАЖНО: добавлено поле I — без момента инерции нельзя проверить
// устойчивость сжатого пояса на весь пролёт B
struct Frame { double B, A, W, I, Ry; };

// Главная функция расчёта. Возвращает QString с готовым текстом результата.
QString calculate(const Params& P, const std::string& scheme,
                  const Strut& strut, const Ring& ring, const Frame& frame,
                  double d_min, double d_max, double d_step);

#endif