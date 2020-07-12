// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphwavsourcemodule.hh"
#include "smmorphwavsource.hh"
#include "smmorphplan.hh"
#include "smleakdebugger.hh"
#include "sminstrument.hh"
#include "smwavsetbuilder.hh"
#include "smmorphplanvoice.hh"
#include <glib.h>
#include <thread>

using namespace SpectMorph;

using std::string;
using std::vector;
using std::max;

static LeakDebugger leak_debugger ("SpectMorph::MorphWavSourceModule");

static float
freq_to_note (float freq)
{
  return 69 + 12 * log (freq / 440) / log (2);
}

void
MorphWavSourceModule::InstrumentSource::retrigger (int channel, float freq, int midi_velocity, float mix_freq)
{
  Audio  *best_audio = nullptr;
  float   best_diff  = 1e10;

  // we can not delete the old wav_set between retrigger() invocations
  //  - LiveDecoder may keep a pointer to contained Audio* entries (which die if the WavSet is freed)
  wav_set = project->get_wav_set (object_id);

  if (wav_set)
    {
      float note = freq_to_note (freq);
      for (vector<WavSetWave>::iterator wi = wav_set->waves.begin(); wi != wav_set->waves.end(); wi++)
        {
          Audio *audio = wi->audio;
          if (audio && wi->channel == channel &&
                       wi->velocity_range_min <= midi_velocity &&
                       wi->velocity_range_max >= midi_velocity)
            {
              float audio_note = freq_to_note (audio->fundamental_freq);
              if (fabs (audio_note - note) < best_diff)
                {
                  best_diff = fabs (audio_note - note);
                  best_audio = audio;
                }
            }
        }
    }
  active_audio = best_audio;
}

Audio*
MorphWavSourceModule::InstrumentSource::audio()
{
  return active_audio;
}

AudioBlock *
MorphWavSourceModule::InstrumentSource::audio_block (size_t index)
{
  if (active_audio && module->play_mode == MorphWavSource::PLAY_MODE_CUSTOM_POSITION)
    {
      const double position = module->morph_plan_voice->control_input (module->position, module->position_control_type, module->position_mod);

      int start, end;
      if (active_audio->loop_type == Audio::LOOP_NONE)
        {
          // play everything
          start = 0;
          end = active_audio->contents.size() - 1;
        }
      else
        {
          // play loop
          start = active_audio->loop_start;
          end = active_audio->loop_end;
        }
      const double frac = (position + 1) / 2;
      index = sm_bound (start, sm_round_positive ((1 - frac) * start + frac * end), end);
    }
  if (active_audio && index < active_audio->contents.size())
    return &active_audio->contents[index];
  else
    return nullptr;
}

void
MorphWavSourceModule::InstrumentSource::update_object_id (int object_id)
{
  this->object_id = object_id;
}

void
MorphWavSourceModule::InstrumentSource::update_project (Project *p)
{
  project = p;
}

MorphWavSourceModule::MorphWavSourceModule (MorphPlanVoice *voice) :
  MorphOperatorModule (voice)
{
  my_source.module = this;

  leak_debugger.add (this);
}

MorphWavSourceModule::~MorphWavSourceModule()
{
  leak_debugger.del (this);
}

LiveDecoderSource *
MorphWavSourceModule::source()
{
  return &my_source;
}

void
MorphWavSourceModule::set_config (MorphOperator *op)
{
  MorphWavSource *source = dynamic_cast<MorphWavSource *> (op);
  Project *project = op->morph_plan()->project();

  my_source.update_project (project);
  my_source.update_object_id (source->object_id());
  position = source->position();
  position_control_type = source->position_control_type();
  play_mode = source->play_mode();

  MorphOperator *position_op = source->position_op();
  if (position_op && play_mode == MorphWavSource::PLAY_MODE_CUSTOM_POSITION)
    position_mod = morph_plan_voice->module (position_op);
  else
    position_mod = nullptr;

  clear_dependencies();
  add_dependency (position_mod);
}
