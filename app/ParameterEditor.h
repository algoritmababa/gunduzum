#ifndef PARAMETEREDITOR_H
#define PARAMETEREDITOR_H

#include "Types.h"

#include <QWidget>

#include <vector>

class QFormLayout;
class QVBoxLayout;

// Builds editors dynamically from ParamSpec descriptions; knows nothing about
// individual algorithms.
class ParameterEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterEditor(QWidget *parent = nullptr);

    void setSpecs(const std::vector<turb::ParamSpec> &specs, const turb::ParamSet &values);
    turb::ParamSet values() const;
    void resetToDefaults();

signals:
    void changed();

private:
    void clear();

    QVBoxLayout *m_outer;
    QWidget *m_content; // rebuilt for every algorithm, owns m_form
    QFormLayout *m_form;
    std::vector<turb::ParamSpec> m_specs;
    std::vector<QWidget *> m_editors;
};

#endif
