// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#ifndef SPECTMORPH_LEAK_DEBUGGER_HH
#define SPECTMORPH_LEAK_DEBUGGER_HH

#include <map>
#include <string>

#include <QMutex>

namespace SpectMorph
{

class LeakDebugger
{
  QMutex                   mutex;
  std::map<void *, int>    ptr_map;
  std::string              type;

  void ptr_add (void *p);
  void ptr_del (void *p);

public:
  LeakDebugger (const std::string& name);
  ~LeakDebugger();

  template<class T> void add (T *instance) { ptr_add (static_cast<void *> (instance)); }
  template<class T> void del (T *instance) { ptr_del (static_cast<void *> (instance)); }
};

}

#endif
