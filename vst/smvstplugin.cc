/*
 *  VST Plugin is under GPL 2 or later, since it is based on Vestige (GPL) and amsynth_vst (GPL)
 *
 *  Copyright (c) 2008-2015 Nick Dowell
 *  Copyright (c) 2016 Stefan Westerfeld
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "vestige/aeffectx.h"
#include "smutils.hh"
#include "smmorphplan.hh"
#include "smmorphplansynth.hh"
#include "smmidisynth.hh"
#include "smmain.hh"
#include "smvstui.hh"
#include "smvstplugin.hh"
#include "smmorphoutputmodule.hh"
#include "smzip.hh"
#include "smhexstring.hh"

#ifdef SM_OS_MACOS // need to include this before using namespace SpectMorph
#include <CoreFoundation/CoreFoundation.h>
#endif

// from http://www.asseca.org/vst-24-specs/index.html
#define effGetParamLabel        6
#define effGetParamDisplay      7
#define effGetChunk             23
#define effSetChunk             24
#define effCanBeAutomated       26
#define effGetOutputProperties  34
#define effVendorSpecific       50
#define effGetTailSize          52
#define effGetMidiKeyName       66
#define effBeginLoadBank        75
#define effFlagsProgramChunks   (1 << 5)

using namespace SpectMorph;

using std::string;
using std::vector;
using std::map;

VstPlugin::VstPlugin (audioMasterCallback master, AEffect *aeffect) :
  audioMaster (master),
  aeffect (aeffect)
{
  audioMaster = master;

  ui = new VstUI (project.morph_plan(), this);

  parameters.push_back (Parameter ("Control #1", 0, -1, 1));
  parameters.push_back (Parameter ("Control #2", 0, -1, 1));
  parameters.push_back (Parameter ("Control #3", 0, -1, 1));
  parameters.push_back (Parameter ("Control #4", 0, -1, 1));

  // initialize mix_freq with something, so that the plugin doesn't crash if the host never calls SetSampleRate
  set_mix_freq (48000);
}

VstPlugin::~VstPlugin()
{
  delete ui;
  ui = nullptr;
}

void
VstPlugin::get_parameter_name (Param param, char *out, size_t len) const
{
  if (param >= 0 && param < parameters.size())
    strncpy (out, parameters[param].name.c_str(), len);
}

void
VstPlugin::get_parameter_label (Param param, char *out, size_t len) const
{
  if (param >= 0 && param < parameters.size())
    strncpy (out, parameters[param].label.c_str(), len);
}

void
VstPlugin::get_parameter_display (Param param, char *out, size_t len) const
{
  if (param >= 0 && param < parameters.size())
    strncpy (out, string_printf ("%.5f", parameters[param].value).c_str(), len);
}

void
VstPlugin::set_parameter_scale (Param param, float value)
{
  if (param >= 0 && param < parameters.size())
    parameters[param].value = parameters[param].min_value + (parameters[param].max_value - parameters[param].min_value) * value;
}

float
VstPlugin::get_parameter_scale (Param param) const
{
  if (param >= 0 && param < parameters.size())
    return (parameters[param].value - parameters[param].min_value) / (parameters[param].max_value - parameters[param].min_value);

  return 0;
}

float
VstPlugin::get_parameter_value (Param param) const
{
  if (param >= 0 && param < parameters.size())
    return parameters[param].value;

  return 0;
}

void
VstPlugin::set_parameter_value (Param param, float value)
{
  if (param >= 0 && param < parameters.size())
    parameters[param].value = value;
}

void
VstPlugin::set_mix_freq (double mix_freq)
{
  /* this should only be called by the host if the plugin is suspended, so
   * we can alter variables that are used by process|processReplacing in the real time thread
   */
  project.set_mix_freq (mix_freq);
}

/*----------------------- save/load ----------------------------*/
class VstExtraParameters : public MorphPlan::ExtraParameters
{
  VstPlugin         *plugin;
  map<string, float> load_value;
public:
  VstExtraParameters (VstPlugin *plugin) :
    plugin (plugin)
  {
  }

  string section() { return "vst_parameters"; }

  void
  save (OutFile& out_file)
  {
    out_file.write_float ("control_1", plugin->get_parameter_value (VstPlugin::PARAM_CONTROL_1));
    out_file.write_float ("control_2", plugin->get_parameter_value (VstPlugin::PARAM_CONTROL_2));
    out_file.write_float ("control_3", plugin->get_parameter_value (VstPlugin::PARAM_CONTROL_3));
    out_file.write_float ("control_4", plugin->get_parameter_value (VstPlugin::PARAM_CONTROL_4));
    out_file.write_float ("volume",    plugin->project.volume());
  }

  void
  handle_event (InFile& in_file)
  {
    if (in_file.event() == InFile::FLOAT)
      {
        load_value[in_file.event_name()] = in_file.event_float();
      }
  }

  float
  get_load_value (const string& name, float def_value) const
  {
    auto vi = load_value.find (name);
    if (vi == load_value.end())
      return def_value;
    else
      return vi->second;
  }
};

int
VstPlugin::save_state (char **buffer)
{
  VstExtraParameters params (this);

  ZipWriter zip_writer;
  project.save (zip_writer, &params);
  chunk_data = zip_writer.data();

  *buffer = reinterpret_cast<char *> (&chunk_data[0]);
  return chunk_data.size();
}

void
VstPlugin::load_state (char *buffer, size_t size)
{
  Error error = Error::Code::NONE;
  VstExtraParameters params (this);

  if (size > 2 && buffer[0] == 'P' && buffer[1] == 'K') // new format
    {
      vector<unsigned char> data (buffer, buffer + size);

      ZipReader zip_reader (data);

      error = project.load (zip_reader, &params);
    }
  else
    {
      vector<unsigned char> data;
      if (!HexString::decode (buffer, data))
        return;

      GenericIn *in = MMapIn::open_vector (data);
      error = project.load_compat (in, &params);
      delete in;
    }

  if (!error)  /* if loading failed, keep values as they are */
    {
      set_parameter_value (VstPlugin::PARAM_CONTROL_1, params.get_load_value ("control_1", 0));
      set_parameter_value (VstPlugin::PARAM_CONTROL_2, params.get_load_value ("control_2", 0));
      set_parameter_value (VstPlugin::PARAM_CONTROL_3, params.get_load_value ("control_3", 0));
      set_parameter_value (VstPlugin::PARAM_CONTROL_4, params.get_load_value ("control_4", 0));
      project.set_volume (params.get_load_value ("volume", -6));
    }
}

/*----------------------- dispatcher ----------------------------*/

static char hostProductString[64] = "";

static intptr_t dispatcher(AEffect *effect, int opcode, int index, intptr_t val, void *ptr, float f)
{
  VstPlugin *plugin = (VstPlugin *)effect->ptr3;

  switch (opcode) {
    case effOpen:
      return 0;

    case effClose:
      delete plugin;
      memset(effect, 0, sizeof(AEffect));
      free(effect);
      sm_plugin_cleanup();
      return 0;

    case effSetProgram:
    case effGetProgram:
    case effGetProgramName:
      return 0;

    case effGetParamLabel:
      plugin->get_parameter_label ((VstPlugin::Param)index, (char *)ptr, 32);
      return 0;

    case effGetParamDisplay:
      plugin->get_parameter_display ((VstPlugin::Param)index, (char *)ptr, 32);
      return 0;

    case effGetParamName:
      plugin->get_parameter_name ((VstPlugin::Param)index, (char *)ptr, 32);
      return 0;

    case effCanBeAutomated:
      // for now, all parameters can be automated, so we hardcode this
      if (index >= 0 && index < VstPlugin::PARAM_COUNT)
        return 1;
      return 0;

    case effSetSampleRate:
      plugin->set_mix_freq (f);
      return 0;

    case effSetBlockSize:
    case effMainsChanged:
      return 0;

    case effEditGetRect:
      plugin->ui->getRect ((ERect **) ptr);
      return 1;

    case effEditOpen:
      plugin->ui->open((PuglNativeWindow)(uintptr_t)ptr);
      return 1;

    case effEditClose:
      plugin->ui->close();
      return 0;

    case effEditIdle:
      plugin->ui->idle();
      return 0;

    case effGetChunk:
      {
        int result = plugin->save_state((char **)ptr);
        VST_DEBUG ("get chunk returned: %d bytes\n", result);
        return result;
      }

    case effSetChunk:
      VST_DEBUG ("set chunk: size %d\n", int (val));
      plugin->load_state((char *)ptr, val);
      return 0;

    case effProcessEvents:
      {
        VstEvents *events = (VstEvents *)ptr;

        for (int32_t i = 0; i < events->numEvents; i++)
          {
            VstMidiEvent *event = (VstMidiEvent *)events->events[i];
            if (event->type != kVstMidiType)
              continue;

            plugin->project.midi_synth()->add_midi_event (event->deltaFrames, reinterpret_cast <unsigned char *> (&event->midiData[0]));
          }
        return 1;
      }

    case effGetOutputProperties:
      return 0;

    case effGetPlugCategory:
      return kPlugCategSynth;
    case effGetEffectName:
      strcpy((char *)ptr, "SpectMorph");
      return 1;
    case effGetVendorString:
      strcpy((char *)ptr, "Stefan Westerfeld");
      return 1;
    case effGetProductString:
      strcpy((char *)ptr, "SpectMorph");
      return 1;
    case effGetVendorVersion:
       return 0;
    case effCanDo:
      if (strcmp("receiveVstMidiEvent", (char *)ptr) == 0 ||
          strcmp("MPE", (char *)ptr) == 0 ||
          strcmp ("supportsViewDpiScaling", (char *)ptr) == 0) return 1;
      if (strcmp("midiKeyBasedInstrumentControl", (char *)ptr) == 0 ||
              strcmp("receiveVstSysexEvent", (char *)ptr) == 0 ||
              strcmp("midiSingleNoteTuningChange", (char *)ptr) == 0 ||
              strcmp("sendVstMidiEvent", (char *)ptr) == 0 ||
              false) return 0;
      VST_DEBUG ("unhandled canDo: %s\n", (char *)ptr);
      return 0;

    case effGetTailSize:
    case effIdle:
    case effGetParameterProperties:
      return 0;

    case effGetVstVersion:
      return 2400;

    case effGetMidiKeyName:
    case effStartProcess:
    case effStopProcess:
    case effBeginSetProgram:
    case effEndSetProgram:
    case effBeginLoadBank:
      return 0;

    default:
      VST_DEBUG ("[smvstplugin] unhandled VST opcode: %d\n", opcode);
      return 0;
  }
}

static void
processReplacing (AEffect *effect, float **inputs, float **outputs, int numSampleFrames)
{
  VstPlugin *plugin     = (VstPlugin *)effect->ptr3;
  MidiSynth *midi_synth = plugin->project.midi_synth();

  // update plan with new parameters / new modules if necessary
  plugin->project.try_update_synth();

  VstTimeInfo *vst_time_info = (VstTimeInfo *) plugin->audioMaster (effect, audioMasterGetTime, 0, kVstPpqPosValid | kVstTempoValid | kVstTransportPlaying, 0, 0);
  if (vst_time_info && vst_time_info->flags & kVstTempoValid)
    midi_synth->set_tempo (vst_time_info->tempo);
  else
    midi_synth->set_tempo (120);
  if (vst_time_info && vst_time_info->flags & kVstTransportPlaying)
    {
      if (vst_time_info && vst_time_info->flags & kVstPpqPosValid)
        midi_synth->set_ppq_pos (vst_time_info->ppqPos);
    }
  midi_synth->set_control_input (0, plugin->parameters[VstPlugin::PARAM_CONTROL_1].value);
  midi_synth->set_control_input (1, plugin->parameters[VstPlugin::PARAM_CONTROL_2].value);
  midi_synth->set_control_input (2, plugin->parameters[VstPlugin::PARAM_CONTROL_3].value);
  midi_synth->set_control_input (3, plugin->parameters[VstPlugin::PARAM_CONTROL_4].value);
  midi_synth->process (outputs[0], numSampleFrames);

  std::copy (outputs[0], outputs[0] + numSampleFrames, outputs[1]);
}

static void
process (AEffect *effect, float **inputs, float **outputs, int numSampleFrames)
{
  // this is not as efficient as it could be
  // however, most likely the actual morphing is a lot more expensive

  float  tmp_output_l[numSampleFrames];
  float  tmp_output_r[numSampleFrames];
  float *tmp_outputs[2] = { tmp_output_l, tmp_output_r };

  processReplacing (effect, inputs, tmp_outputs, numSampleFrames);

  for (int i = 0; i < numSampleFrames; i++)
    {
      outputs[0][i] += tmp_output_l[i];
      outputs[1][i] += tmp_output_r[i];
    }
}

static void setParameter(AEffect *effect, int i, float f)
{
  VstPlugin *plugin = (VstPlugin *)effect->ptr3;

  plugin->set_parameter_scale ((VstPlugin::Param) i, f);
}

static float getParameter(AEffect *effect, int i)
{
  VstPlugin *plugin = (VstPlugin *)effect->ptr3;

  return plugin->get_parameter_scale((VstPlugin::Param) i);
}

#ifdef SM_OS_WINDOWS
#include "windows.h"

HMODULE hInstance;

extern "C" {
BOOL WINAPI DllMain (HINSTANCE hInst, DWORD dwReason, LPVOID lpvReserved)
{
  hInstance = hInst;
  return 1;
}
} // extern "C"

extern "C" AEffect *VSTPluginMain (audioMasterCallback audioMaster) __declspec(dllexport);
#else
extern "C" AEffect *VSTPluginMain (audioMasterCallback audioMaster) __attribute__((visibility("default")));
#endif

extern "C" AEffect *VSTPluginMain (audioMasterCallback audioMaster)
{
  Debug::set_filename ("smvstplugin.log");

  sm_plugin_init();

  VST_DEBUG ("VSTPluginMain called\n"); // debug statements are only visible after init

  if (audioMaster)
    {
      audioMaster (NULL, audioMasterGetProductString, 0, 0, hostProductString, 0.0f);
      VST_DEBUG ("Host: %s\n", hostProductString);
    }

  SM_SET_OS_DATA_DIR();

  AEffect *effect = (AEffect *)calloc(1, sizeof(AEffect));
  effect->magic = kEffectMagic;
  effect->dispatcher = dispatcher;
  effect->process = process;
  effect->setParameter = setParameter;
  effect->getParameter = getParameter;
  effect->numPrograms = 0;
  effect->numParams = VstPlugin::PARAM_COUNT;
  effect->numInputs = 0;
  effect->numOutputs = 2;
  effect->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks | effFlagsHasEditor;

  // Do no use the ->user pointer because ardour clobbers it
  effect->ptr3 = new VstPlugin (audioMaster, effect);
  effect->uniqueID = CCONST ('s', 'm', 'r', 'p');
  effect->processReplacing = processReplacing;

  VST_DEBUG ("VSTPluginMain done => return %p\n", effect);
  return effect;
}

#if 0
// this is required because GCC throws an error if we declare a non-standard function named 'main'
extern "C" __attribute__ ((visibility("default"))) AEffect * main_plugin(audioMasterCallback audioMaster) asm ("main");

extern "C" __attribute__ ((visibility("default"))) AEffect * main_plugin(audioMasterCallback audioMaster)
{
	return VSTPluginMain (audioMaster);
}
#endif
