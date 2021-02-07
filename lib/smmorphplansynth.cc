// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphplansynth.hh"
#include "smmorphplanvoice.hh"
#include "smleakdebugger.hh"

using namespace SpectMorph;

using std::map;
using std::vector;
using std::string;

static LeakDebugger leak_debugger ("SpectMorph::MorphPlanSynth");

MorphPlanSynth::MorphPlanSynth (float mix_freq) :
  m_mix_freq (mix_freq)
{
  leak_debugger.add (this);
}

MorphPlanSynth::~MorphPlanSynth()
{
  leak_debugger.del (this);

  free_shared_state();

  for (size_t i = 0; i < voices.size(); i++)
    delete voices[i];

  voices.clear();
}

MorphPlanVoice *
MorphPlanSynth::add_voice()
{
  MorphPlanVoice *voice = new MorphPlanVoice (m_mix_freq, this);

#if 0 /* FIXME: CONFIG */
  if (plan)
    voice->full_update (plan);
#endif

  voices.push_back (voice);
  return voice;
}

static vector<string>
sorted_id_list (MorphPlanPtr plan)
{
  vector<string> ids;

  if (plan)
    {
      const vector<MorphOperator *>& ops = plan->operators();
      for (vector<MorphOperator *>::const_iterator oi = ops.begin(); oi != ops.end(); oi++)
        {
          ids.push_back ((*oi)->id());
        }
      sort (ids.begin(), ids.end());
    }
  return ids;
}

void
MorphPlanSynth::update_plan (MorphPlanPtr new_plan)
{
  /* FIXME: CONFIG */
#if 0
  vector<string> old_ids = sorted_id_list (plan);
  vector<string> new_ids = sorted_id_list (new_plan);

  if (old_ids == new_ids)
    {
      map<string, MorphOperator *> op_map;

      const vector<MorphOperator *>& ops = new_plan->operators();
      for (vector<MorphOperator *>::const_iterator oi = ops.begin(); oi != ops.end(); oi++)
        op_map[(*oi)->id()] = *oi;

      for (size_t i = 0; i < voices.size(); i++)
        voices[i]->cheap_update (op_map);
    }
  else
    {
      free_shared_state();

      for (size_t i = 0; i < voices.size(); i++)
        voices[i]->full_update (new_plan);
    }
  plan = new_plan;
#endif
  printf ("MorphPlanSynth::update_plan NOP\n");
}

MorphPlanSynth::UpdateP
MorphPlanSynth::prepare_update (MorphPlanPtr plan) /* main thread */
{
  UpdateP update = std::make_shared<Update>();

  for (auto o : plan->operators())
    {
      MorphOperatorConfig *config = o->clone_config();
      Update::Op op = {
        .id = o->id(),
        .type = o->type(),
        .config = config /* FIXME: CONFIG: need to delete this eventually */
      };
      update->ops.push_back (op);
    }
  sort (update->ops.begin(), update->ops.end(),
        [](const Update::Op& a, const Update::Op& b) { return a.id < b.id; });

  vector<string> update_ids = sorted_id_list (plan);

  update->cheap = update_ids == m_last_update_ids;
  m_last_update_ids = update_ids;

  return update;
}

void
MorphPlanSynth::apply_update (MorphPlanSynth::UpdateP update) /* audio thread */
{
  printf ("--- %s update ---\n", update->cheap ? "cheap" : "full");
  for (auto& op : update->ops)
    {
      printf ("I%s -> T%s : P%p\n", op.id.c_str(), op.type.c_str(), op.config);
    }

  if (update->cheap)
    {
      for (size_t i = 0; i < voices.size(); i++)
        voices[i]->cheap_update (update);
    }
  else
    {
      free_shared_state();

      for (size_t i = 0; i < voices.size(); i++)
        voices[i]->full_update (update);
    }
}

void
MorphPlanSynth::update_shared_state (const TimeInfo& time_info)
{
  if (voices.empty())
    return;
  voices[0]->update_shared_state (time_info);
}

MorphModuleSharedState *
MorphPlanSynth::shared_state (MorphOperator *op)
{
  return m_shared_state[op->id()];
}

void
MorphPlanSynth::set_shared_state (MorphOperator *op, MorphModuleSharedState *shared_state)
{
  m_shared_state[op->id()] = shared_state;
}

float
MorphPlanSynth::mix_freq() const
{
  return m_mix_freq;
}

Random *
MorphPlanSynth::random_gen()
{
  return &m_random_gen;
}

bool
MorphPlanSynth::have_output() const
{
  if (voices.empty())
    return false;

  // all voices are the same: either each of them contains an output module, or none of them
  return voices[0]->output() != nullptr;
}

void
MorphPlanSynth::free_shared_state()
{
  map<string, MorphModuleSharedState *>::iterator si;
  for (si = m_shared_state.begin(); si != m_shared_state.end(); si++)
    delete si->second;
  m_shared_state.clear();
}
