// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html

#include "pugl/gl.h"
#if !__APPLE__
#include "GL/glext.h"
#endif
#include "smwindow.hh"
#include "smscrollview.hh"
#include "smnativefiledialog.hh"
#include "smconfig.hh"
#include "pugl/cairo_gl.h"
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <sys/time.h>

using namespace SpectMorph;

using std::vector;
using std::string;
using std::min;
using std::max;

struct SpectMorph::CairoGL
{
private:
  PuglCairoGL pugl_cairo_gl;
  cairo_surface_t *surface;
  int  m_width;
  int  m_height;
  bool m_first_frame = true;

  vector<uint32> tmp_buffer;

public:
  cairo_t *cr;

  CairoGL (int width, int height) :
    m_width (width), m_height (height)
  {
    memset (&pugl_cairo_gl, 0, sizeof (pugl_cairo_gl));

    surface = pugl_cairo_gl_create (&pugl_cairo_gl, width, height, 4);
    cr = cairo_create (surface);
  }
  ~CairoGL()
  {
    cairo_destroy (cr);
    cairo_surface_destroy (surface);

    pugl_cairo_gl_free (&pugl_cairo_gl);
  }
  void
  configure()
  {
    pugl_cairo_gl_configure (&pugl_cairo_gl, m_width, m_height);

    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                  m_width, m_height, 0,
                  GL_BGRA, GL_UNSIGNED_BYTE, pugl_cairo_gl.buffer);
  }
  void
  draw (int x, int y, int w, int h)
  {
    (void) pugl_cairo_gl_draw; // reimplement this:

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);

    glPushMatrix();
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_TEXTURE_2D);

    void *draw_buffer;
    if (x == 0 && y == 0 && w == m_width && h == m_height)
      {
        // draw full frame
        draw_buffer = pugl_cairo_gl.buffer;
      }
    else
      {
        uint32 *src_buffer = reinterpret_cast<uint32 *> (pugl_cairo_gl.buffer);
        tmp_buffer.resize (w * h);

        for (int by = 0; by < h; by++)
          {
            memcpy (&tmp_buffer[by * w], &src_buffer[(by + y) * m_width + x], w * 4);
          }
        draw_buffer = tmp_buffer.data();
      }

    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0,
                     x, y, w, h,
                     GL_BGRA, GL_UNSIGNED_BYTE, draw_buffer);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, (GLfloat)m_height);
    glVertex2f(-1.0f, -1.0f);

    glTexCoord2f((GLfloat)m_width, (GLfloat)m_height);
    glVertex2f(1.0f, -1.0f);

    glTexCoord2f((GLfloat)m_width, 0.0f);
    glVertex2f(1.0f, 1.0f);

    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glPopMatrix();

    m_first_frame = false;
  }
  int
  width()
  {
    return m_width;
  }
  int
  height()
  {
    return m_height;
  }
  bool
  first_frame()
  {
    return m_first_frame;
  }
};

static void
on_event (PuglView* view, const PuglEvent* event)
{
  Window *window = reinterpret_cast<Window *> (puglGetHandle (view));

  window->on_event (event);
}

static void
on_resize (PuglView *view, int *width, int *height, int *set_hints)
{
  Window *window = reinterpret_cast<Window *> (puglGetHandle (view));

  window->on_resize (width, height);
}

Window::Window (const string& title, int width, int height, PuglNativeWindow win_id, bool resize) :
  Widget (nullptr, 0, 0, width, height),
  draw_grid (false)
{
  Config cfg;

  global_scale = cfg.zoom() / 100.0;
  auto_redraw = cfg.auto_redraw();

  view = puglInit (nullptr, nullptr);

  /* draw 128 bits from random generator to ensure that window class name is unique */
  string window_class = "SpectMorph_";
  for (size_t i = 0; i < 4; i++)
    window_class += string_printf ("%08x", g_random_int());

  int scaled_width, scaled_height;
  get_scaled_size (&scaled_width, &scaled_height);

  puglInitWindowClass (view, window_class.c_str());
  puglInitWindowSize (view, scaled_width, scaled_height);
  //puglInitWindowMinSize (view, 256, 256);
  puglInitResizable (view, resize);
  puglIgnoreKeyRepeat (view, false);
  if (win_id)
    puglInitWindowParent (view, win_id);
  puglCreateWindow (view, title.c_str());

  puglSetHandle (view, this);
  puglSetEventFunc (view, ::on_event);
  puglSetResizeFunc (view, ::on_resize);

  cairo_gl.reset (new CairoGL (scaled_width, scaled_height));

  puglEnterContext (view);
  glEnable (GL_DEPTH_TEST);
  glDepthFunc (GL_LESS);
  glClearColor (0.4f, 0.4f, 0.4f, 1.0f);
  cairo_gl->configure();
  // printf ("OpenGL Version: %s\n",(const char*) glGetString(GL_VERSION));
  puglLeaveContext(view, false);

  set_background_color (ThemeColor::WINDOW_BG);
}

Window::~Window()
{
  puglDestroy (view);
}

void
Window::on_widget_deleted (Widget *child)
{
  /* cheap weak pointer emulation */
  if (mouse_widget == child)
    mouse_widget = nullptr;
  if (enter_widget == child)
    enter_widget = nullptr;
  if (menu_widget == child)
    menu_widget = nullptr;
  if (keyboard_focus_widget == child)
    keyboard_focus_widget = nullptr;
  if (dialog_widget == child)
    dialog_widget = nullptr;
}

static vector<Widget *>
crawl_widgets (const vector<Widget *>& widgets)
{
  vector<Widget *> all_widgets;

  for (auto w : widgets)
    {
      all_widgets.push_back (w);
      auto c_result = crawl_widgets (w->children);
      all_widgets.insert (all_widgets.end(), c_result.begin(), c_result.end());
    }
  return all_widgets;
}

vector<Widget *>
Window::crawl_widgets()
{
  return ::crawl_widgets ({ this });
}

static int
get_layer (Widget *w, Widget *menu_widget, Widget *dialog_widget)
{
  if (w == menu_widget)
    return 1;
  if (w == dialog_widget)
    return 2;

  if (w->parent)
    return get_layer (w->parent, menu_widget, dialog_widget);
  else
    return 0; // no parent
}

static bool
get_visible_recursive (Widget *w)
{
  if (!w->visible())
    return false;

  if (w->parent)
    return get_visible_recursive (w->parent);
  else
    return true; // no parent
}

void
Window::on_display()
{
  // glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  for (int layer = 0; layer < 3; layer++)
    {
      if (dialog_widget && layer == 2) /* draw rest of ui darker if dialog is open */
        {
          cairo_rectangle (cairo_gl->cr, 0, 0, width * global_scale, height * global_scale);
          cairo_set_source_rgba (cairo_gl->cr, 0.0, 0, 0, 0.5);
          cairo_fill (cairo_gl->cr);
        }
      for (auto w : crawl_widgets())
        {
          if (get_layer (w, menu_widget, dialog_widget) == layer && get_visible_recursive (w))
            {
              cairo_t *cr = cairo_gl->cr;

              cairo_save (cr);

              cairo_scale (cr, global_scale, global_scale);

              // local coordinates
              cairo_translate (cr, w->abs_x(), w->abs_y());
              if (w->clipping())
                {
                  Rect visible_rect = w->abs_visible_rect();

                  // translate to widget local coordinates
                  visible_rect.move_to (visible_rect.x() - w->abs_x(), visible_rect.y() - w->abs_y());

                  cairo_rectangle (cr, visible_rect.x(), visible_rect.y(), visible_rect.width(), visible_rect.height());
                  cairo_clip (cr);
                }

              if (draw_grid && w == enter_widget)
                w->debug_fill (cr);

              w->draw (cr);
              cairo_restore (cr);
            }
        }
    }

  if (have_file_dialog)
    {
      cairo_rectangle (cairo_gl->cr, 0, 0, width * global_scale, height * global_scale);
      cairo_set_source_rgba (cairo_gl->cr, 0.0, 0, 0, 0.5);
      cairo_fill (cairo_gl->cr);
    }

  if (draw_grid)
    {
      cairo_t *cr = cairo_gl->cr;

      cairo_save (cr);
      cairo_scale (cr, global_scale, global_scale);

      cairo_set_source_rgb (cr, 0, 0, 0);
      cairo_set_line_width (cr, 0.5);

      for (double x = 8; x < width; x += 8)
        {
          for (double y = 8; y < height; y += 8)
            {
              cairo_move_to (cr, x - 2, y);
              cairo_line_to (cr, x + 2, y);
              cairo_move_to (cr, x, y - 2);
              cairo_line_to (cr, x, y + 2);
            }
        }
      cairo_stroke (cr);
      cairo_restore (cr);
    }

  //if (cairo_gl->first_frame()) // first frame is always a full redraw

  cairo_gl->draw (0, 0, cairo_gl->width(), cairo_gl->height());
}

void
Window::process_events()
{
  if (native_file_dialog)
    {
      native_file_dialog->process_events();

      if (!have_file_dialog)
        {
          /* file dialog closed - must be deleted after (not during) process_events */
          native_file_dialog.reset();
        }
    }

  if (0)
    {
      timeval tv;
      gettimeofday (&tv, 0);

      static double last_time_ms = 0;
      const double time_ms = tv.tv_sec + tv.tv_usec / 1000000.0;
      const double delta_time_ms = time_ms - last_time_ms;
      last_time_ms = time_ms;

      if (last_time_ms > 0)
        sm_debug ("process_delta_time %f\n", delta_time_ms);
    }
  puglProcessEvents (view);
}

static void
dump_event (const PuglEvent *event)
{
  switch (event->type)
    {
      case PUGL_NOTHING:            printf ("Event: nothing\n");
        break;
      case PUGL_BUTTON_PRESS:       printf ("Event: button press\n");
        break;
      case PUGL_BUTTON_RELEASE:     printf ("Event: button release\n");
        break;
      case PUGL_CONFIGURE:          printf ("Event: configure w%f h%f\n", event->configure.width, event->configure.height);
        break;
      case PUGL_EXPOSE:             printf ("Event: expose x%f y%f w%f h%f\n", event->expose.x, event->expose.y, event->expose.width, event->expose.height);
        break;
      case PUGL_CLOSE:              printf ("Event: close\n");
        break;
      case PUGL_KEY_PRESS:          printf ("Event: key press %c\n", event->key.character);
        break;
      case PUGL_KEY_RELEASE:        printf ("Event: key release\n");
        break;
      case PUGL_ENTER_NOTIFY:       printf ("Event: enter\n");
        break;
      case PUGL_LEAVE_NOTIFY:       printf ("Event: leave\n");
        break;
      case PUGL_MOTION_NOTIFY:      printf ("Event: motion\n");
        break;
      case PUGL_SCROLL:             printf ("Event: scroll: dx=%f dy=%f\n", event->scroll.dx, event->scroll.dy);
        break;
      case PUGL_FOCUS_IN:           printf ("Event: focus in\n");
        break;
      case PUGL_FOCUS_OUT:          printf ("Event: focus out\n");
        break;
    }
}

void
Window::on_event (const PuglEvent* event)
{
  if (0)
    dump_event (event);

  double ex, ey; /* global scale translated */
  Widget *current_widget = nullptr;

  /* as long as the file dialog is open, ignore user input */
  if (have_file_dialog && event->type != PUGL_EXPOSE && event->type != PUGL_CONFIGURE)
    return;

  switch (event->type)
    {
      case PUGL_BUTTON_PRESS:
        ex = event->button.x / global_scale;
        ey = event->button.y / global_scale;

        mouse_widget = find_widget_xy (ex, ey);
        mouse_widget->mouse_press (ex - mouse_widget->abs_x(), ey - mouse_widget->abs_y());

        if (auto_redraw)
          update();
        break;
      case PUGL_BUTTON_RELEASE:
        ex = event->button.x / global_scale;
        ey = event->button.y / global_scale;
        if (mouse_widget)
          {
            Widget *w = mouse_widget;
            w->mouse_release (ex - w->abs_x(), ey - w->abs_y());
            mouse_widget = nullptr;
          }
        if (auto_redraw)
          update();
        break;
      case PUGL_MOTION_NOTIFY:
        ex = event->motion.x / global_scale;
        ey = event->motion.y / global_scale;
        if (mouse_widget) /* user interaction with one widget */
          {
            current_widget = mouse_widget;
          }
        else
          {
            current_widget = find_widget_xy (ex, ey);
            if (enter_widget != current_widget)
              {
                if (enter_widget)
                  enter_widget->leave_event();

                enter_widget = current_widget;
                current_widget->enter_event();
              }
          }
        current_widget->motion (ex - current_widget->abs_x(), ey - current_widget->abs_y());
        if (auto_redraw)
          update();
        break;
      case PUGL_SCROLL:
        ex = event->scroll.x / global_scale;
        ey = event->scroll.y / global_scale;
        if (mouse_widget)
          current_widget = mouse_widget;
        else
          current_widget = find_widget_xy (ex, ey);

        current_widget->scroll (event->scroll.dx, event->scroll.dy);
        if (auto_redraw)
          update();
        break;
      case PUGL_KEY_PRESS:
        if (keyboard_focus_widget)
          keyboard_focus_widget->key_press_event (event->key);
        else
          {
            if (event->key.character == 'g')
              draw_grid = !draw_grid;
          }
        if (auto_redraw)
          update();
        break;
      case PUGL_CLOSE:
        if (m_close_callback)
          m_close_callback();
        break;
      case PUGL_EXPOSE:
        on_display();
        break;
      case PUGL_CONFIGURE:
        cairo_gl.reset (new CairoGL (event->configure.width, event->configure.height));
        puglEnterContext (view);
        cairo_gl->configure();
        puglLeaveContext (view, false);
        break;
      default:
        break;
    }
}

Widget *
Window::find_widget_xy (double ex, double ey)
{
  Widget *widget = this;

  if (menu_widget)
    widget = menu_widget;  // active menu => only children of the menu get clicks

  if (dialog_widget)
    widget = dialog_widget;

  for (auto w : ::crawl_widgets ({ widget })) // which child gets the click?
    {
      if (get_visible_recursive (w) && w->recursive_enabled() && w->abs_visible_rect().contains (ex, ey))
        {
          widget = w;
        }
    }
  return widget;
}

void
Window::show()
{
  puglPostRedisplay (view);
  puglShowWindow (view);
}

void
Window::open_file_dialog (const string& title, const string& filter_title, const string& filter, std::function<void(string)> callback)
{
  PuglNativeWindow win_id = puglGetNativeWindow (view);

  file_dialog_callback = callback;
  have_file_dialog = true;

  native_file_dialog.reset (NativeFileDialog::create (win_id, true, title, filter_title, filter));
  connect (native_file_dialog->signal_file_selected, this, &Window::on_file_selected);
}

void
Window::save_file_dialog (const string& title, const string& filter_title, const string& filter, std::function<void(string)> callback)
{
  PuglNativeWindow win_id = puglGetNativeWindow (view);

  file_dialog_callback = callback;
  have_file_dialog = true;

  native_file_dialog.reset (NativeFileDialog::create (win_id, false, title, filter_title, filter));
  connect (native_file_dialog->signal_file_selected, this, &Window::on_file_selected);
}

void
Window::on_file_selected (const std::string& filename)
{
  if (file_dialog_callback)
    {
      file_dialog_callback (filename);
      file_dialog_callback = nullptr;
    }
  have_file_dialog = false;
  update();
}

void
Window::update()
{
  puglPostRedisplay (view);
}

Window *
Window::window()
{
  return this;
}

void
Window::wait_event_fps()
{
  /* tradeoff between UI responsiveness and cpu usage caused by thread wakeups
   *
   * 60 fps should make the UI look smooth
   */
  const double frames_per_second = 60;

  usleep (1000 * 1000 / frames_per_second);
}

void
Window::wait_for_event()
{
  if (native_file_dialog)
    {
      /* need to wait for events of this view, and handle io for file dialog */
      wait_event_fps();
    }
  else
    {
      puglWaitForEvent (view);
    }
}

void
Window::set_menu_widget (Widget *widget)
{
  menu_widget = widget;
  mouse_widget = widget;
}

void
Window::set_close_callback (const std::function<void()>& callback)
{
  m_close_callback = callback;
}

void
Window::set_keyboard_focus (Widget *widget)
{
  keyboard_focus_widget = widget;
}

void
Window::set_dialog_widget (Widget *widget)
{
  dialog_widget = widget;
}

PuglNativeWindow
Window::native_window()
{
  return puglGetNativeWindow (view);
}

void
Window::set_gui_scaling (double s)
{
  global_scale = s;

  /* restart with this gui scaling next time */
  Config cfg;

  cfg.set_zoom (sm_round_positive (s * 100));
  cfg.store();

  puglPostResize (view);

  signal_update_size();
}

double
Window::gui_scaling()
{
  return global_scale;
}

void
Window::on_resize (int *win_width, int *win_height)
{
  get_scaled_size (win_width, win_height);
}

void
Window::get_scaled_size (int *w, int *h)
{
  *w = width * global_scale;
  *h = height * global_scale;
}
