// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphwavsourceview.hh"
#include "sminsteditwindow.hh"
#include "smmorphplan.hh"
#include "smwavsetbuilder.hh"

#include "smlabel.hh"
#include "smbutton.hh"
#include "smoperatorlayout.hh"

#include <thread>

using namespace SpectMorph;

using std::string;
using std::vector;

struct UserInstrumentIndex
{
  string
  filename (int number)
  {
    string user_bank_dir = sm_get_user_dir (USER_DIR_DATA) + "/user"; // FIXME: test only
    g_mkdir_with_parents (user_bank_dir.c_str(), 0775);
    return string_printf ("%s/%d.sminst", user_bank_dir.c_str(), number);
  }
  string
  label (int number)
  {
    Instrument inst;

    inst.load (filename (number));
    if (inst.name() != "")
      return string_printf ("%03d %s", number, inst.name().c_str());
    else
      return string_printf ("%03d ---", number);
  }
} user_instrument_index;

MorphWavSourceView::MorphWavSourceView (Widget *parent, MorphWavSource *morph_wav_source, MorphPlanWindow *morph_plan_window) :
  MorphOperatorView (parent, morph_wav_source, morph_plan_window),
  morph_wav_source (morph_wav_source)
{
  OperatorLayout op_layout;

  Label *instrument_label = new Label (body_widget, "Instrument");
  instrument_combobox = new ComboBox (body_widget);
  Button *edit_button = new Button (body_widget, "Edit");

  update_instrument_list();

  op_layout.add_row (3, instrument_label, instrument_combobox, edit_button);
  op_layout.activate();

  connect (instrument_combobox->signal_item_changed, this, &MorphWavSourceView::on_instrument_changed);
  connect (edit_button->signal_clicked, this, &MorphWavSourceView::on_edit);
}

double
MorphWavSourceView::view_height()
{
  return 8;
}

void
MorphWavSourceView::on_load()
{
  /* create instrument in Project if WavSource doesn't have one */
  if (morph_wav_source->instrument() == 0)
    morph_wav_source->set_instrument (morph_wav_source->morph_plan()->project()->add_instrument());

  window()->open_file_dialog ("Select SpectMorph Instrument to load", "SpectMorph Instrument files", "*.sminst", [=](string filename) {
    if (filename != "")
      {
        Instrument *instrument = morph_wav_source->morph_plan()->project()->get_instrument (morph_wav_source->instrument());
        instrument->load (filename);

        morph_wav_source->morph_plan()->project()->rebuild (morph_wav_source->instrument());
      }
  });
}

void
MorphWavSourceView::on_edit()
{
  /* create instrument in Project if WavSource doesn't have one */
  if (morph_wav_source->instrument() == 0)
    morph_wav_source->set_instrument (morph_wav_source->morph_plan()->project()->add_instrument());

  SynthInterface *synth_interface = morph_plan_window->synth_interface();
  synth_interface->synth_inst_edit_update (true, nullptr, false);

  Instrument *instrument = morph_wav_source->morph_plan()->project()->get_instrument (morph_wav_source->instrument());
  InstEditWindow *inst_edit_window = new InstEditWindow (*window()->event_loop(), instrument, synth_interface, window());

  inst_edit_window->show();

  // after this line, inst edit window is owned by parent window
  window()->set_popup_window (inst_edit_window);

  inst_edit_window->set_close_callback ([synth_interface,this,instrument]()
    {
      window()->set_popup_window (nullptr);
      synth_interface->synth_inst_edit_update (false, nullptr, false);
      instrument->save (user_instrument_index.filename (morph_wav_source->INST()));
      update_instrument_list();
      morph_wav_source->morph_plan()->project()->rebuild (morph_wav_source->instrument());
    });
}

void
MorphWavSourceView::on_instrument_changed()
{
  /* create instrument in Project if WavSource doesn't have one */
  if (morph_wav_source->instrument() == 0)
    morph_wav_source->set_instrument (morph_wav_source->morph_plan()->project()->add_instrument());

  Instrument *instrument = morph_wav_source->morph_plan()->project()->get_instrument (morph_wav_source->instrument());
  morph_wav_source->set_INST (atoi (instrument_combobox->text().c_str()));
  instrument->load (user_instrument_index.filename (morph_wav_source->INST()));
  morph_wav_source->morph_plan()->project()->rebuild (morph_wav_source->instrument());
}

void
MorphWavSourceView::update_instrument_list()
{
  instrument_combobox->clear();
  for (int i = 1; i <= 128; i++)
    {
      string item = user_instrument_index.label (i);
      instrument_combobox->add_item (item);

      if (i == morph_wav_source->INST())
        instrument_combobox->set_text (item);
    }
}
