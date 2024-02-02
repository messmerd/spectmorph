// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#ifndef SPECTMORPH_LIVEDECODER_HH
#define SPECTMORPH_LIVEDECODER_HH

#include "smwavset.hh"
#include "smsinedecoder.hh"
#include "smnoisedecoder.hh"
#include "smlivedecodersource.hh"
#include "smpolyphaseinter.hh"
#include "smalignedarray.hh"
#include <vector>
#include <functional>

namespace SpectMorph {

class LiveDecoderFilter;
class LiveDecoder
{
  static constexpr size_t PARTIAL_STATE_RESERVE = 2048; // maximum number of partials to expect
  static constexpr size_t MAX_N_VALUES = 64;            // maximum number of values to process at once
  static constexpr size_t MAX_UNISON_VOICES = 7;        // maximum number of unison voices

  struct PartialState
  {
    float freq;
    float mag;
    float phase;
  };
  std::vector<PartialState> pstate[2], *last_pstate;

  WavSet             *smset;
  Audio              *audio;

  size_t              block_size;
  IFFTSynth           ifft_synth;
  NoiseDecoder        noise_decoder;
  LiveDecoderSource  *source;
  PolyPhaseInter     *pp_inter;
  RTMemoryArea       *rt_memory_area = nullptr;
  LiveDecoderFilter  *filter = nullptr;
  bool                filter_latency_compensation;

  bool                sines_enabled;
  bool                noise_enabled;
  bool                debug_fft_perf_enabled;
  bool                original_samples_enabled;
  bool                loop_enabled;
  bool                start_skip_enabled;

  double              frame_step;
  size_t              zero_values_at_start_scaled;
  size_t              loop_start_scaled;
  size_t              loop_end_scaled;
  int                 loop_point;
  float               current_freq;
  float               mix_freq;

  size_t              have_samples;
  double              pos;
  double              env_pos;
  size_t              frame_idx;
  double              original_sample_pos;
  double              original_samples_norm_factor;
  float               old_portamento_stretch;

  int                 noise_seed;

  AlignedArray<float,16> sse_samples;

  // unison
  int                 unison_voices;
  std::vector<float>  unison_phases[2];
  std::vector<float>  unison_freq_factor;
  float               unison_gain;
  Random              unison_phase_random_gen;

  // vibrato
  bool                vibrato_enabled;
  float               vibrato_depth;
  float               vibrato_frequency;
  float               vibrato_attack;
  float               vibrato_phase;   // state
  float               vibrato_env;     // state

  // timing related
  double              start_env_pos = 0;
  bool                in_process    = false;

  // active/done
  enum class DoneState {
    DONE,
    ACTIVE,
    ALMOST_DONE
  };
  DoneState           done_state = DoneState::DONE;

  Audio::LoopType     get_loop_type();

  void process_internal (size_t       n_values,
                         const float *freq_in,
                         float       *audio_out,
                         float        portamento_stretch);

  void process_portamento (size_t       n_values,
                           const float *freq_in,
                           float       *audio_out);
  void process_vibrato (size_t       n_values,
                        const float *freq_in,
                        float       *audio_out);
  void process_with_filter (size_t n_values,
                            const float *freq_in,
                            float *audio_out,
                            bool ramp);

public:
  LiveDecoder (float mix_freq);
  LiveDecoder (WavSet *smset, float mix_freq);
  LiveDecoder (LiveDecoderSource *source, float mix_freq);
  ~LiveDecoder();

  void enable_noise (bool ne);
  void enable_sines (bool se);
  void enable_debug_fft_perf (bool dfp);
  void enable_original_samples (bool eos);
  void enable_loop (bool eloop);
  void enable_start_skip (bool ess);
  void set_noise_seed (int seed);
  void set_unison_voices (int voices, float detune);
  void set_vibrato (bool enable_vibrato, float depth, float frequency, float attack);
  void set_filter (LiveDecoderFilter *filter);
  void set_source (LiveDecoderSource *source);

  static void precompute_tables (float mix_freq);
  void retrigger (int channel, float freq, int midi_velocity);
  void process (RTMemoryArea& rt_memory_area,
                size_t        n_values,
                const float  *freq_in,
                float        *audio_out);

  double current_pos() const;
  double fundamental_note() const;

  static size_t compute_loop_frame_index (size_t index, Audio *audio);
  bool done() const;

  double time_offset_ms() const;
};

}
#endif
