#include "shpunt_calc.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

using namespace std;

const double PI = 3.14159265358979;

// ==================================================================
//  Решение СЛАУ методом Гаусса с выбором главного элемента
// ==================================================================
static bool gauss(vector<vector<double>> A, vector<double> b, vector<double>& x) {
    int n = (int)A.size();
    for (int c = 0; c < n; ++c) {
        // ищем строку с максимальным элементом в столбце
        int p = c;
        for (int r = c+1; r < n; ++r)
            if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        if (fabs(A[p][c]) < 1e-12) return false;   // вырожденная матрица
        swap(A[p], A[c]); swap(b[p], b[c]);
        // прямой ход
        for (int r = c+1; r < n; ++r) {
            double m = A[r][c]/A[c][c];
            for (int j = c; j < n; ++j) A[r][j] -= m*A[c][j];
            b[r] -= m*b[c];
        }
    }
    // обратный ход
    x.assign(n, 0.0);
    for (int r = n-1; r >= 0; --r) {
        double s = b[r];
        for (int j = r+1; j < n; ++j) s -= A[r][j]*x[j];
        x[r] = s/A[r][r];
    }
    return true;
}

// ==================================================================
//  Вектор узловых нагрузок от активного давления грунта
//  Треугольная/трапецеидальная эпюра на каждом элементе
// ==================================================================
static vector<double> calcLoads(const Params& P, double Ka, int ndof, int n,
                                double t, double dredge, double frac) {
    vector<double> F(ndof, 0.0);
    for (int e = 0; e < n-1; ++e) {
        double z1 = e*t, z2 = (e+1)*t;
        if (z1 >= dredge) continue;               // ниже дна котлована — нагрузки нет
        double L = min(z2, dredge) - z1;
        double p1 = P.gamma*z1*Ka + P.q*Ka;        // давление в начале элемента
        double p2 = P.gamma*min(z2, dredge)*Ka + P.q*Ka;
        // Стандартные формулы для треугольной эпюры на балочном КЭ:
        F[2*e]   += (7*p1 + 3*p2)/20*L;            // поперечная сила, узел e
        F[2*e+1] += -L*L*(p1/20 + p2/30);          // момент, узел e
        F[2*e+2] += (3*p1 + 7*p2)/20*L;            // поперечная сила, узел e+1
        F[2*e+3] +=  L*L*(p1/30 + p2/20);          // момент, узел e+1
    }
    // frac — доля приложенной нагрузки (для пошагового нагружения)
    for (auto& f : F) f *= frac;
    return F;
}

// ==================================================================
//  Результат для одной глубины заделки d
// ==================================================================
struct Result {
    bool ok = false;              // сошлось ли решение
    double uTop = 0;              // прогиб верха стенки, м
    double Mmax = 0;              // макс. изгибающий момент, кН·м
    double zM = 0;                // глубина, где Mmax
    double maxSoilRatio = 0;      // max(p/p_пред) — использование отпора грунта
    vector<double> R;             // реакции в опорах, кН/м (по одной на опору)
};

// ==================================================================
//  Основной решатель для конкретной глубины заделки d
// ==================================================================
static Result solveForD(const Params& P, double d) {
    double Ka = pow(tan(PI/4 - P.phi*PI/360), 2), Kp = 1.0/Ka;

    // Количество узлов и фактический шаг по длине
    int n = (int)round((P.h + d) / P.t) + 1;
    if (n < 2) n = 2;
    double t = (P.h + d) / (n - 1);

    int ndof = 2*n;                    // 2 степени свободы на узел (v, θ)
    double dredge = P.h, EI = P.E*P.I;

    // --- узлы ниже дна котлована (там работает отпор грунта) ---
    vector<int> sub; vector<double> z0;
    for (int i = 0; i < n; ++i)
        if (i*t > dredge + 1e-9) { sub.push_back(i); z0.push_back(i*t - dredge); }

    // --- узлы, где стоят опоры (распорки/обвязки) ---
    vector<int> snode;
    for (auto& s : P.supports) {
        int nodeIdx = (int)round(s.z / t);
        if (nodeIdx < 0 || nodeIdx >= n) continue;
        snode.push_back(nodeIdx);
    }

    // --- глобальная матрица жёсткости упругой балки ---
    vector<vector<double>> K0(ndof, vector<double>(ndof, 0.0));
    for (int e = 0; e < n-1; ++e) {
        double Le = t;
        double ce = EI / (Le*Le*Le);
        double ke[4][4] = {
            { 12*ce,     6*Le*ce,     -12*ce,      6*Le*ce },
            { 6*Le*ce,  4*Le*Le*ce,     -6*Le*ce,  2*Le*Le*ce },
            {-12*ce,    -6*Le*ce,      12*ce,     -6*Le*ce },
            { 6*Le*ce,  2*Le*Le*ce,     -6*Le*ce,  4*Le*Le*ce }
        };
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) K0[2*e+a][2*e+b] += ke[a][b];
    }

    // Состояние нелинейных элементов (отпор грунта и опоры)
    vector<bool> isFrozen(n, false); vector<double> frozenF(n, 0.0);
    vector<bool> supOff(P.supports.size(), false), supFrz(P.supports.size(), false);
    vector<double> supF(P.supports.size(), 0.0), u;
    bool collapsed = false;
    int steps = std::max(1, P.steps);

    // Пошаговое нагружение
    for (int step = 1; step <= steps && !collapsed; ++step) {
        auto F = calcLoads(P, Ka, ndof, n, t, dredge, (double)step/steps);
        // Внутренние итерации: уточняем состояние грунта и опор
        for (int it = 0; it < 300 && !collapsed; ++it) {
            auto K = K0;
            // Отпор грунта — добавляем пружину в узлы ниже дна
            // ИСПРАВЛЕНО: убран квадрат z0[j]*z0[j], оставлен линейный рост K(z) = K*z*t
            for (size_t j = 0; j < sub.size(); ++j)
                if (!isFrozen[sub[j]]) K[2*sub[j]][2*sub[j]] += P.Kpost*z0[j]*t;
            // Упругая опора (пока не «заморожена» и не отключена)
            for (size_t si = 0; si < snode.size(); ++si)
                if (!supOff[si] && !supFrz[si]) K[2*snode[si]][2*snode[si]] += P.supports[si].k;

            // Вектор правой части: внешняя нагрузка + замороженные реакции
            auto Ftot = F;
            for (int i = 0; i < n; ++i) if (isFrozen[i]) Ftot[2*i] += frozenF[i];
            for (size_t si = 0; si < snode.size(); ++si)
                if (supFrz[si]) Ftot[2*snode[si]] += supF[si];

            // ИСПРАВЛЕНО: убрана искусственная микро-добавка жёсткости.
            if (!gauss(K, Ftot, u)) { collapsed = true; break; }
            if (fabs(u[0]) > 10.0)  { collapsed = true; break; } // явная потеря устойчивости

            bool changed = false;

            // Проверка отпора грунта: не превышает ли он предельный пассивный
            for (size_t j = 0; j < sub.size(); ++j) {
                int i = sub[j];
                if (isFrozen[i]) continue;
                double f = P.Kpost*z0[j]*t*u[2*i];
                // ИСПРАВЛЕНО: предельное давление только по Kp (пассивное)
                double lim = max(0.0, P.gamma*z0[j]*Kp)*t;
                if (fabs(f) > lim + 1e-9) {
                    isFrozen[i] = true;
                    frozenF[i] = (f > 0 ? -lim : lim);
                    changed = true;
                }
            }

            // Проверка опор: предельное усилие и односторонняя работа
            for (size_t si = 0; si < snode.size(); ++si) {
                double v = u[2*snode[si]];
                if (supFrz[si]) {
                    if (fabs(v) > 1e-12) supF[si] = (v > 0 ? -P.supports[si].lim : P.supports[si].lim);
                    if (v <= 0) { supFrz[si] = false; supOff[si] = true; changed = true; }
                    continue;
                }
                if (supOff[si]) {
                    if (v > 0) { supOff[si] = false; changed = true; }
                    continue;
                }
                if (P.supports[si].oneSided && v <= 0) {
                    supOff[si] = true; supF[si] = 0.0; changed = true; continue;
                }
                double f = P.supports[si].k*v;
                if (fabs(f) > P.supports[si].lim + 1e-9) {
                    supFrz[si] = true;
                    supF[si] = (f > 0 ? -P.supports[si].lim : P.supports[si].lim);
                    changed = true;
                }
            }
            // ИСПРАВЛЕНО: добавлен выход из цикла при сходимости
            if (!changed) break;
        }
    }

    Result r; r.ok = !collapsed;
    if (!r.ok) return r;
    r.uTop = u[0]; r.Mmax = 0; r.zM = 0; r.maxSoilRatio = 0;

    // Поиск максимума изгибающего момента по длине стенки
    // ИСПРАВЛЕНО: ищем максимальный по модулю, но сохраняем истинный знак
    double maxAbsM = 0;
    for (int e = 0; e < n-1; ++e)
        for (int k = 0; k <= 20; ++k) {
            double xi = k/20.0;
            double N2[4] = { (6-12*xi)/(t*t), (4-6*xi)/t, (-6+12*xi)/(t*t), (2-6*xi)/t };
            double Mx = -EI*(u[2*e]*N2[0] + u[2*e+1]*N2[1] + u[2*e+2]*N2[2] + u[2*e+3]*N2[3]);
            if (fabs(Mx) > maxAbsM) {
                maxAbsM = fabs(Mx);
                r.Mmax = Mx; // сохраняем со знаком
                r.zM = e*t + xi*t;
            }
        }

    // Использование отпора грунта (для проверки по грунту)
    for (size_t j = 0; j < sub.size(); ++j) {
        double f = P.Kpost*z0[j]*t*u[2*sub[j]];
        double lim = max(0.0, P.gamma*z0[j]*Kp)*t;
        if (lim > 1e-9) r.maxSoilRatio = max(r.maxSoilRatio, fabs(f)/lim);
    }

    // Реакции в опорах
    for (size_t si = 0; si < snode.size(); ++si)
        r.R.push_back(supFrz[si] ? -supF[si] : P.supports[si].k*u[2*snode[si]]);
    return r;
}

// ==================================================================
//  Главная функция: перебор d и проверки схемы
// ==================================================================
QString calculate(const Params& P, const std::string& scheme,
                  const Strut& strut, const Ring& ring, const Frame& frame,
                  double d_min, double d_max, double d_step)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);

    // Красивое название схемы для вывода
    std::string schemeDisplay = scheme;
    if      (scheme == "console") schemeDisplay = "консоль";
    else if (scheme == "strut")   schemeDisplay = "распорка";
    else if (scheme == "ring")    schemeDisplay = "круговая обвязка";
    else if (scheme == "frame")   schemeDisplay = "прямоугольная обвязка";

    double Ka = pow(tan(PI/4 - P.phi*PI/360), 2), Kp = 1.0/Ka;
    double Mcap = P.Ry*P.W;      // несущая способность стенки по моменту
    double limDef = P.h / 150.0; // нормативный предельный прогиб

    // -------- шапка отчёта --------
    out << "============================================================\n";
    out << "  РАСЧЁТ ШПУНТОВОЙ СТЕНКИ (инженерный метод МКЭ)\n";
    out << "============================================================\n";
    out << "Схема: " << schemeDisplay << "\n";
    out << "H = " << P.h << " м; γ = " << P.gamma << " кН/м³; φ = " << P.phi
        << "°; K = " << P.Kpost << " кН/м⁴; q = " << P.q << " кПа\n";
    out << "Ka = " << Ka << "; Kp = " << Kp << "\n";
    out << "Профиль: I = " << P.I << " м⁴/м; W = " << P.W << " м³/м; Ry = " << P.Ry << " кПа\n";
    out << "Перебор d: от " << d_min << " до " << d_max << " м с шагом " << d_step << " м\n";
    out << "------------------------------------------------------------\n\n";
    out << ">>> Итерационный подбор d\n\n";

    // -------- перебор глубины заделки --------
    bool found = false;
    for (double d = d_min; d <= d_max + 1e-9; d += d_step) {
        Result r = solveForD(P, d);
        out << "  d = " << d << " м:  ";

        if (!r.ok) {
            out << "равн.[-]  (НЕУСТОЙЧИВА)\n";
            continue;
        }

        bool isOk = true;
        out << "равн.[+]  ";

        // -------- проверка опоры (для strut / ring / frame) --------
        bool supOk = true;
        if (scheme != "console" && !r.R.empty()) {
            double supForce = fabs(r.R[0]);        // кН/м — погонная реакция

            if (scheme == "strut") {
                // Распорка: полное усилие N = q·s, где s — шаг распорок
                double N = supForce*strut.s;
                double Leff = strut.L/(strut.nMid + 1);
                double Ncr = PI*PI*P.E*strut.I/(Leff*Leff);
                if (N > Ncr/1.5) supOk = false;    // проверка устойчивости
            } else if (scheme == "ring") {
                // Круговое кольцо: N = q·R, проверка на прочность при сжатии
                double N = supForce*ring.R;
                if (N > P.Ry*ring.A) supOk = false;
            } else if (scheme == "frame") {
                // Прямоугольная обвязка — ЗАМКНУТАЯ РАМА
                double q = supForce;               // кН/м — реакция от стенки
                double N = q * frame.B / 2.0;      // кН — сжатие в поясе

                // Проверка прочности при сжатии
                double sigma = N / frame.A;        // кПа
                if (sigma > frame.Ry) supOk = false;

                // Устойчивость: замкнутая рама → углы жёсткие → Leff = 0.5·B,
                // Ncr = 4π²EI / B² (fixed-fixed).
                if (frame.I > 1e-12) {
                    double Ncr = 4.0 * PI * PI * P.E * frame.I / (frame.B * frame.B);
                    if (N > Ncr / 1.5) supOk = false;
                }
            }
        }
        out << "расп.[" << (supOk ? "+" : "-") << "]  ";
        if (!supOk) isOk = false;

        // -------- прочность стенки по изгибу --------
        // ИСПРАВЛЕНО: сравниваем абсолютную величину момента с несущей способностью
        bool strOk = fabs(r.Mmax) <= Mcap;
        out << "прочн.[" << (strOk ? "+" : "-") << "]  ";
        if (!strOk) isOk = false;

        // -------- грунт (отпор не превышает предельного) --------
        bool soilOk = r.maxSoilRatio <= 1.0;
        out << "грунт[" << (soilOk ? "+" : "-") << "]  ";
        if (!soilOk) isOk = false;

        // -------- прогиб верха стенки --------
        bool defOk = fabs(r.uTop) <= limDef;
        out << "прогиб[" << (defOk ? "+" : "-") << "]  ";
        if (!defOk) isOk = false;

        out << "  (Mmax = " << fabs(r.Mmax) << " кН*м; y = "
            << fabs(r.uTop)*1000 << " мм)";

        // -------- если все проверки сошлись — выводим итог и выходим --------
        if (isOk) {
            out << "   <<< ОК\n\n";
            out << "============================================================\n";
            out << "         РЕЗУЛЬТАТ\n";
            out << "============================================================\n";
            out << "Минимальная глубина заделки: d = " << d << " м\n";
            out << "Полная длина: L = H + d = " << P.h + d << " м\n";

            if (scheme == "strut") {
                double N = fabs(r.R[0])*strut.s;
                double Leff = strut.L/(strut.nMid + 1);
                double Ncr = PI*PI*P.E*strut.I/(Leff*Leff);
                out << "Распорка: N = " << N << " кН; Nкр = " << Ncr << " кН\n";
            }
            if (scheme == "ring") {
                double N = fabs(r.R[0])*ring.R;
                out << "Кольцо: распор N = " << N << " кН; Ry·A = "
                    << P.Ry*ring.A << " кН\n";
            }
            if (scheme == "frame") {
                // Вывод в тех же величинах, что и проверка
                double qObs = fabs(r.R[0]);
                double N = qObs * frame.B / 2.0;
                out << "Обвязка: распор N = " << N << " кН\n";
                // ИСПРАВЛЕНО: перевод кПа в МПа для вывода
                out << "  прочность: σ = " << N/frame.A/1000.0 << " МПа; "
                    << "Ry = " << frame.Ry/1000.0 << " МПа; σ/Ry = "
                    << (N/frame.A)/frame.Ry << " (≤ 1.0)\n";
                if (frame.I > 1e-12) {
                    double Ncr = 4.0 * PI * PI * P.E * frame.I / (frame.B * frame.B);
                    out << "  устойчивость: N/Nкр = " << N/Ncr
                        << " (≤ 0.67); Nкр = " << Ncr << " кН\n";
                }
            }

            out << "Mmax = " << fabs(r.Mmax) << " кН·м при z = " << r.zM << " м\n";
            out << "σ = " << fabs(r.Mmax)/P.W << " кПа при Ry = " << P.Ry << " кПа\n";
            out << "y_max ≈ " << fabs(r.uTop)*1000 << " мм при [y] = "
                << limDef*1000 << " мм\n";
            out << "Грунт: max(p/p_пред) = " << r.maxSoilRatio << "\n";
            out << "============================================================\n";

            found = true;
            break;
        }
        out << "\n";
    }
    if (!found) out << "\nНи одна заделка не подошла - измените профиль или схему\n";

    return QString::fromStdString(out.str());
}

