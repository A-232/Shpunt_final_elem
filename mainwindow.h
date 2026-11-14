#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QString>
#include "params_io.h"

class QLineEdit;
class QSpinBox;
class QComboBox;
class QTextEdit;
class QFormLayout;
class QGroupBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onCalculate();
    void onDefaults();
    void onLoadParams();
    void onSaveParams();
    void onSaveResult();
    void onSchemeChanged();

private:
    void buildUI();
    void syncEnableStates();

    QLineEdit* addDouble(QFormLayout* form, const QString& key,
                         const QString& label, double def,
                         const QString& tip = "");
    QSpinBox*  addInt   (QFormLayout* form, const QString& key,
                     const QString& label, int def, int lo, int hi);

    double getD(const QString& key) const;
    int    getI(const QString& key) const;
    void   setD(const QString& key, double v);
    void   setI(const QString& key, int v);

    bool readForm (FullParams& fp, QString& err) const;
    void writeForm(const FullParams& fp);

    QComboBox* m_scheme = nullptr;
    QMap<QString, QLineEdit*> m_doubleFields;
    QMap<QString, QSpinBox*>  m_intFields;

    QGroupBox* m_supportGroup = nullptr;
    QGroupBox* m_strutGroup   = nullptr;
    QGroupBox* m_ringGroup    = nullptr;
    QGroupBox* m_frameGroup   = nullptr;

    QTextEdit* m_result = nullptr;
    QTextEdit* m_log    = nullptr;

    QString m_lastPath;
};

#endif