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

#include "smmath.hh"
#include <algorithm>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include <string>

using namespace SpectMorph;
using std::string;

double
gettime ()
{
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void
perftest()
{
  const size_t TEST_SIZE = 3000;
  const int REPS = 20000;

  double sin_vec[TEST_SIZE], cos_vec[TEST_SIZE];
  double start_time, end_time;

  VectorSinParams params;

  params.freq = 440;
  params.mix_freq = 44100;
  params.phase = 0.12345;
  params.mag = 0.73;

  start_time = gettime();
  for (int reps = 0; reps < REPS; reps++)
    {
      for (int i = 0; i < TEST_SIZE; i++)
        {
          const double phase = params.freq / params.mix_freq * 2.0 * M_PI * i + params.phase;
          double s, c;
          sincos (phase, &s, &c);
          sin_vec[i] = s * params.mag;
          cos_vec[i] = c * params.mag;
        }
      volatile double yy = sin_vec[reps & 1023] + cos_vec[reps & 2047]; // do not optimize loop out
    }
  end_time = gettime();

  // assume 2Ghz processor and compute cycles per value
  printf ("libm sincos: %f pseudocycles per value\n", (end_time - start_time) * 2.0 * 1000 * 1000 * 1000 / (TEST_SIZE * REPS));

  start_time = gettime();
  for (int reps = 0; reps < REPS; reps++)
    {
      std::fill (sin_vec, sin_vec + TEST_SIZE, 0);
      std::fill (cos_vec, cos_vec + TEST_SIZE, 0);
      fast_vector_sincos_add (params, sin_vec, sin_vec + TEST_SIZE, cos_vec);

      volatile double yy = sin_vec[reps & 1023] + cos_vec[reps & 2047]; // do not optimize loop out
    }
  end_time = gettime();

  // assume 2Ghz processor and compute cycles per value
  printf ("fast sincos: %f pseudocycles per value\n", (end_time - start_time) * 2.0 * 1000 * 1000 * 1000 / (TEST_SIZE * REPS));
}

int
main (int argc, char **argv)
{
  // do perf test only when the user wants it

  if (argc == 2 && string (argv[1]) == "perf")
    perftest();

  // always do accuracy test

  double freqs[] = { 40, 440, 880, 1024, 3000, 9000, 12345, -1 };
  double rates[] = { 44100, 48000, 96000, -1 };
  double phases[] = { -0.9, 0.0, 0.3, M_PI * 0.5, M_PI * 0.95, -100 };
  double mags[] = { 0.01, 0.1, 0.54321, 1.0, 2.24, -1 };

  const size_t TEST_SIZE = 3000;

  double max_err = 0.0;

  for (int f = 0; freqs[f] > 0; f++)
    {
      for (int r = 0; rates[r] > 0; r++)
        {
          for (int p = 0; phases[p] > -99; p++)
            {
              for (int m = 0; mags[m] > 0; m++)
                {
                  VectorSinParams params;

                  params.freq = freqs[f];
                  params.mix_freq = rates[r];
                  params.phase = phases[p];
                  params.mag = mags[m];

                  // compute correct result
                  double libm_sin_vec[TEST_SIZE], libm_cos_vec[TEST_SIZE];

                  for (int i = 0; i < TEST_SIZE; i++)
                    {
                      const double phase = params.freq / params.mix_freq * 2.0 * M_PI * i + params.phase;
                      double s, c;
                      sincos (phase, &s, &c);
                      libm_sin_vec[i] = s * params.mag;
                      libm_cos_vec[i] = c * params.mag;
                    }

                  // test fast approximation
                  double sin_vec[TEST_SIZE], cos_vec[TEST_SIZE];

                  std::fill (sin_vec, sin_vec + TEST_SIZE, 0);
                  fast_vector_sin_add (params, sin_vec, sin_vec + TEST_SIZE);

                  for (int i = 0; i < TEST_SIZE; i++)
                    max_err = std::max (max_err, fabs (sin_vec[i] - libm_sin_vec[i]));

                  std::fill (sin_vec, sin_vec + TEST_SIZE, 0);
                  std::fill (cos_vec, cos_vec + TEST_SIZE, 0);
                  fast_vector_sincos_add (params, sin_vec, sin_vec + TEST_SIZE, cos_vec);

                  for (int i = 0; i < TEST_SIZE; i++)
                    {
                      max_err = std::max (max_err, fabs (sin_vec[i] - libm_sin_vec[i]));
                      max_err = std::max (max_err, fabs (cos_vec[i] - libm_cos_vec[i]));
                    }

                }
            }
        }
    }
  printf ("testfastsin: maximum error: %.17g\n", max_err);
  assert (max_err < 3e-12);
}
