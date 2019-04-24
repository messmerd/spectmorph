// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smproject.hh"
#include "smmidisynth.hh"
#include "smsynthinterface.hh"
#include "smmorphoutputmodule.hh"

using namespace SpectMorph;

using std::string;
using std::vector;

void
ControlEventVector::take (SynthControlEvent *ev)
{
  // we'd rather run destructors in non-rt part of the code
  if (clear)
    {
      events.clear();
      clear = false;
    }

  events.emplace_back (ev);
}

void
ControlEventVector::run_rt (Project *project)
{
  if (!clear)
    {
      for (const auto& ev : events)
        ev->run_rt (project);

      clear = true;
    }
}

void
Project::try_update_synth()
{
  // handle synth updates (if locking is possible without blocking)
  //  - apply new parameters
  //  - process events
  if (m_synth_mutex.try_lock())
    {
      m_control_events.run_rt (this);
      m_out_events = m_midi_synth->inst_edit_synth()->take_out_events();
      m_voices_active = m_midi_synth->active_voice_count() > 0;

      m_synth_mutex.unlock();
    }
}

void
Project::synth_take_control_event (SynthControlEvent *event)
{
  std::lock_guard<std::mutex> lg (m_synth_mutex);
  m_control_events.take (event);
}

void
Project::rebuild()
{
  WavSetBuilder *builder = new WavSetBuilder (&instrument, /* keep_samples */ false);

  new std::thread ([this, builder]() {
    struct Event : public SynthControlEvent {
      std::shared_ptr<WavSet> wav_set;

      void
      run_rt (Project *project) // FIXME: EventData!
      {
        project->wav_set = wav_set;
      }
    } *event = new Event();

    event->wav_set = std::shared_ptr<WavSet> (builder->run());
    delete builder;

    synth_take_control_event (event);
  });
}

vector<string>
Project::notify_take_events()
{
  std::lock_guard<std::mutex> lg (m_synth_mutex);
  return std::move (m_out_events);
}

SynthInterface *
Project::synth_interface() const
{
  return m_synth_interface.get();
}

MidiSynth *
Project::midi_synth() const
{
  return m_midi_synth;
}

Project::Project()
{
  m_synth_interface.reset (new SynthInterface (this));
}

void
Project::update_plan (MorphPlanPtr plan)
{
  // this might take a while, and cannot be done in synthesis thread
  MorphPlanSynth mp_synth (m_midi_synth->mix_freq());
  MorphPlanVoice *mp_voice = mp_synth.add_voice();
  mp_synth.update_plan (plan);

  MorphOutputModule *om = mp_voice->output();
  if (om)
    {
      om->retrigger (0, 440, 1);
      float s;
      float *values[1] = { &s };
      om->process (1, values, 1);
    }

  // FIXME: refptr is locking (which is not too good)
  m_synth_interface->emit_update_plan (plan);
}

bool
Project::voices_active()
{
  std::lock_guard<std::mutex> lg (m_synth_mutex);
  return m_voices_active;
}
