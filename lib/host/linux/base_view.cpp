/*=============================================================================
   Copyright (c) 2016-2020 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/app.hpp>
#include <infra/assert.hpp>
#include <elements/base_view.hpp>
#include <elements/window.hpp>
//#include <elements/support/text_utils.hpp> $$$ fixme $$$
#include <artist/resources.hpp>
#include <artist/canvas.hpp>
#include <gtk/gtk.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "GrContext.h"
#include "gl/GrGLInterface.h"
#include "SkSurface.h"

#include <map>
#include <string>
#include <iostream> // $$$ temp $$$

namespace cycfi::artist
{
   void init_paths()
   {
      add_search_path(fs::current_path() / "resources");
   }
}

namespace cycfi { namespace elements
{
   struct host_view
   {
      host_view();
      ~host_view();

      GtkWidget* widget = nullptr;

      // Mouse button click tracking
      std::uint32_t click_time = 0;
      std::uint32_t click_count = 0;

      // Scroll acceleration tracking
      std::uint32_t scroll_time = 0;

      point cursor_position;

      using key_map = std::map<key_code, key_action>;
      key_map keys;

      int modifiers = 0; // the latest modifiers

      GtkIMContext* im_context;
      GdkCursorType active_cursor_type = GDK_ARROW;

      point                      _size;
      sk_sp<const GrGLInterface> _xface;
      sk_sp<GrContext>           _ctx;
      sk_sp<SkSurface>           _surface;
      cairo_t*                   _cr;
   };

   struct platform_access
   {
      inline static host_view* get_host_view(base_view& view)
      {
         return view.host();
      }
   };

   float get_scale(GtkWidget* widget); // $$$ fixme $$$
   // {
   //    auto gdk_win = gtk_widget_get_window(widget);
   //    return 1.0f / gdk_window_get_scale_factor(gdk_win);
   // }

   host_view::host_view()
    : im_context(gtk_im_context_simple_new())
   {
   }

   host_view::~host_view()
   {
   }

   namespace
   {
      // Some globals
      host_view* host_view_under_cursor = nullptr;
      GdkCursorType view_cursor_type = GDK_ARROW;

      inline base_view& get(gpointer user_data)
      {
         return *reinterpret_cast<base_view*>(user_data);
      }

      gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data)
      {
         auto& view = get(user_data);
         auto* host_view_h = platform_access::get_host_view(view);
         host_view_h->_cr = cr;
         return false;
      }

      void realize(GtkGLArea* area, gpointer user_data)
      {
         gtk_gl_area_make_current(area);
         if (gtk_gl_area_get_error(area) != nullptr)
            return;

         auto& view = get(user_data);
         auto* host_view_h = platform_access::get_host_view(view);

         // glClearColor(1.0, 1.0, 1.0, 1.0);
         // glClear(GL_COLOR_BUFFER_BIT);
         host_view_h->_xface = GrGLMakeNativeInterface();
         host_view_h->_ctx = GrContext::MakeGL(host_view_h->_xface);
      }

      gboolean render(GtkGLArea* area, GdkGLContext* context, gpointer user_data)
      {
         auto& view = get(user_data);
         auto* host_view_h = platform_access::get_host_view(view);
         auto error = [](char const* msg) { throw std::runtime_error(msg); };

         auto w = gtk_widget_get_allocated_width(host_view_h->widget);
         auto h = gtk_widget_get_allocated_height(host_view_h->widget);
         if (host_view_h->_size.x != w || host_view_h->_size.y != h)
         {
            host_view_h->_surface.reset();
            host_view_h->_size.x = w;
            host_view_h->_size.y = h;
            // glViewport(0, 0, w, h);
            //host_view_h->_ctx->resize(w, h);

            std::cout << "resize" << std::endl;

            // $$$ sawat $$$
            // glClearColor(1.0, 1.0, 1.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
         }

         auto scale = 1.0 / get_scale(host_view_h->widget);
         bool redraw = false;

         if (!host_view_h->_surface)
         {
            GrGLint buffer;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);
            GrGLFramebufferInfo info;
            info.fFBOID = (GrGLuint) buffer;
            SkColorType colorType = kRGBA_8888_SkColorType;


            info.fFormat = GL_RGBA8;
            GrBackendRenderTarget target(
               w * scale
               , h * scale
               , 0, 8, info
            );

            host_view_h->_surface =
               SkSurface::MakeFromBackendRenderTarget(
                  host_view_h->_ctx.get(), target,
                  kBottomLeft_GrSurfaceOrigin, colorType, nullptr, nullptr
               );

            std::cout << "new surface" << std::endl;

            if (!host_view_h->_surface)
               error("Error: SkSurface::MakeRenderTarget returned null");

            redraw = true;

            gtk_widget_draw(host_view_h->widget, host_view_h->_cr);
            return true;

            // gtk_gl_area_queue_render((GtkGLArea*)host_view_h->widget);
            // return false;
         }

         SkCanvas* gpu_canvas = host_view_h->_surface->getCanvas();
         gpu_canvas->save();

         // gpu_canvas->scale(scale, scale);
         auto cnv = canvas{ gpu_canvas };
         cnv.pre_scale(scale);//host_view_h->_scale);

         auto start = std::chrono::steady_clock::now();
         view.draw(cnv, {0, 0, w, h});
         auto stop = std::chrono::steady_clock::now();
         auto elapsed = std::chrono::duration<double>{ stop - start }.count();

         std::cout << "draw " << (1.0/elapsed) << " fps" << std::endl;

         gpu_canvas->restore();
         host_view_h->_surface->flush();
         //_skia_context->swapBuffers();

         // if (redraw)
         //    render(area, context, user_data);

         // glFlush();
         return true;
      }

      template <typename Event>
      bool get_mouse(Event* event, mouse_button& btn, host_view* view)
      {
         btn.modifiers = 0;
         if (event->state & GDK_SHIFT_MASK)
            btn.modifiers |= mod_shift;
         if (event->state & GDK_CONTROL_MASK)
            btn.modifiers |= mod_control | mod_action;
         if (event->state & GDK_MOD1_MASK)
            btn.modifiers |= mod_alt;
         if (event->state & GDK_SUPER_MASK)
            btn.modifiers |= mod_action;

         btn.num_clicks = view->click_count;
         btn.pos = { float(event->x), float(event->y) };
         return true;
      }

      bool get_button(GdkEventButton* event, mouse_button& btn, host_view* view)
      {
         if (event->button > 4)
            return false;

         gint dbl_click_time;
         g_object_get(
            gtk_settings_get_default()
          , "gtk-double-click-time", &dbl_click_time
          , nullptr
         );

         switch (event->type)
         {
            case GDK_BUTTON_PRESS:
               btn.down = true;
               if ((event->time - view->click_time) < guint32(dbl_click_time))
                  ++view->click_count;
               else
                  view->click_count = 1;
               view->click_time = event->time;
               break;

            case GDK_BUTTON_RELEASE:
               btn.down = false;
               break;

            default:
               return false;
         }

         if (!get_mouse(event, btn, view))
            return false;
         return true;
      }

      gboolean on_button(GtkWidget* /* widget */, GdkEventButton* event, gpointer user_data)
      {
         auto& view = get(user_data);
         mouse_button btn;
         if (get_button(event, btn, platform_access::get_host_view(view)))
            view.click(btn);
         return true;
      }

      gboolean on_motion(GtkWidget* /* widget */, GdkEventMotion* event, gpointer user_data)
      {
         auto& base_view = get(user_data);
         host_view* view = platform_access::get_host_view(base_view);
         mouse_button btn;
         if (get_mouse(event, btn, view))
         {
            view->cursor_position = btn.pos;

            if (event->state & GDK_BUTTON1_MASK)
            {
               btn.down = true;
               btn.state = mouse_button::left;
            }
            else if (event->state & GDK_BUTTON2_MASK)
            {
               btn.down = true;
               btn.state = mouse_button::middle;
            }
            else if (event->state & GDK_BUTTON3_MASK)
            {
               btn.down = true;
               btn.state = mouse_button::right;
            }
            else
            {
               btn.down = false;
            }

            if (btn.down)
               base_view.drag(btn);
            else
               base_view.cursor(view->cursor_position, cursor_tracking::hovering);
         }
         return true;
      }

      gboolean on_scroll(GtkWidget* /* widget */, GdkEventScroll* event, gpointer user_data)
      {
         auto& base_view = get(user_data);
         auto* host_view_h = platform_access::get_host_view(base_view);
         auto elapsed = std::max<float>(10.0f, event->time - host_view_h->scroll_time);
         static constexpr float _1s = 100;
         host_view_h->scroll_time = event->time;

         float dx = 0;
         float dy = 0;
         float step = _1s / elapsed;

         switch (event->direction)
         {
            case GDK_SCROLL_UP:
               dy = step;
               break;
            case GDK_SCROLL_DOWN:
               dy = -step;
               break;
            case GDK_SCROLL_LEFT:
               dx = step;
               break;
            case GDK_SCROLL_RIGHT:
               dx = -step;
               break;
            case GDK_SCROLL_SMOOTH:
               dx = event->delta_x;
               dy = event->delta_y;
               break;
            default:
               break;
         }

         base_view.scroll(
            { dx, dy },
            { float(event->x), float(event->y) }
         );
         return true;
      }
   }

   static void change_window_cursor(GtkWidget* widget, GdkCursorType type)
   {
      GdkDisplay* display = gtk_widget_get_display(widget);
      GdkWindow* window = gtk_widget_get_window(widget);
      GdkCursor* cursor = gdk_cursor_new_for_display(display, type);
      gdk_window_set_cursor(window, cursor);
      g_object_unref(cursor);
   }

   gboolean on_event_crossing(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data)
   {
      auto& base_view = get(user_data);
      auto* host_view_h = platform_access::get_host_view(base_view);
      host_view_h->cursor_position = point{ float(event->x), float(event->y) };
      if (event->type == GDK_ENTER_NOTIFY)
      {
         base_view.cursor(host_view_h->cursor_position, cursor_tracking::entering);
         host_view_under_cursor = host_view_h;
         if (host_view_h->active_cursor_type != view_cursor_type)
         {
            change_window_cursor(widget, view_cursor_type);
            host_view_h->active_cursor_type = view_cursor_type;
         }
      }
      else
      {
         base_view.cursor(host_view_h->cursor_position, cursor_tracking::leaving);
         host_view_under_cursor = nullptr;
      }
      return true;
   }

   // Defined in key.cpp
   key_code translate_key(unsigned key);

   static void on_text_entry(GtkIMContext* /* context */, const gchar* str, gpointer user_data)
   {
      auto& base_view = get(user_data);
      auto* host_view_h = platform_access::get_host_view(base_view);
      auto cp = codepoint(str);
      base_view.text({ cp, host_view_h->modifiers });
   }

   int get_mods(int state)
   {
      int mods = 0;
      if (state & GDK_SHIFT_MASK)
         mods |= mod_shift;
      if (state & GDK_CONTROL_MASK)
         mods |= mod_control | mod_action;
      if (state & GDK_MOD1_MASK)
         mods |= mod_alt;
      if (state & GDK_SUPER_MASK)
         mods |= mod_super;

      return mods;
   }

   void handle_key(base_view& _view, host_view::key_map& keys, key_info k)
   {
      bool repeated = false;

      if (k.action == key_action::release)
      {
         keys.erase(k.key);
         return;
      }

      if (k.action == key_action::press
         && keys[k.key] == key_action::press)
         repeated = true;

      keys[k.key] = k.action;

      if (repeated)
         k.action = key_action::repeat;

      _view.key(k);
   }

   gboolean on_key(GtkWidget* widget, GdkEventKey* event, gpointer user_data)
   {
      auto& base_view = get(user_data);
      auto* host_view_h = platform_access::get_host_view(base_view);
      gtk_im_context_filter_keypress(host_view_h->im_context, event);

      int modifiers = get_mods(event->state);
      auto const action = event->type == GDK_KEY_PRESS? key_action::press : key_action::release;
      host_view_h->modifiers = modifiers;

      // We don't want the shift key handled when obtaining the keyval,
      // so we do this again here, instead of relying on event->keyval
      guint keyval = 0;
      gdk_keymap_translate_keyboard_state(
         gdk_keymap_get_for_display(gtk_widget_get_display(widget)),
         event->hardware_keycode,
         GdkModifierType(event->state & ~GDK_SHIFT_MASK),
         event->group,
         &keyval,
         nullptr, nullptr, nullptr);

      auto const key = translate_key(keyval);
      if (key == key_code::unknown)
         return false;

      handle_key(base_view, host_view_h->keys, { key, action, modifiers });
      return true;
   }

   void on_focus(GtkWidget* /* widget */, GdkEventFocus* event, gpointer user_data)
   {
      auto& base_view = get(user_data);
      if (event->in)
         base_view.begin_focus();
      else
         base_view.end_focus();
   }

   int poll_function(gpointer user_data)
   {
      auto& base_view = get(user_data);
      base_view.poll();
      return true;
   }

   // $$$ TODO: Investigate $$$
   // Somehow, this prevents us from having linker errors
   // Without this, we get undefined reference to `glXGetCurrentContext'
   auto proc = &glXGetProcAddress;

   GtkWidget* make_view(base_view& view, GtkWidget* parent)
   {
      auto error = [](char const* msg) { throw std::runtime_error(msg); };
      if (!proc)
         error("Error: glXGetProcAddress is null");

      auto* content_view = gtk_gl_area_new();
      auto* host_view_h = platform_access::get_host_view(view);

      gtk_container_add(GTK_CONTAINER(parent), content_view);

      g_signal_connect(content_view, "render",
         G_CALLBACK(render), &view);
      g_signal_connect(content_view, "realize",
         G_CALLBACK(realize), &view);

      // Subscribe to content_view events
      // g_signal_connect(content_view, "configure-event",
      //    G_CALLBACK(on_configure), &view);
      g_signal_connect(content_view, "draw",
         G_CALLBACK(on_draw), &view);
      g_signal_connect(content_view, "button-press-event",
         G_CALLBACK(on_button), &view);
      g_signal_connect (content_view, "button-release-event",
         G_CALLBACK(on_button), &view);
      g_signal_connect(content_view, "motion-notify-event",
         G_CALLBACK(on_motion), &view);
      g_signal_connect(content_view, "scroll-event",
         G_CALLBACK(on_scroll), &view);
      g_signal_connect(content_view, "enter-notify-event",
         G_CALLBACK(on_event_crossing), &view);
      g_signal_connect(content_view, "leave-notify-event",
         G_CALLBACK(on_event_crossing), &view);

      gtk_widget_set_events(content_view,
         gtk_widget_get_events(content_view)
         | GDK_BUTTON_PRESS_MASK
         | GDK_BUTTON_RELEASE_MASK
         | GDK_POINTER_MOTION_MASK
         | GDK_SCROLL_MASK
         | GDK_ENTER_NOTIFY_MASK
         | GDK_LEAVE_NOTIFY_MASK
         // | GDK_SMOOTH_SCROLL_MASK
      );

      // Subscribe to parent events
      g_signal_connect(parent, "key-press-event",
         G_CALLBACK(on_key), &view);
      g_signal_connect(parent, "key-release-event",
         G_CALLBACK(on_key), &view);
      g_signal_connect(parent, "focus-in-event",
         G_CALLBACK(on_focus), &view);
      g_signal_connect(parent, "focus-out-event",
         G_CALLBACK(on_focus), &view);

      gtk_widget_set_events(parent,
         gtk_widget_get_events(parent)
         | GDK_KEY_PRESS_MASK
         | GDK_FOCUS_CHANGE_MASK
      );

      // Subscribe to text entry commit
      g_signal_connect(view.host()->im_context, "commit",
         G_CALLBACK(on_text_entry), &view);

      // Create 1ms timer
      g_timeout_add(1, poll_function, &view);

      // $$$ TODO: do this $$$
      // host_view_h->_scale = gdk_window_get_scale_factor(w);
      return content_view;
   }

   // Defined in window.cpp
   GtkWidget* get_window(host_window& h);
   void on_window_activate(host_window& h, std::function<void()> f);

   // Defined in app.cpp
   bool app_is_activated();

//   struct init_view_class $$$ fixme $$$
//   {
//      init_view_class()
//      {
//         auto pwd = fs::current_path();
//         auto resource_path = pwd / "resources";
//         resource_paths.push_back(resource_path);
//      }
//   };

   base_view::base_view(extent /* size_ */)
    : base_view(new host_view)
   {
      // $$$ FIXME: Implement Me $$$
      CYCFI_ASSERT(false, "Unimplemented");
   }

   base_view::base_view(host_view_handle h)
    : _view(h)
   {
//      static init_view_class init; $$$ fixme $$$
   }

   base_view::base_view(host_window_handle h)
    : base_view(new host_view)
   {
      auto make_view =
         [this, h]()
         {
            _view->widget = elements::make_view(*this, get_window(*h));
         };

      if (app_is_activated())
         make_view();
      else
         on_window_activate(*h, make_view);
   }

   base_view::~base_view()
   {
      if (host_view_under_cursor == _view)
         host_view_under_cursor = nullptr;
      delete _view;
   }

   point base_view::cursor_pos() const
   {
      return _view->cursor_position;
   }

   elements::extent base_view::size() const
   {
      auto x = gtk_widget_get_allocated_width(_view->widget);
      auto y = gtk_widget_get_allocated_height(_view->widget);
      return { float(x), float(y) };
   }

   void base_view::size(elements::extent p)
   {
      // $$$ Wrong: don't size the window!!! $$$
      gtk_window_resize(GTK_WINDOW(_view->widget), p.x, p.y);
   }

   float base_view::hdpi_scale() const
   {
      return get_scale(_view->widget);
   }

   void base_view::refresh()
   {
      auto x = gtk_widget_get_allocated_width(_view->widget);
      auto y = gtk_widget_get_allocated_height(_view->widget);
      refresh({ 0, 0, float(x), float(y) });
   }

   void base_view::refresh(rect area)
   {
      gtk_gl_area_queue_render((GtkGLArea*)_view->widget);
      // auto scale = 1; // get_scale(_view->widget);
      // gtk_widget_queue_draw_area(_view->widget,
      //    area.left * scale,
      //    area.top * scale,
      //    area.width() * scale,
      //    area.height() * scale
      // );
   }

   std::string clipboard()
   {
      GtkClipboard* clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
      gchar* text = gtk_clipboard_wait_for_text(clip);
      return std::string(text);
   }

   void clipboard(std::string_view text)
   {
      GtkClipboard* clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
      gtk_clipboard_set_text(clip, text.data(), text.size());
   }

   void set_cursor(cursor_type type)
   {
      switch (type)
      {
         case cursor_type::arrow:
            view_cursor_type = GDK_ARROW;
            break;
         case cursor_type::ibeam:
            view_cursor_type = GDK_XTERM;
            break;
         case cursor_type::cross_hair:
            view_cursor_type = GDK_CROSSHAIR;
            break;
         case cursor_type::hand:
            view_cursor_type = GDK_HAND2;
            break;
         case cursor_type::h_resize:
            view_cursor_type = GDK_SB_H_DOUBLE_ARROW;
            break;
         case cursor_type::v_resize:
            view_cursor_type = GDK_SB_V_DOUBLE_ARROW;
            break;
      }

      auto* host_view_h = host_view_under_cursor;
      if (host_view_h && host_view_h->active_cursor_type != view_cursor_type)
      {
         change_window_cursor(host_view_under_cursor->widget, view_cursor_type);
         host_view_h->active_cursor_type = view_cursor_type;
      }
   }
}}

