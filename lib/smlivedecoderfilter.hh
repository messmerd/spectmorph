// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#pragma once

#include "smutils.hh"
#include "smladdervcf.hh"
#include "smskfilter.hh"
#include "smmorphoutput.hh"
#include "smlinearsmooth.hh"
#include "smflexadsr.hh"
#include "smdcblocker.hh"

namespace SpectMorph
{

class MorphOutputModule;

class LiveDecoderFilter
{
  struct SmoothValue {
    float value;
    float delta;
    bool  constant;
  };
  SmoothValue               log_cutoff_smooth;
  SmoothValue               resonance_smooth;
  SmoothValue               drive_smooth;
  bool                      smooth_first = false;
  float                     key_tracking = 0;
  FlexADSR                  envelope;
  float                     depth_octaves = 0;
  float                     mix_freq = 0;

  MorphOutput::FilterType   filter_type;
  MorphOutputModule        *output_module = nullptr;

  static constexpr int FILTER_OVERSAMPLE = 4;

  LadderVCF                 ladder_filter { FILTER_OVERSAMPLE };
  SKFilter                  sk_filter { FILTER_OVERSAMPLE };
  DCBlocker                 dc_blocker;

public:
  LiveDecoderFilter();

  void retrigger();
  void release();
  void process (size_t n_values, float *audio, float current_note);

  void set_config (MorphOutputModule *output_module, const MorphOutput::Config *cfg, float mix_freq);

  int idelay();
};

}
