// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphoutputmodule.hh"

using namespace SpectMorph;

namespace SpectMorph
{

class EffectDecoderSource : public LiveDecoderSource
{
  LiveDecoderSource *source;
  Audio              m_audio;
  float              m_skip;
public:
  explicit EffectDecoderSource (LiveDecoderSource *source);

  void retrigger (int channel, float freq, int midi_velocity, float mix_freq) override;
  Audio* audio() override;
  AudioBlock* audio_block (size_t index) override;

  void set_skip (float m_skip);
};

}

void
EffectDecoderSource::retrigger (int channel, float freq, int midi_velocity, float mix_freq)
{
  source->retrigger (channel, freq, midi_velocity, mix_freq);
}

Audio*
EffectDecoderSource::audio()
{
  return &m_audio;
}

AudioBlock*
EffectDecoderSource::audio_block (size_t index)
{
  Audio *audio = source->audio();
  if (!audio)
    return nullptr;

  const double time_ms = index + m_skip; // 1ms frame step
  int source_index = sm_round_positive (time_ms / audio->frame_step_ms);

  if (audio->loop_type == Audio::LOOP_FRAME_FORWARD || audio->loop_type == Audio::LOOP_FRAME_PING_PONG)
    {
      source_index = LiveDecoder::compute_loop_frame_index (source_index, audio);
    }

  return source->audio_block (source_index);
}

void
EffectDecoderSource::set_skip (float skip)
{
  m_skip = skip;
}

EffectDecoderSource::EffectDecoderSource (LiveDecoderSource *source) :
  source (source),
  m_skip (0)
{
  m_audio.fundamental_freq     = 440;
  m_audio.mix_freq             = 48000;
  m_audio.frame_size_ms        = 1;
  m_audio.frame_step_ms        = 1;
  m_audio.attack_start_ms      = 0;
  m_audio.attack_end_ms        = 0;
  m_audio.zeropad              = 4;
  m_audio.loop_type            = Audio::LOOP_NONE;
  m_audio.zero_values_at_start = 1500;              // FIXME everywhere
  m_audio.sample_count         = 2 << 31;
}

EffectDecoder::EffectDecoder (LiveDecoderSource *source) :
  source (new EffectDecoderSource (source))
{
}

EffectDecoder::~EffectDecoder()
{
}

void
EffectDecoder::set_config (MorphOutput *output, float mix_freq)
{
  chain_decoders.resize (1);

  chain_decoders[0].reset (new LiveDecoder (source));

  for (auto& dec : chain_decoders)
    {
      dec->enable_noise (output->noise());
      dec->enable_sines (output->sines());

      if (output->unison()) // unison?
        dec->set_unison_voices (output->unison_voices(), output->unison_detune());
      else
        dec->set_unison_voices (1, 0);
    }

  if (output->adsr())
    {
      source->set_skip (output->adsr_skip());

      if (!adsr_envelope)
        adsr_envelope.reset (new ADSREnvelope());

      adsr_envelope->set_config (output->adsr_attack(),
                                 output->adsr_decay(),
                                 output->adsr_sustain(),
                                 output->adsr_release(),
                                 mix_freq);
    }
  else
    {
      source->set_skip (0);
      adsr_envelope.reset();
    }
}

void
EffectDecoder::retrigger (int channel, float freq, int midi_velocity, float mix_freq)
{
  g_return_if_fail (chain_decoders.size() == 1);

  if (adsr_envelope)
    adsr_envelope->retrigger();

  chain_decoders[0]->retrigger (channel, freq, midi_velocity, mix_freq);
}

void
EffectDecoder::process (size_t       n_values,
                        const float *freq_in,
                        const float *freq_mod_in,
                        float       *audio_out)
{
  g_assert (chain_decoders.size() == 1);

  chain_decoders[0]->process (n_values, freq_in, freq_mod_in, audio_out);

  if (adsr_envelope)
    adsr_envelope->process (n_values, audio_out);
}
