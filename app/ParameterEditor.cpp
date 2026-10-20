#include "ParameterEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

#include <climits>
#include <cmath>

ParameterEditor::ParameterEditor(QWidget *parent)
    : QWidget(parent)
    , m_form(new QFormLayout(this))
{
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

void ParameterEditor::clear()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_editors.clear();
}

void ParameterEditor::setSpecs(const std::vector<turb::ParamSpec> &specs, const turb::ParamSet &values)
{
    clear();
    m_specs = specs;
    if (specs.empty()) {
        m_form->addRow(new QLabel(tr("(no parameters)")));
        return;
    }
    const turb::ParamSet v = turb::ParamSet::withDefaults(specs, values);
    for (const turb::ParamSpec &s : specs) {
        QWidget *w = nullptr;
        const double val = v.get(s.key, s.defaultValue);
        switch (s.type) {
        case turb::ParamSpec::Int: {
            QSpinBox *sb = new QSpinBox;
            sb->setRange(int(std::max<double>(s.minValue, INT_MIN)), int(std::min<double>(s.maxValue, INT_MAX)));
            sb->setValue(int(std::lround(val)));
            connect(sb, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ParameterEditor::changed);
            w = sb;
            break;
        }
        case turb::ParamSpec::Double: {
            QDoubleSpinBox *sb = new QDoubleSpinBox;
            sb->setDecimals(s.decimals);
            sb->setRange(s.minValue, s.maxValue);
            sb->setSingleStep(s.step);
            sb->setValue(val);
            connect(sb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
                    &ParameterEditor::changed);
            w = sb;
            break;
        }
        case turb::ParamSpec::Bool: {
            QCheckBox *cb = new QCheckBox;
            cb->setChecked(val != 0.0);
            connect(cb, &QCheckBox::toggled, this, &ParameterEditor::changed);
            w = cb;
            break;
        }
        case turb::ParamSpec::Choice: {
            QComboBox *cb = new QComboBox;
            for (const std::string &c : s.choices)
                cb->addItem(QString::fromStdString(c));
            cb->setCurrentIndex(int(std::lround(val)));
            connect(cb, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                    &ParameterEditor::changed);
            w = cb;
            break;
        }
        }
        const QString tip = QString::fromStdString(s.tooltip);
        w->setToolTip(tip);
        QLabel *label = new QLabel(QString::fromStdString(s.label));
        label->setToolTip(tip);
        m_form->addRow(label, w);
        m_editors.push_back(w);
    }
}

turb::ParamSet ParameterEditor::values() const
{
    turb::ParamSet out;
    for (size_t i = 0; i < m_specs.size() && i < m_editors.size(); ++i) {
        const turb::ParamSpec &s = m_specs[i];
        QWidget *w = m_editors[i];
        double v = s.defaultValue;
        if (QSpinBox *sb = qobject_cast<QSpinBox *>(w))
            v = sb->value();
        else if (QDoubleSpinBox *db = qobject_cast<QDoubleSpinBox *>(w))
            v = db->value();
        else if (QCheckBox *cb = qobject_cast<QCheckBox *>(w))
            v = cb->isChecked() ? 1.0 : 0.0;
        else if (QComboBox *co = qobject_cast<QComboBox *>(w))
            v = co->currentIndex();
        out.set(s.key, v);
    }
    return out;
}

void ParameterEditor::resetToDefaults()
{
    const std::vector<turb::ParamSpec> specs = m_specs;
    setSpecs(specs, turb::ParamSet());
    emit changed();
}
