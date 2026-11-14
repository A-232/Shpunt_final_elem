#include "mainwindow.h"
#include "shpunt_calc.h"

#include <QApplication>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QDoubleValidator>
#include <QLocale>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QStatusBar>
#include <QFontDatabase>
#include <QFile>
#include <QTextStream>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Расчёт шпунтовой стенки — инженерный метод МКЭ");
    resize(960, 540);
    buildUI();
    syncEnableStates();
    connect(m_scheme, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSchemeChanged);
    statusBar()->showMessage("Готово к расчёту");
}

// ==================================================================
//  Построение интерфейса
// ==================================================================
void MainWindow::buildUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);

    auto* splitter = new QSplitter(Qt::Horizontal);
    root->addWidget(splitter, 1);

    // ---------------- LEFT: форма ----------------
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);

    auto* leftWidget = new QWidget;
    auto* tabs = new QTabWidget(leftWidget);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->addWidget(tabs);

    // ===== TAB: Схема =====
    {
        auto* w = new QWidget; auto* v = new QVBoxLayout(w);

        auto* g1 = new QGroupBox("Расчётная схема");
        auto* f1 = new QFormLayout(g1);
        m_scheme = new QComboBox;
        m_scheme->addItem("Консоль",              "console");
        m_scheme->addItem("Распорка",             "strut");
        m_scheme->addItem("Круговая обвязка",     "ring");
        m_scheme->addItem("Прямоугольная обвязка","frame");
        m_scheme->setCurrentIndex(1);
        f1->addRow("Схема:", m_scheme);
        v->addWidget(g1);

        auto* g2 = new QGroupBox("Геометрия и разбиение МКЭ");
        auto* f2 = new QFormLayout(g2);
        addDouble(f2, "h",     "Высота стенки h, м",         6.0,
                  "Глубина котлована над дном");
        addDouble(f2, "t",     "Шаг разбиения t, м",         0.5,
                  "Шаг КЭ по длине стенки");
        addInt   (f2, "steps", "Шагов нагружения",           20, 1, 200);
        v->addWidget(g2);

        auto* g3 = new QGroupBox("Перебор глубины заделки d");
        auto* f3 = new QFormLayout(g3);
        addDouble(f3, "d_min",  "d_min, м",  0.25);
        addDouble(f3, "d_max",  "d_max, м",  12.0);
        addDouble(f3, "d_step", "d_step, м", 0.25);
        v->addWidget(g3);

        v->addStretch();
        tabs->addTab(w, "Схема");
    }

    // ===== TAB: Грунт =====
    {
        auto* w = new QWidget; auto* v = new QVBoxLayout(w);
        auto* g = new QGroupBox("Грунт за стенкой");
        auto* f = new QFormLayout(g);
        addDouble(f, "gamma", "Удельный вес γ, кН/м³",       18.0);
        addDouble(f, "phi",   "Угол внутр. трения φ, °",     30.0);
        addDouble(f, "Kpost", "Коэф. постели K, кН/м⁴",      15000.0,
                  "Ориентировочно 5000…50000 для слабых…плотных грунтов");
        addDouble(f, "q",     "Пригрузка q, кПа",            10.0);
        v->addWidget(g);
        v->addStretch();
        tabs->addTab(w, "Грунт");
    }

    // ===== TAB: Шпунт =====
    {
        auto* w = new QWidget; auto* v = new QVBoxLayout(w);
        auto* g = new QGroupBox("Профиль шпунта (на 1 м стенки)");
        auto* f = new QFormLayout(g);
        addDouble(f, "E",  "Модуль упругости E, кПа",        2.06e8);
        addDouble(f, "I",  "Момент инерции I, м⁴/м",         3.0e-4);
        addDouble(f, "W",  "Момент сопротивления W, м³/м",   1.67e-3);
        addDouble(f, "Ry", "Расч. сопротивление Ry, кПа",    240e3);
        v->addWidget(g);
        v->addStretch();
        tabs->addTab(w, "Шпунт");
    }

    // ===== TAB: Усиление =====
    {
        auto* w = new QWidget; auto* v = new QVBoxLayout(w);

        m_supportGroup = new QGroupBox("Установка опоры (распорка / обвязка)");
        auto* f0 = new QFormLayout(m_supportGroup);
        addDouble(f0, "support_z",   "Глубина установки, м",       1.0,
                  "От верха стенки; -1 = опора отсутствует");
        addDouble(f0, "support_k",   "Жёсткость опоры, кН/м",      1e6);
        addDouble(f0, "support_lim", "Предельное усилие, кН/м",    1e8);
        v->addWidget(m_supportGroup);

        m_strutGroup = new QGroupBox("Распорка (схема «strut»)");
        auto* f1 = new QFormLayout(m_strutGroup);
        addDouble(f1, "strut_A",    "Площадь A, м²",         53e-4);
        addDouble(f1, "strut_I",    "Момент инерции I, м⁴",  2.955e-5);
        addDouble(f1, "strut_L",    "Длина L, м",            16.0);
        addDouble(f1, "strut_s",    "Шаг распорок s, м",     3.0);
        addInt   (f1, "strut_nMid", "Промежуточных связей",  1, 0, 20);
        v->addWidget(m_strutGroup);

        m_ringGroup = new QGroupBox("Круговая обвязка (схема «ring»)");
        auto* f2 = new QFormLayout(m_ringGroup);
        addDouble(f2, "ring_A", "Площадь A, м²",  61.2e-4);
        addDouble(f2, "ring_R", "Радиус R, м",    8.0);
        v->addWidget(m_ringGroup);

        m_frameGroup = new QGroupBox("Прямоугольная обвязка (схема «frame»)");
        auto* f3 = new QFormLayout(m_frameGroup);
        addDouble(f3, "frame_B",  "Пролёт B, м",              6.0);
        addDouble(f3, "frame_A",  "Площадь A, м²",            30.6e-4);
        addDouble(f3, "frame_W",  "Момент сопротивления W, м³", 242e-6);
        addDouble(f3, "frame_I",  "Момент инерции I, м⁴",     2.55e-5);
        addDouble(f3, "frame_Ry", "Ry, кПа",                  240e3);
        v->addWidget(m_frameGroup);

        v->addStretch();
        tabs->addTab(w, "Усиление");
    }

    scrollArea->setWidget(leftWidget);
    splitter->addWidget(scrollArea);

    // ---------------- RIGHT: результат ----------------
    auto* resTabs = new QTabWidget;
    auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_result = new QTextEdit;
    m_result->setReadOnly(true);
    m_result->setFont(mono);
    m_result->setPlainText(
        "Здесь будет результат расчёта.\n\n"
        "1. Выберите расчётную схему из выпадающего списка.\n"
        "2. В левой части по очереди заполните все вкладки вашими параметрами. \n"
        "   Или нажмите «Загрузить параметры…» если у вас есть готовый файл с заполненными параметрами.\n"
        "3. Нажмите «Рассчитать» (или Enter в любом поле).");
    resTabs->addTab(m_result, "Результат");

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(mono);
    resTabs->addTab(m_log, "Журнал итераций");

    splitter->addWidget(resTabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({560, 690});

    // ---------------- BOTTOM: кнопки ----------------
    auto* btnRow = new QHBoxLayout;
    auto* btnCalc    = new QPushButton("▶ Рассчитать");
    auto* btnDef     = new QPushButton("Сброс к умолчаниям");
    auto* btnLoad    = new QPushButton("Загрузить параметры…");
    auto* btnSave    = new QPushButton("Сохранить параметры…");
    auto* btnSaveRes = new QPushButton("Сохранить результат…");
    auto* btnClose   = new QPushButton("Закрыть");
    btnCalc->setDefault(true);
    btnCalc->setStyleSheet("font-weight: bold; padding: 6px 18px;");
    btnRow->addWidget(btnCalc);
    btnRow->addStretch();
    btnRow->addWidget(btnDef);
    btnRow->addWidget(btnLoad);
    btnRow->addWidget(btnSave);
    btnRow->addWidget(btnSaveRes);
    btnRow->addWidget(btnClose);
    root->addLayout(btnRow);

    connect(btnCalc,    &QPushButton::clicked, this, &MainWindow::onCalculate);
    connect(btnDef,     &QPushButton::clicked, this, &MainWindow::onDefaults);
    connect(btnLoad,    &QPushButton::clicked, this, &MainWindow::onLoadParams);
    connect(btnSave,    &QPushButton::clicked, this, &MainWindow::onSaveParams);
    connect(btnSaveRes, &QPushButton::clicked, this, &MainWindow::onSaveResult);
    connect(btnClose,   &QPushButton::clicked, this, &QWidget::close);

    // Стартовая инициализация формы
    writeForm(ParamsIO::defaults());
}

// ==================================================================
//  Вспомогательные конструкторы полей
// ==================================================================
QLineEdit* MainWindow::addDouble(QFormLayout* form, const QString& key,
                                 const QString& label, double def,
                                 const QString& tip)
{
    auto* lbl  = new QLabel(label);
    auto* edit = new QLineEdit(QString::number(def, 'g', 12));
    auto* val = new QDoubleValidator(-1e15, 1e15, 15);
    val->setNotation(QDoubleValidator::ScientificNotation);
    val->setLocale(QLocale::c());   // точка — чтобы std::stod и toDouble работали
    edit->setValidator(val);
    edit->setMinimumWidth(150);
    if (!tip.isEmpty()) { edit->setToolTip(tip); lbl->setToolTip(tip); }
    form->addRow(lbl, edit);
    m_doubleFields[key] = edit;
    connect(edit, &QLineEdit::returnPressed, this, &MainWindow::onCalculate);
    return edit;
}

QSpinBox* MainWindow::addInt(QFormLayout* form, const QString& key,
                             const QString& label, int def, int lo, int hi)
{
    auto* lbl  = new QLabel(label);
    auto* spin = new QSpinBox;
    spin->setRange(lo, hi);
    spin->setValue(def);
    spin->setMinimumWidth(150);
    form->addRow(lbl, spin);
    m_intFields[key] = spin;
    return spin;
}

// ==================================================================
//  Чтение/запись полей
// ==================================================================
double MainWindow::getD(const QString& key) const {
    bool ok; double v = m_doubleFields[key]->text().toDouble(&ok);
    return ok ? v : 0.0;
}
int MainWindow::getI(const QString& key) const {
    return m_intFields[key]->value();
}
void MainWindow::setD(const QString& key, double v) {
    m_doubleFields[key]->setText(QString::number(v, 'g', 12));
}
void MainWindow::setI(const QString& key, int v) {
    m_intFields[key]->setValue(v);
}

void MainWindow::syncEnableStates()
{
    QString s = m_scheme->currentData().toString();
    bool isConsole = (s == "console");
    m_supportGroup->setEnabled(!isConsole);
    m_strutGroup->setEnabled(s == "strut");
    m_ringGroup->setEnabled(s == "ring");
    m_frameGroup->setEnabled(s == "frame");
}
void MainWindow::onSchemeChanged() { syncEnableStates(); }

// ==================================================================
//  Чтение формы → FullParams + валидация
// ==================================================================
bool MainWindow::readForm(FullParams& fp, QString& err) const
{
    // 1. Проверяем, что все поля содержат корректные числа
    for (auto it = m_doubleFields.constBegin(); it != m_doubleFields.constEnd(); ++it) {
        bool ok; it.value()->text().toDouble(&ok);
        if (!ok || it.value()->text().trimmed().isEmpty()) {
            err = QString("Поле «%1» содержит некорректное число: «%2»")
                      .arg(it.key(), it.value()->text());
            return false;
        }
    }

    fp = ParamsIO::defaults();
    fp.scheme      = m_scheme->currentData().toString().toStdString();
    fp.P.h         = getD("h");
    fp.P.t         = getD("t");
    fp.P.steps     = getI("steps");
    fp.P.gamma     = getD("gamma");
    fp.P.phi       = getD("phi");
    fp.P.Kpost     = getD("Kpost");
    fp.P.q         = getD("q");
    fp.P.E         = getD("E");
    fp.P.I         = getD("I");
    fp.P.W         = getD("W");
    fp.P.Ry        = getD("Ry");
    fp.strut.A     = getD("strut_A");
    fp.strut.I     = getD("strut_I");
    fp.strut.L     = getD("strut_L");
    fp.strut.s     = getD("strut_s");
    fp.strut.nMid  = getI("strut_nMid");
    fp.ring.A      = getD("ring_A");
    fp.ring.R      = getD("ring_R");
    fp.frame.B     = getD("frame_B");
    fp.frame.A     = getD("frame_A");
    fp.frame.W     = getD("frame_W");
    fp.frame.I     = getD("frame_I");
    fp.frame.Ry    = getD("frame_Ry");
    fp.d_min       = getD("d_min");
    fp.d_max       = getD("d_max");
    fp.d_step      = getD("d_step");

    // 2. Опора — только для не-консольных схем
    fp.P.supports.clear();
    if (fp.scheme != "console") {
        double supZ   = getD("support_z");
        double supK   = getD("support_k");
        double supLim = getD("support_lim");
        if (supZ >= 0.0) {
            Support s;
            s.z = supZ; s.k = supK; s.lim = supLim; s.oneSided = true;
            fp.P.supports.push_back(s);
        } else {
            err = "Для выбранной схемы необходимо задать support_z ≥ 0 "
                  "(support_z < 0 допустим только для схемы «Консоль»)";
            return false;
        }
    }

    // 3. Проверки диапазонов
    if (fp.P.h <= 0)          { err = "Высота стенки h должна быть > 0"; return false; }
    if (fp.P.t <= 0)          { err = "Шаг разбиения t должен быть > 0"; return false; }
    if (fp.P.steps < 1)       { err = "Число шагов нагружения должно быть ≥ 1"; return false; }
    if (fp.P.gamma <= 0)      { err = "Удельный вес γ должен быть > 0"; return false; }
    if (fp.P.phi <= 0 || fp.P.phi >= 90)
    { err = "Угол трения φ должен быть в диапазоне 0…90°"; return false; }
    if (fp.P.E <= 0 || fp.P.I <= 0 || fp.P.W <= 0 || fp.P.Ry <= 0)
    { err = "Характеристики шпунта E, I, W, Ry должны быть > 0"; return false; }
    if (fp.d_step <= 0)       { err = "Шаг перебора d_step должен быть > 0"; return false; }
    if (fp.d_max <= fp.d_min) { err = "d_max должно быть больше d_min"; return false; }
    if (fp.scheme == "strut" &&
        (fp.strut.A <= 0 || fp.strut.I <= 0 || fp.strut.L <= 0 || fp.strut.s <= 0))
    { err = "Параметры распорки должны быть > 0"; return false; }
    if (fp.scheme == "ring" &&
        (fp.ring.A <= 0 || fp.ring.R <= 0))
    { err = "Параметры круговой обвязки должны быть > 0"; return false; }
    if (fp.scheme == "frame" &&
        (fp.frame.B <= 0 || fp.frame.A <= 0 || fp.frame.W <= 0 ||
         fp.frame.I <= 0 || fp.frame.Ry <= 0))
    { err = "Параметры прямоугольной обвязки должны быть > 0"; return false; }
    return true;
}

// ==================================================================
//  Запись FullParams → форма
// ==================================================================
void MainWindow::writeForm(const FullParams& fp)
{
    int idx = m_scheme->findData(QString::fromStdString(fp.scheme));
    if (idx >= 0) m_scheme->setCurrentIndex(idx);

    setD("h", fp.P.h);         setD("t", fp.P.t);       setI("steps", fp.P.steps);
    setD("gamma", fp.P.gamma); setD("phi", fp.P.phi);
    setD("Kpost", fp.P.Kpost); setD("q", fp.P.q);
    setD("E", fp.P.E);         setD("I", fp.P.I);
    setD("W", fp.P.W);         setD("Ry", fp.P.Ry);

    setD("strut_A", fp.strut.A); setD("strut_I", fp.strut.I);
    setD("strut_L", fp.strut.L); setD("strut_s", fp.strut.s);
    setI("strut_nMid", fp.strut.nMid);

    setD("ring_A", fp.ring.A);   setD("ring_R", fp.ring.R);

    setD("frame_B",  fp.frame.B);  setD("frame_A",  fp.frame.A);
    setD("frame_W",  fp.frame.W);  setD("frame_I",  fp.frame.I);
    setD("frame_Ry", fp.frame.Ry);

    setD("d_min", fp.d_min);   setD("d_max", fp.d_max);   setD("d_step", fp.d_step);

    if (fp.P.supports.empty()) {
        setD("support_z", -1.0);
        setD("support_k", 1e6);
        setD("support_lim", 1e8);
    } else {
        setD("support_z",   fp.P.supports[0].z);
        setD("support_k",   fp.P.supports[0].k);
        setD("support_lim", fp.P.supports[0].lim);
    }

    syncEnableStates();
}

// ==================================================================
//  Слоты
// ==================================================================
void MainWindow::onCalculate()
{
    FullParams fp;
    QString err;
    if (!readForm(fp, err)) {
        QMessageBox::warning(this, "Некорректные данные", err);
        return;
    }

    statusBar()->showMessage("Расчёт…");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QString full = calculate(fp.P, fp.scheme, fp.strut, fp.ring, fp.frame,
                             fp.d_min, fp.d_max, fp.d_step);

    QApplication::restoreOverrideCursor();

    // Разделяем на «журнал» и «результат» по маркеру "         РЕЗУЛЬТАТ\n"
    int idx = full.indexOf("         РЕЗУЛЬТАТ\n");
    QString logText, resultText;
    if (idx > 0) {
        int startLine = full.lastIndexOf("=====", idx);
        int lineStart = full.lastIndexOf('\n', startLine);
        lineStart = (lineStart < 0) ? 0 : lineStart + 1;
        resultText = full.mid(lineStart).trimmed();
        logText    = full.left(lineStart).trimmed();
    } else {
        resultText = "!!! РЕШЕНИЕ НЕ НАЙДЕНО.\n\n"
                     "См. вкладку «Журнал итераций» для подробностей.\n"
                     "Увеличьте d_max, измените профиль шпунта или схему.";
        logText    = full;
    }

    m_result->setPlainText(resultText);
    m_log->setPlainText(logText);

    if (idx > 0)
        statusBar()->showMessage("Расчёт завершён успешно");
    else
        statusBar()->showMessage("Решение не найдено");
}

void MainWindow::onDefaults()
{
    writeForm(ParamsIO::defaults());
    m_result->clear();
    m_log->clear();
    statusBar()->showMessage("Параметры сброшены к умолчаниям");
}

void MainWindow::onLoadParams()
{
    QString startDir = m_lastPath.isEmpty() ? QDir::currentPath() : m_lastPath;
    QString f = QFileDialog::getOpenFileName(this,
                                             "Загрузить параметры", startDir,
                                             "Текстовые файлы (*.txt);;Все файлы (*)");
    if (f.isEmpty()) return;
    FullParams fp; QString err;
    if (!ParamsIO::loadFromFile(f, fp, err)) {
        QMessageBox::critical(this, "Ошибка загрузки", err); return;
    }
    writeForm(fp);
    m_lastPath = f;
    statusBar()->showMessage("Загружено: " + f);
}

void MainWindow::onSaveParams()
{
    QString startDir = m_lastPath.isEmpty() ? QDir::currentPath() : m_lastPath;
    QString f = QFileDialog::getSaveFileName(this,
                                             "Сохранить параметры", startDir + "/parameters.txt",
                                             "Текстовые файлы (*.txt)");
    if (f.isEmpty()) return;

    FullParams fp; QString err;
    if (!readForm(fp, err)) {
        QMessageBox::warning(this, "Некорректные данные",
                             "Перед сохранением исправьте:\n\n" + err);
        return;
    }
    if (!ParamsIO::saveToFile(f, fp, err)) {
        QMessageBox::critical(this, "Ошибка сохранения", err); return;
    }
    m_lastPath = f;
    statusBar()->showMessage("Сохранено: " + f);
}

void MainWindow::onSaveResult()
{
    if (m_result->toPlainText().isEmpty() ||
        m_result->toPlainText().startsWith("Здесь будет")) {
        QMessageBox::information(this, "Нет результата",
                                 "Сначала выполните расчёт.");
        return;
    }
    QString f = QFileDialog::getSaveFileName(this,
                                             "Сохранить результат", QDir::currentPath() + "/result.txt",
                                             "Текстовые файлы (*.txt)");
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось открыть файл"); return;
    }
    QTextStream ts(&file);
    ts << m_result->toPlainText() << "\n\n";
    ts << ">>> ЖУРНАЛ ИТЕРАЦИЙ\n\n";
    ts << m_log->toPlainText() << "\n";
    statusBar()->showMessage("Сохранено: " + f);
}