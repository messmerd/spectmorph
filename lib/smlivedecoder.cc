/* 
 * Copyright (C) 2009-2010 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "smlivedecoder.hh"
#include "smmath.hh"

#include <bse/bsemathsignal.h>

#include <stdio.h>

using SpectMorph::LiveDecoder;
using std::vector;

static float
freq_to_note (float freq)
{
  return 69 + 12 * log (freq / 440) / log (2);
}
static inline double
fmatch (double f1, double f2)
{
  return f2 < (f1 * 1.05) && f2 > (f1 * 0.95);
}

LiveDecoder::LiveDecoder (WavSet *smset) :
  smset (smset),
  audio (NULL),
  sine_decoder (NULL),
  noise_decoder (NULL),
  sines_enabled (true),
  noise_enabled (true),
  last_frame()
{
}

void
LiveDecoder::retrigger (int channel, float freq, float mix_freq)
{
  double best_diff = 1e10;
  Audio *best_audio = 0;

  if (smset)
    {
      float note = freq_to_note (freq);

      // find best audio candidate
      for (vector<WavSetWave>::iterator wi = smset->waves.begin(); wi != smset->waves.end(); wi++)
        {
          Audio *audio = wi->audio;
          if (audio && wi->channel == channel)
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

  audio = best_audio;

  if (best_audio)
    {
      frame_size = audio->frame_size_ms * mix_freq / 1000;
      frame_step = audio->frame_step_ms * mix_freq / 1000;
      zero_values_at_start_scaled = audio->zero_values_at_start * mix_freq / audio->mix_freq;
      loop_point = audio->loop_point;

      size_t block_size = 1;
      while (block_size < frame_size)
        block_size *= 2;

      window.resize (block_size);
      for (guint i = 0; i < window.size(); i++)
        {
          if (i < frame_size)
            window[i] = bse_window_cos (2.0 * i / frame_size - 1.0);
          else
            window[i] = 0;
        }

      if (noise_decoder)
        delete noise_decoder;
      noise_decoder = new NoiseDecoder (audio->mix_freq, mix_freq);

      if (sine_decoder)
        delete sine_decoder;
      SineDecoder::Mode mode = SineDecoder::MODE_PHASE_SYNC_OVERLAP;
      sine_decoder = new SineDecoder (mix_freq, frame_size, frame_step, mode);

      samples.resize (frame_size);
      zero_float_block (frame_size, &samples[0]);

      noise_block_size = noise_decoder->preferred_block_size();
      noise_samples.resize (noise_block_size * 2);
      zero_float_block (noise_block_size * 2, &noise_samples[0]);

      have_noise_samples = 0;
      have_samples = 0;
      pos = 0;
      frame_idx = 0;
      env_pos = 0;

      last_frame = Frame();
    }
  current_freq = freq;
  current_mix_freq = mix_freq;
}

void
LiveDecoder::process (size_t n_values, const float *freq_in, const float *freq_mod_in, float *audio_out)
{
  if (!audio)   // nothing loaded
    {
      std::fill (audio_out, audio_out + n_values, 0);
      return;
    }

  unsigned int i = 0;
  while (i < n_values)
    {
      if (have_samples == 0)
        {
          double want_freq = freq_in ? BSE_SIGNAL_TO_FREQ (freq_in[i]) : current_freq;

          std::copy (samples.begin() + frame_step, samples.end(), samples.begin());
          zero_float_block (frame_step, &samples[frame_size - frame_step]);

          if ((frame_idx + 1) < audio->contents.size())
            {
              Frame frame (audio->contents[frame_idx]);
              Frame next_frame; // not used

              for (size_t partial = 0; partial < frame.freqs.size(); partial++)
                {
                  frame.freqs[partial] *= want_freq / audio->fundamental_freq;
                  double smag = frame.phases[partial * 2];
                  double cmag = frame.phases[partial * 2 + 1];
                  double mag = sqrt (smag * smag + cmag * cmag);
                  double phase = atan2 (smag, cmag);
                  double best_fdiff = 1e12;

                  for (size_t old_partial = 0; old_partial < last_frame.freqs.size(); old_partial++)
                    {
                      if (fmatch (last_frame.freqs[old_partial], frame.freqs[partial]))
                        {
                          double lsmag = last_frame.phases[old_partial * 2];
                          double lcmag = last_frame.phases[old_partial * 2 + 1];
                          double lphase = atan2 (lsmag, lcmag);
                          double phase_delta = 2 * M_PI * last_frame.freqs[old_partial] / current_mix_freq;
                          // FIXME: I have no idea why we have to /subtract/ the phase
                          // here, and not /add/, but this way it works

                          // find best phase
                          double fdiff = fabs (last_frame.freqs[old_partial] - frame.freqs[partial]);
                          if (fdiff < best_fdiff)
                            {
                              phase = lphase - frame_step * phase_delta;
                              best_fdiff = fdiff;
                            }
                        }
                    }
                  // anti alias filter:
                  double filter_fact = 18000.0 / 44100.0;  // for 44.1 kHz, filter at 18 kHz (higher mix freq => higher filter)
                  double norm_freq   = frame.freqs[partial] / current_mix_freq;
                  if (norm_freq > filter_fact)
                    {
                      if (norm_freq > 0.5)
                        {
                          // above nyquist freq
                          mag = 0;
                        }
                      else
                        {
                          // between filter_fact and 0.5 (db linear filter)
                          const double db_at_nyquist = -60;
                          mag *= bse_db_to_factor ((norm_freq - filter_fact) / (0.5 - filter_fact) * db_at_nyquist);
                        }
                    }
                  frame.phases[partial * 2] = sin (phase) * mag;
                  frame.phases[partial * 2 + 1] = cos (phase) * mag;
                }

              last_frame = frame;

              decoded_data.resize (frame_size);

              if (sines_enabled)
                {
                  sine_decoder->process (frame, next_frame, window, decoded_data);
                  for (size_t i = 0; i < frame_size; i++)
                    samples[i] += decoded_data[i];
                }

              if (frame_idx != loop_point) /* if in loop mode: loop current frame */
                frame_idx++;
            }
          pos = 0;
          have_samples = frame_step;

          std::copy (noise_samples.begin() + frame_step, noise_samples.end(), noise_samples.begin());
          zero_float_block (frame_step, &noise_samples[noise_samples.size() - frame_step]);
          while (have_noise_samples < frame_step)
            {
              decoded_data.resize (noise_block_size);
              if (noise_enabled)
                {
                  noise_decoder->process (last_frame, window, decoded_data);
                  for (size_t i = 0; i < noise_block_size; i++)
                    noise_samples[have_noise_samples + i] += decoded_data[i];
                }
              have_noise_samples += noise_block_size / 2;
            }
          have_noise_samples -= have_samples;
        }

      g_assert (have_samples > 0);
      if (env_pos >= zero_values_at_start_scaled)
        {
          audio_out[i] = samples[pos] + noise_samples[pos];

          // decode envelope
          const double time_ms = env_pos * 1000.0 / current_mix_freq;
          if (time_ms < audio->attack_start_ms)
            {
              audio_out[i] = 0;
            }
          else if (time_ms < audio->attack_end_ms)
            {
              audio_out[i] *= (time_ms - audio->attack_start_ms) / (audio->attack_end_ms - audio->attack_start_ms);
            } // else envelope is 1

          // do not skip sample
          i++;
        }
      else
        {
          // skip sample
        }
      pos++;
      env_pos++;
      have_samples--;
    }
}

void
LiveDecoder::enable_noise (bool en)
{
  noise_enabled = en;
}

void
LiveDecoder::enable_sines (bool es)
{
  sines_enabled = es;
}
