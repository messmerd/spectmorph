// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#ifndef SPECTMORPH_PROJECT_HH
#define SPECTMORPH_PROJECT_HH

#include "sminstrument.hh"
#include "smwavset.hh"
#include "smwavsetbuilder.hh"
#include "smbuilderthread.hh"
#include "smmorphplan.hh"
#include "smuserinstrumentindex.hh"
#include "smnotifybuffer.hh"

#include <thread>
#include <mutex>

namespace SpectMorph
{

class MidiSynth;
class SynthInterface;
class MorphWavSource;

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
  void destroy_all_events();
};

class Project : public SignalReceiver
{
public:
  enum class StorageModel {
    COPY,
    REFERENCE
  };

  static constexpr size_t WAV_SETS_RESERVE = 256;
private:
  std::vector<std::unique_ptr<WavSet>> wav_sets;

  std::unique_ptr<MidiSynth>  m_midi_synth;
  double                      m_mix_freq = 0;
  double                      m_volume = -6;
  MorphPlan                   m_morph_plan;
  std::vector<unsigned char>  m_last_plan_data;
  bool                        m_state_changed_notify = false;
  StorageModel                m_storage_model = StorageModel::COPY;

  std::mutex                  m_synth_mutex;
  ControlEventVector          m_control_events;          // protected by synth mutex
  NotifyBuffer                m_notify_buffer;           // protected by synth mutex
  bool                        m_state_changed = false;   // protected by synth mutex

  std::unique_ptr<SynthInterface> m_synth_interface;

  UserInstrumentIndex         m_user_instrument_index;
  BuilderThread               m_builder_thread;

  struct InstrumentMapEntry {
    std::unique_ptr<Instrument> instrument;
    std::string                 lv2_absolute_path;
  };
  typedef std::map<int, InstrumentMapEntry> InstrumentMap;
  InstrumentMap               m_instrument_map;

  std::vector<MorphWavSource *> list_wav_sources();

  Error load_internal (ZipReader& zip_reader, MorphPlan::ExtraParameters *params, bool load_wav_sources);
  void  post_load();

  void on_plan_changed();
  void on_operator_added (MorphOperator *op);
  void on_operator_removed (MorphOperator *op);

public:
  Project();

  InstrumentMapEntry& lookup_instrument (MorphWavSource *wav_source);
  void set_lv2_absolute_path (MorphWavSource *wav_source, const std::string& path);

  void rebuild (MorphWavSource *wav_source);
  void add_rebuild_result (int object_id, std::unique_ptr<WavSet>& wav_set);
  void clear_wav_sets (std::vector<std::unique_ptr<WavSet>>& wav_sets);
  bool rebuild_active (int object_id);

  WavSet *get_wav_set (int object_id);

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
  bool try_update_synth();
  void set_mix_freq (double mix_freq);
  void set_storage_model (StorageModel model);
  void set_state_changed_notify (bool notify);
  void state_changed();

  void set_volume (double new_volume);
  double volume() const;

  NotifyBuffer *notify_buffer();
  SynthInterface *synth_interface() const;
  MidiSynth *midi_synth() const;
  MorphPlan *morph_plan();
  UserInstrumentIndex *user_instrument_index();

  Error save (const std::string& filename);
  Error save (ZipWriter& zip_writer, MorphPlan::ExtraParameters *params);
  Error load (const std::string& filename, bool load_wav_sources = true);
  Error load (ZipReader& zip_reader, MorphPlan::ExtraParameters *params, bool load_wav_sources = true);
  Error load_compat (GenericInP in, MorphPlan::ExtraParameters *params);

  std::string save_plan_lv2 (std::function<std::string(std::string)> abstract_path);
  void        load_plan_lv2 (std::function<std::string(std::string)> absolute_path, const std::string& plan);
  void        clear_lv2_filenames();

  Signal<double> signal_volume_changed;
};

}

#endif
