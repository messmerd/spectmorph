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

#ifndef SPECTMORPH_MATH_HH
#define SPECTMORPH_MATH_HH

#include <math.h>
#include <glib.h>
#ifdef __SSE__
#include <xmmintrin.h>
#endif

namespace SpectMorph
{

struct
VectorSinParams
{
  VectorSinParams() :
    mix_freq (-1),
    freq (-1),
    phase (-100),
    mag (-1)
  {
  }
  double mix_freq;
  double freq;
  double phase;
  double mag;
};


template<class Iterator>
inline void
fast_vector_sin_add (const VectorSinParams& params, Iterator begin, Iterator end)
{
  g_return_if_fail (params.mix_freq > 0 && params.freq > 0 && params.phase > -99 && params.mag > 0);

  const double phase_inc = params.freq / params.mix_freq * 2 * M_PI;
  const double inc_re = cos (phase_inc);
  const double inc_im = sin (phase_inc);
  int n = 0;

  double state_re;
  double state_im;

  sincos (params.phase, &state_im, &state_re);
  state_re *= params.mag;
  state_im *= params.mag;

  for (Iterator x = begin; x != end; x++)
    {
      *x += state_im;
      if ((n++ & 255) == 255)
        {
          sincos (phase_inc * n + params.phase, &state_im, &state_re);
          state_re *= params.mag;
          state_im *= params.mag;
        }
      else
        {
          /*
           * (state_re + i * state_im) * (inc_re + i * inc_im) =
           *   state_re * inc_re - state_im * inc_im + i * (state_re * inc_im + state_im * inc_re)
           */
          const double re = state_re * inc_re - state_im * inc_im;
          const double im = state_re * inc_im + state_im * inc_re;
          state_re = re;
          state_im = im;
        }
    }
}

template<class Iterator>
inline void
fast_vector_sincos_add (const VectorSinParams& params, Iterator sin_begin, Iterator sin_end, Iterator cos_begin)
{
  g_return_if_fail (params.mix_freq > 0 && params.freq > 0 && params.phase > -99 && params.mag > 0);

  const double phase_inc = params.freq / params.mix_freq * 2 * M_PI;
  const double inc_re = cos (phase_inc);
  const double inc_im = sin (phase_inc);
  int n = 0;

  double state_re;
  double state_im;

  sincos (params.phase, &state_im, &state_re);
  state_re *= params.mag;
  state_im *= params.mag;

  for (Iterator x = sin_begin, y = cos_begin; x != sin_end; x++, y++)
    {
      *x += state_im;
      *y += state_re;
      if ((n++ & 255) == 255)
        {
          sincos (phase_inc * n + params.phase, &state_im, &state_re);
          state_re *= params.mag;
          state_im *= params.mag;
        }
      else
        {
          /*
           * (state_re + i * state_im) * (inc_re + i * inc_im) =
           *   state_re * inc_re - state_im * inc_im + i * (state_re * inc_im + state_im * inc_re)
           */
          const double re = state_re * inc_re - state_im * inc_im;
          const double im = state_re * inc_im + state_im * inc_re;
          state_re = re;
          state_im = im;
        }
    }
}

/* see: http://ds9a.nl/gcc-simd/ */
union F4Vector
{
  float f[4];
#ifdef __SSE__
  __m128 v;   // vector of four single floats
#endif
};

inline void
float_fast_vector_sincos_add (const VectorSinParams& params, float *sin_begin, float *sin_end, float *cos_begin)
{
#ifdef __SSE__
  g_return_if_fail (params.mix_freq > 0 && params.freq > 0 && params.phase > -99 && params.mag > 0);

  const int TABLE_SIZE = 32;

  const double phase_inc = params.freq / params.mix_freq * 2 * M_PI;
  const double inc_re16 = cos (phase_inc * TABLE_SIZE * 4);
  const double inc_im16 = sin (phase_inc * TABLE_SIZE * 4);
  int n = 0;

  double state_re;
  double state_im;

  sincos (params.phase, &state_im, &state_re);
  state_re *= params.mag;
  state_im *= params.mag;


  F4Vector incf_re[TABLE_SIZE] = {0, };
  F4Vector incf_im[TABLE_SIZE] = {0, };

  // compute tables using FPU
  VectorSinParams table_params = params;
  table_params.phase = 0;
  table_params.mag = 1;
  fast_vector_sincos_add (table_params, incf_im[0].f, incf_im[0].f + (TABLE_SIZE * 4), incf_re[0].f);

  // inner loop using SSE instructions
  int todo = sin_end - sin_begin;
  while (todo >= 4 * TABLE_SIZE)
    {
      F4Vector sf_re;
      F4Vector sf_im;
      sf_re.f[0] = state_re;
      sf_re.f[1] = state_re;
      sf_re.f[2] = state_re;
      sf_re.f[3] = state_re;
      sf_im.f[0] = state_im;
      sf_im.f[1] = state_im;
      sf_im.f[2] = state_im;
      sf_im.f[3] = state_im;

      /*
       * formula for complex multiplication:
       *
       * (state_re + i * state_im) * (inc_re + i * inc_im) =
       *   state_re * inc_re - state_im * inc_im + i * (state_re * inc_im + state_im * inc_re)
       */
      F4Vector *new_im = reinterpret_cast<F4Vector *> (sin_begin + n);
      F4Vector *new_re = reinterpret_cast<F4Vector *> (cos_begin + n);
      for (int k = 0; k < TABLE_SIZE; k++)
        {
          new_re[k].v = _mm_add_ps (new_re[k].v, _mm_sub_ps (_mm_mul_ps (sf_re.v, incf_re[k].v),
                                                             _mm_mul_ps (sf_im.v, incf_im[k].v)));
          new_im[k].v = _mm_add_ps (new_im[k].v, _mm_add_ps (_mm_mul_ps (sf_re.v, incf_im[k].v),
                                                 _mm_mul_ps (sf_im.v, incf_re[k].v)));
        }

      n += 4 * TABLE_SIZE;

      /*
       * (state_re + i * state_im) * (inc_re + i * inc_im) =
       *   state_re * inc_re - state_im * inc_im + i * (state_re * inc_im + state_im * inc_re)
       */
      const double re = state_re * inc_re16 - state_im * inc_im16;
      const double im = state_re * inc_im16 + state_im * inc_re16;
      state_re = re;
      state_im = im;

      todo -= 4 * TABLE_SIZE;
    }

  // compute the remaining sin/cos values using the FPU
  VectorSinParams rest_params = params;
  rest_params.phase += n * phase_inc;
  fast_vector_sincos_add (rest_params, sin_begin + n, sin_end, cos_begin + n);
#else
  fast_vector_sincos_add (params, sin_begin, sin_end, cos_begin);
#endif
}

} // namespace SpectMorph

#endif
