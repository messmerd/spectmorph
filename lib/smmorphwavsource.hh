// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#ifndef SPECTMORPH_MORPH_WAV_SOURCE_HH
#define SPECTMORPH_MORPH_WAV_SOURCE_HH

#include "smmorphoperator.hh"
#include "smmodulationlist.hh"
#include "smproperty.hh"
#include "smformantcorrection.hh"

#include <string>

namespace SpectMorph
{

class MorphWavSource;
class Project;
class Instrument;

class MorphWavSource : public MorphOperator
{
  LeakDebugger2 leak_debugger2 { "SpectMorph::MorphWavSource" };
public:
  enum PlayMode {
    PLAY_MODE_STANDARD        = 1,
    PLAY_MODE_CUSTOM_POSITION = 2
  };
  struct Config : public MorphOperatorConfig
  {
    Project          *project = nullptr;

    int                     object_id = 0;
    PlayMode                play_mode = PLAY_MODE_STANDARD;
    ModulationData          position_mod;
    FormantCorrection::Mode formant_correct = FormantCorrection::MODE_REPITCH;
    float                   fuzzy_resynth;
  };
  static constexpr auto P_PLAY_MODE          = "play_mode";
  static constexpr auto P_POSITION           = "position";
  static constexpr auto P_FORMANT_CORRECTION = "formant_correction";
  static constexpr auto P_FUZZY_FREQS        = "fuzzy_freqs";

  static constexpr auto USER_BANK = "User";
protected:
  Config      m_config;

  int         m_instrument = 1;
  std::string m_bank = USER_BANK;
  std::string m_lv2_filename;

public:
  MorphWavSource (MorphPlan *morph_plan);

  // inherited from MorphOperator
  const char *type() override;
  int         insert_order() override;
  bool        save (OutFile& out_file) override;
  bool        load (InFile&  in_file) override;
  OutputType  output_type() override;
  std::vector<MorphOperator *> dependencies() override;
  MorphOperatorConfig *clone_config() override;

  void        set_object_id (int id);
  int         object_id();

  void        set_bank_and_instrument (const std::string& bank, int inst);

  int         instrument();
  std::string bank();

  void        set_lv2_filename (const std::string& filename);
  std::string lv2_filename();

  void        on_instrument_updated (const std::string& bank, int number, const Instrument *new_instrument);
  void        on_bank_removed (const std::string& bank);

  Signal<> signal_labels_changed;
};

}

#endif
