// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#ifndef SPECTMORPH_MIDI_SYNTH_HH
#define SPECTMORPH_MIDI_SYNTH_HH

#include "smmorphplansynth.hh"

namespace SpectMorph {

class MidiSynth
{
  class Voice
  {
  public:
    enum State {
      STATE_IDLE,
      STATE_ON,
      STATE_RELEASE
    };
    MorphPlanVoice *mp_voice;

    State        state;
    bool         pedal;
    int          midi_note;
    int          channel;
    double       env;
    double       velocity;
    double       freq;
    double       pitch_bend_cent;

    Voice() :
      mp_voice (NULL),
      state (STATE_IDLE),
      pedal (false)
    {
    }
    ~Voice()
    {
      mp_voice = NULL;
    }
  };

  MorphPlanSynth        morph_plan_synth;

  std::vector<Voice>    voices;
  std::vector<Voice *>  idle_voices;
  std::vector<Voice *>  active_voices;
  double                mix_freq;
  bool                  pedal_down;

  float                 control[2];

  Voice  *alloc_voice();
  void    free_unused_voices();
  float   freq_from_note (float note);

  void process_audio (float *output, size_t n_values);
  void process_note_on (int channel, int midi_note, int midi_velocity);
  void process_note_off (int midi_note);
  void process_midi_controller (int controller, int value);
  void process_pitch_bend (int channel, double value);

  struct MidiEvent
  {
    unsigned int  offset;
    char          midi_data[3];

    bool is_note_on() const;
    bool is_note_off() const;
    bool is_controller() const;
    bool is_pitch_bend() const;
    int  channel() const;
  };
  std::vector<MidiEvent>  midi_events;

public:
  MidiSynth (double mix_freq, size_t n_voices);

  void add_midi_event (size_t offset, const unsigned char *midi_data);
  void process (float *output, size_t n_values);

  void set_control_input (int i, float value);
  void update_plan (MorphPlanPtr new_plan);

  size_t active_voice_count() const;
};

}

#endif /* SPECTMORPH_MIDI_SYNTH_HH */
