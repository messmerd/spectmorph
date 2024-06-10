// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#include "smmorphoutputmodule.hh"
#include "smmorphoutput.hh"
#include "smmorphplan.hh"
#include <glib.h>
#include <assert.h>

using namespace SpectMorph;

using std::string;
using std::vector;

MorphOutputModule::MorphOutputModule (MorphPlanVoice *voice) :
  MorphOperatorModule (voice),
  decoder (this,  morph_plan_voice->mix_freq())
{
}

void
MorphOutputModule::set_config (const MorphOperatorConfig *op_cfg)
{
  cfg = dynamic_cast<const MorphOutput::Config *> (op_cfg);
  g_return_if_fail (cfg != NULL);

  MorphOperatorModule *mod = morph_plan_voice->module (cfg->channel_ops[0]);
  LiveDecoderSource *source = mod ? mod->source() : nullptr;

  /* since the source is part of a module (and modules get newly created in
   * main thread and then replaced in audio thread), comparing the pointer to
   * the source in the LiveDecoder is enough to see if the source changed
   */
  decoder.set_config (cfg, source, morph_plan_voice->mix_freq());
}

bool
MorphOutputModule::portamento() const
{
  return cfg->portamento;
}

float
MorphOutputModule::portamento_glide() const
{
  return cfg->portamento_glide;
}

float
MorphOutputModule::velocity_sensitivity() const
{
  return cfg->velocity_sensitivity;
}

int
MorphOutputModule::pitch_bend_range() const
{
  return cfg->pitch_bend_range;
}

float
MorphOutputModule::filter_cutoff_mod() const
{
  return apply_modulation (cfg->filter_cutoff_mod);
}

float
MorphOutputModule::filter_resonance_mod() const
{
  return apply_modulation (cfg->filter_resonance_mod);
}

float
MorphOutputModule::filter_drive_mod() const
{
  return apply_modulation (cfg->filter_drive_mod);
}

void
MorphOutputModule::process (const TimeInfoGenerator& time_info_gen, RTMemoryArea& rt_memory_area, size_t n_samples, float **values, size_t n_ports, const float *freq_in)
{
  const bool have_cycle = morph_plan_voice->morph_plan_synth()->have_cycle();

  this->time_info_gen = &time_info_gen;
  m_rt_memory_area = &rt_memory_area;

  if (!have_cycle)
    decoder.process (rt_memory_area, n_samples, freq_in, values[0]);
  else
    zero_float_block (n_samples, values[0]);

  this->time_info_gen = nullptr;
  m_rt_memory_area = nullptr;
}

RTMemoryArea *
MorphOutputModule::rt_memory_area() const
{
  return m_rt_memory_area;
}

TimeInfo
MorphOutputModule::compute_time_info() const
{
  assert (time_info_gen);
  return time_info_gen->time_info (decoder.time_offset_ms());
}

void
MorphOutputModule::retrigger (const TimeInfo& time_info, int channel, float freq, int midi_velocity)
{
  const bool have_cycle = morph_plan_voice->morph_plan_synth()->have_cycle();
  if (have_cycle)
    return;

  decoder.retrigger (channel, freq, midi_velocity);
  morph_plan_voice->note_on (time_info);
}

void
MorphOutputModule::release()
{
  decoder.release();
  morph_plan_voice->note_off();
}

bool
MorphOutputModule::done()
{
  // done means: the signal will be only zeros from here
  return decoder.done();
}
