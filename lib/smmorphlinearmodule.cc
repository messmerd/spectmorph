// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#include "smmorphlinearmodule.hh"
#include "smmorphlinear.hh"
#include "smmorphplan.hh"
#include "smmorphplanvoice.hh"
#include "smmath.hh"
#include "smleakdebugger.hh"
#include "smlivedecoder.hh"
#include "smmorphutils.hh"
#include "smutils.hh"
#include "smrtmemory.hh"
#include <glib.h>
#include <assert.h>

using namespace SpectMorph;

using std::string;
using std::vector;
using std::min;
using std::max;
using std::sort;
using SpectMorph::MorphUtils::md_cmp;
using SpectMorph::MorphUtils::MagData;

static LeakDebugger leak_debugger ("SpectMorph::MorphLinearModule");

#define DEBUG (0)

MorphLinearModule::MorphLinearModule (MorphPlanVoice *voice) :
  MorphOperatorModule (voice)
{
  my_source.module = this;

  audio.fundamental_freq     = 440;
  audio.mix_freq             = 48000;
  audio.frame_size_ms        = 1;
  audio.frame_step_ms        = 1;
  audio.zeropad              = 4;
  audio.loop_type            = Audio::LOOP_NONE;

  leak_debugger.add (this);
}

MorphLinearModule::~MorphLinearModule()
{
  leak_debugger.del (this);
}

void
MorphLinearModule::set_config (const MorphOperatorConfig *op_cfg)
{
  cfg = dynamic_cast<const MorphLinear::Config *> (op_cfg);
  g_return_if_fail (cfg != NULL);

  left_mod = morph_plan_voice->module (cfg->left_op);
  right_mod = morph_plan_voice->module (cfg->right_op);

  have_left_source = cfg->left_wav_set != nullptr;
  if (have_left_source)
    left_source.set_wav_set (cfg->left_wav_set);

  have_right_source = cfg->right_wav_set != nullptr;
  if (have_right_source)
    right_source.set_wav_set (cfg->right_wav_set);
}

void
MorphLinearModule::MySource::retrigger (int channel, float freq, int midi_velocity)
{
  if (module->left_mod && module->left_mod->source())
    {
      module->left_mod->source()->retrigger (channel, freq, midi_velocity);
    }

  if (module->right_mod && module->right_mod->source())
    {
      module->right_mod->source()->retrigger (channel, freq, midi_velocity);
    }

  if (module->have_left_source)
    {
      module->left_source.retrigger (channel, freq, midi_velocity);
    }

  if (module->have_right_source)
    {
      module->right_source.retrigger (channel, freq, midi_velocity);
    }
}

Audio*
MorphLinearModule::MySource::audio()
{
  return &module->audio;
}

static void
dump_block (size_t index, const char *what, const RTAudioBlock& block)
{
  if (DEBUG)
    {
      for (size_t i = 0; i < block.freqs.size(); i++)
        sm_printf ("%zd:%s %.17g %.17g\n", index, what, block.freqs_f (i), block.mags_f (i));
    }
}

static void
dump_line (size_t index, const char *what, double start, double end)
{
  if (DEBUG)
    {
      sm_printf ("%zd:%s %.17g %.17g\n", index, what, start, end);
    }
}

bool
MorphLinearModule::MySource::rt_audio_block (size_t index, RTAudioBlock& out_audio_block)
{
  bool have_left = false, have_right = false;

  const double morphing = module->apply_modulation (module->cfg->morphing_mod);
  const double interp = (morphing + 1) / 2; /* examples => 0: only left; 0.5 both equally; 1: only right */
  const double time_ms = index; // 1ms frame step
  const auto   morph_mode = module->cfg->db_linear ? MorphUtils::MorphMode::DB_LINEAR : MorphUtils::MorphMode::LINEAR;

  RTAudioBlock left_block (module->rt_memory_area()), right_block (module->rt_memory_area());

  Audio *left_audio = nullptr;
  Audio *right_audio = nullptr;
  if (module->left_mod && module->left_mod->source())
    {
      have_left = MorphUtils::get_normalized_block (module->left_mod->source(), time_ms, left_block);
      left_audio = module->left_mod->source()->audio();
    }

  if (module->right_mod && module->right_mod->source())
    {
      have_right = MorphUtils::get_normalized_block (module->right_mod->source(), time_ms, right_block);
      right_audio = module->right_mod->source()->audio();
    }

  if (module->have_left_source)
    {
      have_left = MorphUtils::get_normalized_block (&module->left_source, time_ms, left_block);
      left_audio = module->left_source.audio();
    }

  if (module->have_right_source)
    {
      have_right = MorphUtils::get_normalized_block (&module->right_source, time_ms, right_block);
      right_audio = module->right_source.audio();
    }

  if (have_left && have_right) // true morph: both sources present
    {
      assert (left_audio && right_audio);

      const size_t max_partials = left_block.freqs.size() + right_block.freqs.size();
      out_audio_block.freqs.set_capacity (max_partials);
      out_audio_block.mags.set_capacity (max_partials);

      dump_block (index, "A", left_block);
      dump_block (index, "B", right_block);

      MagData mds[max_partials + AVOID_ARRAY_UB];

      size_t mds_size = MorphUtils::init_mag_data (mds, left_block, right_block);
      size_t left_freqs_size = left_block.freqs.size();
      size_t right_freqs_size = right_block.freqs.size();

      MorphUtils::FreqState   left_freqs[left_freqs_size + AVOID_ARRAY_UB];
      MorphUtils::FreqState   right_freqs[right_freqs_size + AVOID_ARRAY_UB];

      init_freq_state (left_block.freqs, left_freqs);
      init_freq_state (right_block.freqs, right_freqs);

      for (size_t m = 0; m < mds_size; m++)
        {
          size_t i, j;
          bool match = false;
          if (mds[m].block == MagData::BLOCK_LEFT)
            {
              i = mds[m].index;

              if (!left_freqs[i].used)
                match = MorphUtils::find_match (left_freqs[i].freq_f, right_freqs, right_freqs_size, &j);
            }
          else // (mds[m].block == MagData::BLOCK_RIGHT)
            {
              j = mds[m].index;
              if (!right_freqs[j].used)
                match = MorphUtils::find_match (right_freqs[j].freq_f, left_freqs, left_freqs_size, &i);
            }
          if (match)
            {
              double freq;

              /* prefer frequency of louder partial */
              const double lfreq = left_block.freqs[i];
              const double rfreq = right_block.freqs[j];

              if (left_block.mags[i] > right_block.mags[j])
                {
                  const double mfact = right_block.mags_f (j) / left_block.mags_f (i);

                  freq = lfreq + mfact * interp * (rfreq - lfreq);
                }
              else
                {
                  const double mfact = left_block.mags_f (i) / right_block.mags_f (j);

                  freq = rfreq + mfact * (1 - interp) * (lfreq - rfreq);
                }

              uint16_t mag_idb;
              if (module->cfg->db_linear)
                {
                  const uint16_t lmag_idb = max (left_block.mags[i], SM_IDB_CONST_M96);
                  const uint16_t rmag_idb = max (right_block.mags[j], SM_IDB_CONST_M96);

                  mag_idb = sm_round_positive ((1 - interp) * lmag_idb + interp * rmag_idb);
                }
              else
                {
                  mag_idb = sm_factor2idb ((1 - interp) * left_block.mags_f (i) + interp * right_block.mags_f (j));
                }
              out_audio_block.freqs.push_back (freq);
              out_audio_block.mags.push_back (mag_idb);

              dump_line (index, "L", left_block.freqs[i], right_block.freqs[j]);
              left_freqs[i].used = 1;
              right_freqs[j].used = 1;
            }
        }
      for (size_t i = 0; i < left_block.freqs.size(); i++)
        {
          if (!left_freqs[i].used)
            {
              out_audio_block.freqs.push_back (left_block.freqs[i]);
              out_audio_block.mags.push_back (left_block.mags[i]);

              interp_mag_one (interp, &out_audio_block.mags.back(), NULL, morph_mode);
            }
        }
      for (size_t i = 0; i < right_block.freqs.size(); i++)
        {
          if (!right_freqs[i].used)
            {
              out_audio_block.freqs.push_back (right_block.freqs[i]);
              out_audio_block.mags.push_back (right_block.mags[i]);

              interp_mag_one (interp, NULL, &out_audio_block.mags.back(), morph_mode);
            }
        }
      assert (left_block.noise.size() == right_block.noise.size());

      out_audio_block.noise.set_capacity (left_block.noise.size());
      for (size_t i = 0; i < left_block.noise.size(); i++)
        out_audio_block.noise.push_back (sm_factor2idb ((1 - interp) * left_block.noise_f (i) + interp * right_block.noise_f (i)));

      out_audio_block.sort_freqs();

      return true;
    }
  else if (have_left) // only left source output present
    {
      MorphUtils::morph_scale (out_audio_block, left_block, 1 - interp, morph_mode);
      return true;
    }
  else if (have_right) // only right source output present
    {
      MorphUtils::morph_scale (out_audio_block, right_block, interp, morph_mode);
      return true;
    }
  else
    {
      return false;
    }
}

LiveDecoderSource *
MorphLinearModule::source()
{
  return &my_source;
}
