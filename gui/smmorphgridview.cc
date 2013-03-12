// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphgridview.hh"
#include "smmorphgridwidget.hh"
#include "smmorphplan.hh"
#include "smcomboboxoperator.hh"

#include <QSpinBox>
#include <QLabel>
#include <QPainter>

using namespace SpectMorph;

class OpFilter : public OperatorFilter
{
  bool
  filter (MorphOperator *op)
  {
    return true; // FIXME
  }
} op_filter;

MorphGridView::MorphGridView (MorphGrid *morph_grid, MorphPlanWindow *morph_plan_window) :
  MorphOperatorView (morph_grid, morph_plan_window),
  morph_grid (morph_grid)
{
  width_spinbox = new QSpinBox();
  height_spinbox = new QSpinBox();

  width_spinbox->setRange (1, 16);
  height_spinbox->setRange (1, 16);

  width_spinbox->setValue (morph_grid->width());
  height_spinbox->setValue (morph_grid->height());

  QHBoxLayout *hbox = new QHBoxLayout();
  hbox->addWidget (new QLabel ("Width"));
  hbox->addWidget (width_spinbox);
  hbox->addWidget (new QLabel ("Height"));
  hbox->addWidget (height_spinbox);

  op_combobox = new ComboBoxOperator (morph_grid->morph_plan(), &op_filter);

  QHBoxLayout *bottom_hbox = new QHBoxLayout();
  bottom_hbox->addWidget (new QLabel ("Instrument/Source"));
  bottom_hbox->addWidget (op_combobox);

  grid_widget = new MorphGridWidget (morph_grid);
  QVBoxLayout *vbox = new QVBoxLayout();
  vbox->addLayout (hbox);
  vbox->addWidget (grid_widget);
  vbox->addLayout (bottom_hbox);
  setLayout (vbox);

  connect (grid_widget, SIGNAL (selection_changed()), this, SLOT (on_selection_changed()));
  connect (width_spinbox, SIGNAL (valueChanged (int)), this, SLOT (on_size_changed()));
  connect (height_spinbox, SIGNAL (valueChanged (int)), this, SLOT (on_size_changed()));
}

void
MorphGridView::on_size_changed()
{
  morph_grid->set_width (width_spinbox->value());
  morph_grid->set_height (height_spinbox->value());
}

void
MorphGridView::on_selection_changed()
{
  op_combobox->setEnabled (morph_grid->has_selection());
}
