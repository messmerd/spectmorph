// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#ifndef SPECTMORPH_PROJECT_HH
#define SPECTMORPH_PROJECT_HH

#include "sminstrument.hh"
#include "smwavset.hh"
#include "smwavsetbuilder.hh"
#include "smobject.hh"
#include "smbuilderthread.hh"

#include <thread>
#include <mutex>

namespace SpectMorph
{

class Project;
class MidiSynth;
class SynthInterface;
class MorphPlan;

class SynthControlEvent
{
public:
  virtual void run_rt (Project *project) = 0;
  virtual
  ~SynthControlEvent()
  {
  }
};

struct InstFunc : public SynthControlEvent
{
  std::function<void(Project *)> func;
  std::function<void()>          free_func;
public:
  InstFunc (const std::function<void(Project *)>& func,
            const std::function<void()>& free_func) :
    func (func),
    free_func (free_func)
  {
  }
  ~InstFunc()
  {
    free_func();
  }
  void
  run_rt (Project *project)
  {
    func (project);
  }
};

class ControlEventVector
{
  std::vector<std::unique_ptr<SynthControlEvent>> events;
  bool clear = false;
public:
  void take (SynthControlEvent *ev);
  void run_rt (Project *project);
};

class Job;

class Project : public SignalReceiver
{
  std::vector<std::shared_ptr<WavSet>> wav_sets;

  std::unique_ptr<MidiSynth>  m_midi_synth;
  double                      m_mix_freq = 0;
  double                      m_volume = -6;
  RefPtr<MorphPlan>           m_morph_plan;

  std::mutex                  m_synth_mutex;
  ControlEventVector          m_control_events;          // protected by synth mutex
  std::vector<std::string>    m_out_events;              // protected by synth mutex
  bool                        m_voices_active = false;   // protected by synth mutex

  std::unique_ptr<SynthInterface> m_synth_interface;

  BuilderThread               m_builder_thread;

  std::map<int, std::unique_ptr<Instrument>> instrument_map;

  void on_plan_changed();

public:
  Project();

  void rebuild (int inst_id);
  void add_rebuild_result (int inst_id, WavSet *wav_set);

  int add_instrument();
  Instrument *get_instrument (int inst_id);

  std::shared_ptr<WavSet> get_wav_set (int inst_id);

  void synth_take_control_event (SynthControlEvent *event);

  std::mutex&
  synth_mutex()
  {
    /* the synthesis thread will typically not block on synth_mutex
     * instead, it tries locking it, and if that fails, continues
     *
     * the ui thread will block on the synth_mutex to enqueue events,
     * parameter changes (in form of a new morph plan, volume, ...)
     * and so on
     *
     * if the synthesis thread can obtain a lock, it will then be
     * able to process these events to update its internal state
     * and also send notifications back to the ui
     */
    return m_synth_mutex;
  }
  void try_update_synth();
  void set_mix_freq (double mix_freq);
  bool voices_active();

  void set_volume (double new_volume);
  double volume() const;

  std::vector<std::string> notify_take_events();
  SynthInterface *synth_interface() const;
  MidiSynth *midi_synth() const;
  RefPtr<MorphPlan> morph_plan() const;

  Signal<double> signal_volume_changed;
};

}

#endif
