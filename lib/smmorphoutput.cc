// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#include "smmorphoutput.hh"
#include "smmorphplan.hh"

#include <assert.h>

#define CHANNEL_OP_COUNT 4

using namespace SpectMorph;

using std::string;
using std::vector;

MorphOutput::MorphOutput (MorphPlan *morph_plan) :
  MorphOperator (morph_plan)
{
  connect (morph_plan->signal_operator_removed, this, &MorphOutput::on_operator_removed);

  m_config.channel_ops.resize (CHANNEL_OP_COUNT);

  add_property (&m_config.velocity_sensitivity, P_VELOCITY_SENSITIVITY, "Velocity Sns", "%.2f dB", 24, 0, 48);
  add_property (&m_config.pitch_bend_range, P_PITCH_BEND_RANGE, "Pitch Bend", "%d st", 48, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 24, 36, 48 });

  add_property (&m_config.sines, P_SINES, "Enable Sine Synthesis", true);
  add_property (&m_config.noise, P_NOISE, "Enable Noise Synthesis", true);

  add_property (&m_config.unison, P_UNISON, "Enable Unison Effect", false);
  add_property (&m_config.unison_voices, P_UNISON_VOICES, "Voices", "%d", 2, 2, 7);
  add_property (&m_config.unison_detune, P_UNISON_DETUNE, "Detune", "%.1f Cent", 6, 0.5, 50);

  add_property (&m_config.adsr, P_ADSR, "Enable custom ADSR Envelope", false);
  add_property (&m_config.adsr_skip, P_ADSR_SKIP, "Skip", "%.1f ms", 500, 0, 1000);
  add_property (&m_config.adsr_attack, P_ADSR_ATTACK, "Attack", "%.1f %%", 15, 0, 100);
  add_property (&m_config.adsr_decay, P_ADSR_DECAY,"Decay", "%.1f %%", 20, 0, 100);
  add_property (&m_config.adsr_sustain, P_ADSR_SUSTAIN, "Sustain", "%.1f %%", 70, 0, 100);
  add_property (&m_config.adsr_release, P_ADSR_RELEASE, "Release", "%.1f %%", 50, 0, 100);

  EnumInfo filter_type_enum_info (
    {
      { FILTER_TYPE_LADDER, "Ladder" },
      { FILTER_TYPE_SALLEN_KEY, "Sallen-Key" }
    });
  EnumInfo filter_ladder_mode_enum_info (
    {
      { FILTER_LADDER_LP1, "Low-pass 6dB" },
      { FILTER_LADDER_LP2, "Low-pass 12dB" },
      { FILTER_LADDER_LP3, "Low-pass 18dB" },
      { FILTER_LADDER_LP4, "Low-pass 24dB" }
    });
  EnumInfo filter_sk_mode_enum_info (
    {
      { FILTER_SK_LP1, "Low-pass 6dB" },
      { FILTER_SK_LP2, "Low-pass 12dB" },
      { FILTER_SK_LP3, "Low-pass 18dB" },
      { FILTER_SK_LP4, "Low-pass 24dB" },
      { FILTER_SK_LP6, "Low-pass 36dB" },
      { FILTER_SK_LP8, "Low-pass 48dB" },
      { FILTER_SK_BP2, "Band-pass 6dB" },
      { FILTER_SK_BP4, "Band-pass 12dB" },
      { FILTER_SK_BP6, "Band-pass 18dB" },
      { FILTER_SK_BP8, "Band-pass 24dB" },
      { FILTER_SK_HP1, "High-pass 6dB" },
      { FILTER_SK_HP2, "High-pass 12dB" },
      { FILTER_SK_HP3, "High-pass 18dB" },
      { FILTER_SK_HP4, "High-pass 24dB" },
      { FILTER_SK_HP6, "High-pass 36dB" },
      { FILTER_SK_HP8, "High-pass 48dB" },
    });
  add_property (&m_config.filter, P_FILTER, "Enable Filter", false);
  add_property_enum (&m_config.filter_type, P_FILTER_TYPE, "Filter Type", FILTER_TYPE_LADDER, filter_type_enum_info);
  add_property_enum (&m_config.filter_ladder_mode, P_FILTER_LADDER_MODE, "Filter Ladder Mode", FILTER_LADDER_LP2, filter_ladder_mode_enum_info);
  add_property_enum (&m_config.filter_sk_mode, P_FILTER_SK_MODE, "Filter SK Mode", FILTER_SK_LP3, filter_sk_mode_enum_info);
  add_property (&m_config.filter_attack, P_FILTER_ATTACK, "Attack", "%.1f %%", 15, 0, 100);
  add_property (&m_config.filter_decay, P_FILTER_DECAY, "Decay", "%.1f %%", 50, 0, 100);
  add_property (&m_config.filter_sustain, P_FILTER_SUSTAIN, "Sustain", "%.1f %%", 30, 0, 100);
  add_property (&m_config.filter_release, P_FILTER_RELEASE, "Release", "%.1f %%", 50, 0, 100);
  add_property (&m_config.filter_depth, P_FILTER_DEPTH, "Depth", "%.1f st", 36, -96, 96);
  add_property (&m_config.filter_key_tracking, P_FILTER_KEY_TRACKING, "Key Tracking", "%.1f %%", 50, 0, 100);

  double cutoff_min = 20;
  double cutoff_max = 30000;
  auto cutoff_property = add_property_log (&m_config.filter_cutoff_mod, P_FILTER_CUTOFF, "Cutoff", "%.1f Hz", 500, cutoff_min, cutoff_max);
  cutoff_property->set_modulation_range_ui ((std::log2 (cutoff_max) - std::log2 (cutoff_min)) * 12); // semi tones

  add_property (&m_config.filter_resonance_mod, P_FILTER_RESONANCE, "Resonance", "%.1f %%", 30, 0, 100);
  add_property (&m_config.filter_drive_mod, P_FILTER_DRIVE, "Drive", "%.1f dB", 0, -24, 36);

  add_property (&m_config.portamento, P_PORTAMENTO, "Enable Portamento (Mono)", false);
  add_property_xparam (&m_config.portamento_glide, P_PORTAMENTO_GLIDE, "Glide", "%.2f ms", 200, 0, 1000, 3);

  add_property (&m_config.vibrato, P_VIBRATO, "Enable Vibrato", false);
  add_property (&m_config.vibrato_depth, P_VIBRATO_DEPTH, "Depth", "%.2f Cent", 10, 0, 50);
  add_property_log (&m_config.vibrato_frequency, P_VIBRATO_FREQUENCY, "Frequency", "%.3f Hz", 4, 1, 15);
  add_property (&m_config.vibrato_attack, P_VIBRATO_ATTACK, "Attack", "%.2f ms", 0, 0, 1000);
}

const char *
MorphOutput::type()
{
  return "SpectMorph::MorphOutput";
}

int
MorphOutput::insert_order()
{
  return 1000;
}

bool
MorphOutput::save (OutFile& out_file)
{
  write_properties (out_file);

  for (size_t i = 0; i < m_config.channel_ops.size(); i++)
    {
      string name;

      if (m_config.channel_ops[i])   // NULL pointer => name = ""
        name = m_config.channel_ops[i].get()->name();

      out_file.write_string ("channel", name);
    }

  return true;
}

bool
MorphOutput::load (InFile& ifile)
{
  bool read_ok = true;
  load_channel_op_names.clear();

  while (ifile.event() != InFile::END_OF_FILE)
    {
      if (read_property_event (ifile))
        {
          // property has been read, so we ignore the event
        }
      else if (ifile.event() == InFile::STRING)
        {
          if (ifile.event_name() == "channel")
            {
              load_channel_op_names.push_back (ifile.event_data());
            }
          else
            {
              report_bad_event (read_ok, ifile);
            }
        }
      else
        {
          report_bad_event (read_ok, ifile);
        }
      ifile.next_event();
    }
  return read_ok;
}

void
MorphOutput::post_load (OpNameMap& op_name_map)
{
  for (size_t i = 0; i < m_config.channel_ops.size(); i++)
    {
      if (i < load_channel_op_names.size())
        {
          string name = load_channel_op_names[i];
          m_config.channel_ops[i].set (op_name_map[name]);
        }
    }
}

MorphOperator::OutputType
MorphOutput::output_type()
{
  return OUTPUT_NONE;
}

void
MorphOutput::set_channel_op (int ch, MorphOperator *op)
{
  assert (ch >= 0 && ch < CHANNEL_OP_COUNT);

  m_config.channel_ops[ch].set (op);
  m_morph_plan->emit_plan_changed();
}

MorphOperator *
MorphOutput::channel_op (int ch)
{
  assert (ch >= 0 && ch < CHANNEL_OP_COUNT);

  return m_config.channel_ops[ch].get();
}

void
MorphOutput::on_operator_removed (MorphOperator *op)
{
  for (size_t ch = 0; ch < m_config.channel_ops.size(); ch++)
    {
      if (m_config.channel_ops[ch].get() == op)
        m_config.channel_ops[ch].set (nullptr);
    }
}

vector<MorphOperator *>
MorphOutput::dependencies()
{
  vector<MorphOperator *> deps;

  for (auto& ptr : m_config.channel_ops)
    deps.push_back (ptr.get());

  if (m_config.filter)
    get_property_dependencies (deps, { P_FILTER_CUTOFF, P_FILTER_RESONANCE });
  return deps;
}

MorphOperatorConfig *
MorphOutput::clone_config()
{
  Config *cfg = new Config (m_config);
  return cfg;
}
