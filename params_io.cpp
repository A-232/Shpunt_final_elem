#include "params_io.h"
#include <QFile>
#include <QTextStream>
#include <QMap>
#include <cmath>

FullParams ParamsIO::defaults() {
    FullParams fp;
    fp.scheme = "strut";
    fp.P.t = 0.5;
    fp.P.steps = 20;
    fp.P.h = 3.0;
    fp.P.gamma = 18.0;
    fp.P.phi = 30.0;
    fp.P.Kpost = 15000.0;
    fp.P.q = 10.0;
    fp.P.E = 2.06e8;
    fp.P.I = 3.0e-4;
    fp.P.W = 1.67e-3;
    fp.P.Ry = 240e3;
    fp.strut.A = 53e-4;   fp.strut.I = 2.955e-5;
    fp.strut.L = 16.0;    fp.strut.s = 3.0;    fp.strut.nMid = 1;
    fp.ring.A = 61.2e-4;  fp.ring.R = 8.0;
    fp.frame.B = 6.0;     fp.frame.A = 30.6e-4; fp.frame.W = 242e-6;
    fp.frame.I = 2.55e-5; fp.frame.Ry = 240e3;
    fp.d_min = 0.25; fp.d_max = 12.0; fp.d_step = 0.25;
    // Опора по умолчанию (для не-консольных схем)
    Support s; s.z = 1.0; s.k = 1e6; s.lim = 1e8;
    // Учитываем схему по умолчанию
    s.oneSided = (fp.scheme == "strut"); 
    fp.P.supports.push_back(s);
    return fp;
}

static QMap<QString, QString> readKV(const QString& fname) {
    QMap<QString, QString> kv;
    QFile f(fname);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return kv;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        int c = line.indexOf('#'); if (c >= 0) line = line.left(c);
        int eq = line.indexOf('='); if (eq < 0) continue;
        QString k = line.left(eq).trimmed();
        QString v = line.mid(eq + 1).trimmed();
        if (!k.isEmpty()) kv[k] = v;
    }
    return kv;
}
static double gn(const QMap<QString,QString>& kv, const QString& k, double def) {
    if (!kv.contains(k)) return def;
    bool ok; double v = kv[k].toDouble(&ok); return ok ? v : def;
}
static int gi(const QMap<QString,QString>& kv, const QString& k, int def) {
    if (!kv.contains(k)) return def;
    bool ok; int v = kv[k].toInt(&ok); return ok ? v : def;
}
static QString gs(const QMap<QString,QString>& kv, const QString& k,
                  const QString& def) {
    return kv.contains(k) ? kv[k] : def;
}

bool ParamsIO::loadFromFile(const QString& filename, FullParams& fp, QString& err) {
    QFile f(filename);
    if (!f.exists()) { err = "Файл не найден: " + filename; return false; }
    auto kv = readKV(filename);
    if (kv.isEmpty()) { err = "Файл пуст или не содержит пар «ключ=значение»: " + filename; return false; }

    fp = defaults();
    fp.scheme      = gs(kv, "scheme", "strut").toStdString();
    fp.P.t         = gn(kv, "t",       fp.P.t);
    fp.P.steps     = gi(kv, "steps",   fp.P.steps);
    fp.P.h         = gn(kv, "h",       fp.P.h);
    fp.P.gamma     = gn(kv, "gamma",   fp.P.gamma);
    fp.P.phi       = gn(kv, "phi",     fp.P.phi);
    fp.P.Kpost     = gn(kv, "Kpost",   fp.P.Kpost);
    fp.P.q         = gn(kv, "q",       fp.P.q);
    fp.P.E         = gn(kv, "E",       fp.P.E);
    fp.P.I         = gn(kv, "I",       fp.P.I);
    fp.P.W         = gn(kv, "W",       fp.P.W);
    fp.P.Ry        = gn(kv, "Ry",      fp.P.Ry);

    fp.strut.A     = gn(kv, "strut_A", fp.strut.A);
    fp.strut.I     = gn(kv, "strut_I", fp.strut.I);
    fp.strut.L     = gn(kv, "strut_L", fp.strut.L);
    fp.strut.s     = gn(kv, "strut_s", fp.strut.s);
    fp.strut.nMid  = gi(kv, "strut_nMid", fp.strut.nMid);

    fp.ring.A      = gn(kv, "ring_A",  fp.ring.A);
    fp.ring.R      = gn(kv, "ring_R",  fp.ring.R);

    fp.frame.B     = gn(kv, "frame_B",  fp.frame.B);
    fp.frame.A     = gn(kv, "frame_A",  fp.frame.A);
    fp.frame.W     = gn(kv, "frame_W",  fp.frame.W);
    fp.frame.I     = gn(kv, "frame_I",  fp.frame.I);
    fp.frame.Ry    = gn(kv, "frame_Ry", fp.frame.Ry);

    fp.d_min       = gn(kv, "d_min",   fp.d_min);
    fp.d_max       = gn(kv, "d_max",   fp.d_max);
    fp.d_step      = gn(kv, "d_step",  fp.d_step);

    // Опорные связи: файл описывает ровно одну опору (как в GUI).
    // При scheme == console — игнорируем.
    double supZ   = gn(kv, "support_z",   1.0);
    double supK   = gn(kv, "support_k",   1e6);
    double supLim = gn(kv, "support_lim", 1e8);
    fp.P.supports.clear();
    if (fp.scheme != "console" && supZ >= 0.0) {
        Support s;
        s.z = supZ; s.k = supK; s.lim = supLim;
        // Распорка работает только на сжатие (односторонняя)
        // Обвязки (ring, frame) держат стенку в обе стороны (двухсторонние)
        if (fp.scheme == "strut") {
            s.oneSided = true;
        } else {
            s.oneSided = false; 
        }
        fp.P.supports.push_back(s);
    }
    return true;
}

bool ParamsIO::saveToFile(const QString& filename, const FullParams& fp, QString& err) {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        err = "Не удалось открыть файл для записи: " + filename; return false;
    }
    QTextStream ts(&f);
    auto n = [](double v) { return QString::number(v, 'g', 12); };

    ts << "# ============ СХЕМА ============\n";
    ts << "# console — консоль;  strut — распорка;\n";
    ts << "# ring    — круговая обвязка;  frame — прямоугольная обвязка\n";
    ts << "scheme = " << QString::fromStdString(fp.scheme) << "\n\n";

    ts << "# ============ ГЕОМЕТРИЯ И РАЗБИЕНИЕ МКЭ ============\n";
    ts << "h        = " << n(fp.P.h)      << "     # глубина котлована, м\n";
    ts << "t        = " << n(fp.P.t)      << "     # шаг разбиения МКЭ, м\n";
    ts << "steps    = " << fp.P.steps      << "     # число шагов нагружения\n\n";

    ts << "# ============ ГРУНТ ЗА СТЕНКОЙ ============\n";
    ts << "gamma    = " << n(fp.P.gamma)  << "     # удельный вес, кН/м³\n";
    ts << "phi      = " << n(fp.P.phi)    << "     # угол внутр. трения, °\n";
    ts << "Kpost    = " << n(fp.P.Kpost)  << "     # коэф. постели, кН/м⁴\n";
    ts << "q        = " << n(fp.P.q)      << "     # пригрузка, кПа\n\n";

    ts << "# ============ ШПУНТ ============\n";
    ts << "E        = " << n(fp.P.E)      << "\n";
    ts << "I        = " << n(fp.P.I)      << "\n";
    ts << "W        = " << n(fp.P.W)      << "\n";
    ts << "Ry       = " << n(fp.P.Ry)     << "\n\n";

    ts << "# ============ ОПОРА (для схем strut/ring/frame) ============\n";
    double supZ = -1.0, supK = 1e6, supLim = 1e8;
    if (!fp.P.supports.empty()) {
        supZ   = fp.P.supports[0].z;
        supK   = fp.P.supports[0].k;
        supLim = fp.P.supports[0].lim;
    }
    ts << "support_z   = " << n(supZ)   << "     # глубина установки, м (<0 — нет опоры)\n";
    ts << "support_k   = " << n(supK)   << "     # жёсткость, кН/м\n";
    ts << "support_lim = " << n(supLim) << "     # предельное усилие, кН/м\n\n";

    ts << "# ============ РАСПОРКА ============\n";
    ts << "strut_A    = " << n(fp.strut.A)    << "\n";
    ts << "strut_I    = " << n(fp.strut.I)    << "\n";
    ts << "strut_L    = " << n(fp.strut.L)    << "\n";
    ts << "strut_s    = " << n(fp.strut.s)    << "\n";
    ts << "strut_nMid = " << fp.strut.nMid     << "\n\n";

    ts << "# ============ КРУГОВАЯ ОБВЯЗКА ============\n";
    ts << "ring_A     = " << n(fp.ring.A)     << "\n";
    ts << "ring_R     = " << n(fp.ring.R)     << "\n\n";

    ts << "# ============ ПРЯМОУГОЛЬНАЯ ОБВЯЗКА ============\n";
    ts << "frame_B    = " << n(fp.frame.B)    << "\n";
    ts << "frame_A    = " << n(fp.frame.A)    << "\n";
    ts << "frame_W    = " << n(fp.frame.W)    << "\n";
    ts << "frame_I    = " << n(fp.frame.I)    << "\n";
    ts << "frame_Ry   = " << n(fp.frame.Ry)   << "\n\n";

    ts << "# ============ ПЕРЕБОР ГЛУБИНЫ ЗАДЕЛКИ d ============\n";
    ts << "d_min      = " << n(fp.d_min)      << "\n";
    ts << "d_max      = " << n(fp.d_max)      << "\n";
    ts << "d_step     = " << n(fp.d_step)     << "\n";
    return true;
}