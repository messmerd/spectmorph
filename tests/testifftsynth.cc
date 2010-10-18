/*
 * Copyright (C) 2010 Stefan Westerfeld
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

#include "smifftsynth.hh"
#include "smsinedecoder.hh"
#include "smmath.hh"
#include "smmain.hh"
#include "smfft.hh"

#include <birnet/birnetutils.hh>
#include <bse/bsemathsignal.h>

#include <vector>
#include <stdio.h>
#include <sys/time.h>

using namespace SpectMorph;

using std::vector;
using std::max;
using std::min;
using Birnet::AlignedArray;

double
gettime()
{
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void
perf_test()
{
  const double mix_freq = 48000;
  const size_t block_size = 1024;

  vector<float> samples (block_size);
  vector<float> window (block_size);

  IFFTSynth synth (block_size, mix_freq, IFFTSynth::WIN_HANNING);

  const double freq  = 440;
  const double mag   = 0.1;
  const double phase = 0.7;
  const double clocks_per_sec = 2500.0 * 1000 * 1000;

  int RUNS = 1000 * 1000 * 10;
  double start, end;

  synth.clear_partials();
  start = gettime();
  for (int r = 0; r < RUNS; r++)
    synth.render_partial (freq, mag, phase);
  end = gettime();

  printf ("render_partial: clocks per sample: %f\n", clocks_per_sec * (end - start) / RUNS / block_size);

  AlignedArray<float, 16> sse_samples (block_size);

  synth.get_samples (&sse_samples[0]);  // first run may be slower

  RUNS = 100000;
  start = gettime();
  for (int r = 0; r < RUNS; r++)
    synth.get_samples (&sse_samples[0]);
  end = gettime();

  printf ("get_samples: clocks per sample: %f\n", clocks_per_sec * (end - start) / RUNS / block_size);

  SineDecoder sd (mix_freq, block_size, block_size / 2, SineDecoder::MODE_PHASE_SYNC_OVERLAP_IFFT);
  AudioBlock b, next_b;
  vector<double> dwindow (window.begin(), window.end());
  RUNS = 10000;

  int FREQS = 1000;
  for (int i = 0; i < FREQS; i++)
    {
      b.freqs.push_back (440 + i);
      b.phases.push_back (0.1);
      b.phases.push_back (0.9);
    }
  start = gettime();
  for (int r = 0; r < RUNS; r++)
    sd.process (b, next_b, dwindow, samples);
  end = gettime();
  printf ("SineDecoder (%d partials): clocks per sample: %f\n", FREQS, clocks_per_sec * (end - start) / FREQS / RUNS / block_size);

  SineDecoder sdo (mix_freq, block_size, block_size / 2, SineDecoder::MODE_PHASE_SYNC_OVERLAP);
  RUNS = 2000;

  start = gettime();
  for (int r = 0; r < RUNS; r++)
    sdo.process (b, next_b, dwindow, samples);
  end = gettime();
  printf ("Old SineDecoder (%d partials): clocks per sample: %f\n", FREQS, clocks_per_sec * (end - start) / FREQS / RUNS / block_size);
}

void
accuracy_test (double freq, double mag, double phase, double mix_freq)
{
  const size_t block_size = 1024;

  vector<float> spectrum (block_size);
  vector<float> samples (block_size);
  vector<float> window (block_size);

  for (size_t i = 0; i < window.size(); i++)
    window[i] = window_blackman_harris_92 (2.0 * i / block_size - 1.0);

  IFFTSynth synth (block_size, mix_freq, IFFTSynth::WIN_BLACKMAN_HARRIS_92);

  synth.clear_partials();
  synth.render_partial (freq, mag, phase);
  synth.get_samples (&samples[0]);

  VectorSinParams vsparams;
  vsparams.mix_freq = mix_freq;
  vsparams.freq = freq;
  vsparams.mag = mag;
  vsparams.phase = phase;
  vsparams.mode = VectorSinParams::REPLACE;

  AlignedArray<float, 16> aligned_decoded_sines (block_size);
  fast_vector_sinf (vsparams, &aligned_decoded_sines[0], &aligned_decoded_sines[block_size]);

  double max_diff = 0;
#if 0
  for (size_t i = 0; i < block_size; i++)
    {
      max_diff = max (max_diff, double (samples[i]) - aligned_decoded_sines[i] * window[i]);
      //printf ("%zd %.17g %.17g\n", i, samples[i], aligned_decoded_sines[i] * window[i]);
    }
  printf ("# max_diff(nq) = %.17g\n", max_diff);
#endif

  vsparams.freq = synth.quantized_freq (freq);
  fast_vector_sinf (vsparams, &aligned_decoded_sines[0], &aligned_decoded_sines[block_size]);

  synth.clear_partials();
  synth.render_partial (freq, mag, phase);
  synth.get_samples (&samples[0]);

  //printf ("# qfreq = %.17g\n", vsparams.freq);
  max_diff = 0;
  for (size_t i = 0; i < block_size; i++)
    {
      max_diff = max (max_diff, double (samples[i]) - aligned_decoded_sines[i] * window[i]);
      //printf ("%zd %.17g %.17g\n", i, samples[i], aligned_decoded_sines[i] * window[i]);
    }
  printf ("%f %.17g\n", freq, max_diff);
}

void
test_accs()
{
  const double mix_freq = 48000;
  const size_t block_size = 1024;

  vector<double> window (block_size);
  vector<float> samples (block_size);
  vector<float> osamples (block_size);

  for (size_t i = 0; i < window.size(); i++)
    window[i] = window_blackman_harris_92 (2.0 * i / block_size - 1.0);

  SineDecoder sd (mix_freq, block_size, block_size / 2, SineDecoder::MODE_PHASE_SYNC_OVERLAP_IFFT);
  AudioBlock b, next_b;
  vector<double> dwindow (window.begin(), window.end());

  b.freqs.push_back (IFFTSynth (block_size, mix_freq, IFFTSynth::WIN_BLACKMAN_HARRIS_92).quantized_freq (440));
  b.phases.push_back (0.1);
  b.phases.push_back (0.9);

  sd.process (b, next_b, dwindow, samples);

  SineDecoder sdo (mix_freq, block_size, block_size / 2, SineDecoder::MODE_PHASE_SYNC_OVERLAP);
  sdo.process (b, next_b, dwindow, osamples);

  double max_diff = 0;
  for (size_t i = 0; i < block_size; i++)
    {
      printf ("%d %.17g %.17g\n", i, samples[i], osamples[i]);
      max_diff = max (max_diff, fabs (samples[i] - osamples[i]));
    }
  printf ("# max_diff = %.17g\n", max_diff);
}

void
test_spect()
{
  size_t block_size = 1024;
  double mix_freq = 48000;
  double freq = 440;
  double phase = 0;

  vector<float> samples (block_size * 100);

  IFFTSynth synth (block_size, mix_freq, IFFTSynth::WIN_HANNING);
  freq = synth.quantized_freq (freq);
  for (int block = 0; (block * block_size / 2 + block_size) < samples.size(); block++)
    {
      synth.clear_partials();
      synth.render_partial (freq, 1, phase);
      synth.get_samples (&samples[block * block_size / 2], IFFTSynth::ADD);
      phase += (freq / mix_freq) * 2 * M_PI * block_size / 2;
    }
  size_t power2 = 1;
  while (power2 * 2 < (samples.size() - block_size * 2))
    power2 *= 2;
  vector<float> wsig;
  for (size_t i = 0; i < power2; i++)
    wsig.push_back (bse_window_blackman (2.0 * i / power2 - 1.0) * samples[block_size + i]);

  //for (size_t i = 0; i < power2; i++)
  //  printf ("%d %f\n", i, wsig[i]);

  vector<float> spect (power2);
  FFT::fftar_float (power2, &wsig[0], &spect[0]);

  double max_mag = 0;
  for (size_t d = 0; d < power2; d += 2)
    {
      double re = spect[d];
      double im = spect[d+1];
      max_mag = max (sqrt (re * re + im * im), max_mag);
    }
  for (size_t d = 0; d < power2; d += 2)
    {
      double re = spect[d] / max_mag;
      double im = spect[d+1] / max_mag;
      printf ("%f %.17g\n", (d * mix_freq / 2.0) / power2, sqrt (re * re + im * im));
    }
}

int
main (int argc, char **argv)
{
  sm_init (&argc, &argv);

  if (argc == 2 && strcmp (argv[1], "perf") == 0)
    {
      perf_test();
      return 0;
    }
  if (argc == 2 && strcmp (argv[1], "accs") == 0)
    {
      test_accs();
      return 0;
    }
  if (argc == 2 && strcmp (argv[1], "spect") == 0)
    {
      test_spect();
      return 0;
    }
  const double mag = 0.991;
  const double phase = 0.5;
  accuracy_test (187, mag, phase, 48000);
  for (double freq = 20; freq < 24000; freq = min (freq * 1.01, freq + 0.5))
    accuracy_test (freq, mag, phase, 48000);
}
