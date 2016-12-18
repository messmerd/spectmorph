// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "smmorphoutput.hh"
#include "smmorphplan.hh"
#include "smleakdebugger.hh"

#include <assert.h>

#define CHANNEL_OP_COUNT 4

using namespace SpectMorph;

using std::string;
using std::vector;

static LeakDebugger leak_debugger ("SpectMorph::MorphOutput");

MorphOutput::MorphOutput (MorphPlan *morph_plan) :
  MorphOperator (morph_plan),
  channel_ops (CHANNEL_OP_COUNT)
{
  connect (morph_plan, SIGNAL (operator_removed (MorphOperator *)), this, SLOT (on_operator_removed (MorphOperator *)));

  m_sines = true;
  m_noise = true;

  m_chorus = false;
  m_unison_voices = 2;
  m_unison_detune = 6.0;

  m_adsr = false;
  m_adsr_skip     = 500;
  m_adsr_attack   = 5;
  m_adsr_decay    = 20;
  m_adsr_sustain  = 70;
  m_adsr_release  = 70;

  leak_debugger.add (this);
}

MorphOutput::~MorphOutput()
{
  leak_debugger.del (this);
}

const char *
MorphOutput::type()
{
  return "SpectMorph::MorphOutput";
}

int
MorphOutput::insert_order()
{
  return 1000;
}

bool
MorphOutput::save (OutFile& out_file)
{
  for (size_t i = 0; i < channel_ops.size(); i++)
    {
      string name;

      if (channel_ops[i])   // NULL pointer => name = ""
        name = channel_ops[i]->name();

      out_file.write_string ("channel", name);
    }
  out_file.write_bool ("sines", m_sines);
  out_file.write_bool ("noise", m_noise);
  out_file.write_bool ("chorus", m_chorus);
  out_file.write_int ("unison_voices", m_unison_voices);
  out_file.write_float ("unison_detune", m_unison_detune);

  out_file.write_bool ("adsr", m_adsr);
  out_file.write_float ("adsr_skip",    m_adsr_skip);
  out_file.write_float ("adsr_attack",  m_adsr_attack);
  out_file.write_float ("adsr_decay",   m_adsr_decay);
  out_file.write_float ("adsr_sustain", m_adsr_sustain);
  out_file.write_float ("adsr_release", m_adsr_release);
  return true;
}

bool
MorphOutput::load (InFile& ifile)
{
  load_channel_op_names.clear();

  while (ifile.event() != InFile::END_OF_FILE)
    {
      if (ifile.event() == InFile::STRING)
        {
          if (ifile.event_name() == "channel")
            {
              load_channel_op_names.push_back (ifile.event_data());
            }
          else
            {
              g_printerr ("bad string\n");
              return false;
            }
        }
      else if (ifile.event() == InFile::BOOL)
        {
          if (ifile.event_name() == "sines")
            {
              m_sines = ifile.event_bool();
            }
          else if (ifile.event_name() == "noise")
            {
              m_noise = ifile.event_bool();
            }
          else if (ifile.event_name() == "chorus")
            {
              m_chorus = ifile.event_bool();
            }
          else if (ifile.event_name() == "adsr")
            {
              m_adsr = ifile.event_bool();
            }
          else
            {
              g_printerr ("bad bool\n");
              return false;
            }
        }
      else if (ifile.event() == InFile::INT)
        {
          if (ifile.event_name() == "unison_voices")
            {
              m_unison_voices = ifile.event_int();
            }
          else
            {
              g_printerr ("bad int\n");
              return false;
            }
        }
      else if (ifile.event() == InFile::FLOAT)
        {
          if (ifile.event_name() == "unison_detune")
            {
              m_unison_detune = ifile.event_float();
            }
          else if (ifile.event_name() == "adsr_skip")
            {
              m_adsr_skip = ifile.event_float();
            }
          else if (ifile.event_name() == "adsr_attack")
            {
              m_adsr_attack = ifile.event_float();
            }
          else if (ifile.event_name() == "adsr_decay")
            {
              m_adsr_decay = ifile.event_float();
            }
          else if (ifile.event_name() == "adsr_sustain")
            {
              m_adsr_sustain = ifile.event_float();
            }
          else if (ifile.event_name() == "adsr_release")
            {
              m_adsr_release = ifile.event_float();
            }
          else
            {
              g_printerr ("bad float\n");
              return false;
            }
        }
      else
        {
          g_printerr ("bad event\n");
          return false;
        }
      ifile.next_event();
    }
  return true;
}

void
MorphOutput::post_load (OpNameMap& op_name_map)
{
  for (size_t i = 0; i < channel_ops.size(); i++)
    {
      if (i < load_channel_op_names.size())
        {
          string name = load_channel_op_names[i];
          channel_ops[i] = op_name_map[name];
        }
    }
}

MorphOperator::OutputType
MorphOutput::output_type()
{
  return OUTPUT_NONE;
}

void
MorphOutput::set_channel_op (int ch, MorphOperator *op)
{
  assert (ch >= 0 && ch < CHANNEL_OP_COUNT);

  channel_ops[ch] = op;
  m_morph_plan->emit_plan_changed();
}

MorphOperator *
MorphOutput::channel_op (int ch)
{
  assert (ch >= 0 && ch < CHANNEL_OP_COUNT);

  return channel_ops[ch];
}

bool
MorphOutput::sines()
{
  return m_sines;
}

void
MorphOutput::set_sines (bool es)
{
  m_sines = es;

  m_morph_plan->emit_plan_changed();
}

bool
MorphOutput::noise()
{
  return m_noise;
}

void
MorphOutput::set_noise (bool en)
{
  m_noise = en;

  m_morph_plan->emit_plan_changed();
}

//---- unison effect ----

bool
MorphOutput::chorus()
{
  return m_chorus;
}

void
MorphOutput::set_chorus (bool ec)
{
  m_chorus = ec;

  m_morph_plan->emit_plan_changed();
}

int
MorphOutput::unison_voices() const
{
  return m_unison_voices;
}

void
MorphOutput::set_unison_voices (int voices)
{
  m_unison_voices = voices;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::unison_detune() const
{
  return m_unison_detune;
}

void
MorphOutput::set_unison_detune (float detune)
{
  m_unison_detune = detune;

  m_morph_plan->emit_plan_changed();
}

//---- adsr ----

bool
MorphOutput::adsr() const
{
  return m_adsr;
}

void
MorphOutput::set_adsr (bool ea)
{
  m_adsr = ea;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::adsr_skip() const
{
  return m_adsr_skip;
}

void
MorphOutput::set_adsr_skip (float skip)
{
  m_adsr_skip = skip;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::adsr_attack() const
{
  return m_adsr_attack;
}

void
MorphOutput::set_adsr_attack (float attack)
{
  m_adsr_attack = attack;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::adsr_decay() const
{
  return m_adsr_decay;
}

void
MorphOutput::set_adsr_decay (float decay)
{
  m_adsr_decay = decay;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::adsr_sustain() const
{
  return m_adsr_sustain;
}

void
MorphOutput::set_adsr_sustain (float sustain)
{
  m_adsr_sustain = sustain;

  m_morph_plan->emit_plan_changed();
}

float
MorphOutput::adsr_release() const
{
  return m_adsr_release;
}

void
MorphOutput::set_adsr_release (float release)
{
  m_adsr_release = release;

  m_morph_plan->emit_plan_changed();
}


void
MorphOutput::on_operator_removed (MorphOperator *op)
{
  for (size_t ch = 0; ch < channel_ops.size(); ch++)
    {
      if (channel_ops[ch] == op)
        channel_ops[ch] = NULL;
    }
}
