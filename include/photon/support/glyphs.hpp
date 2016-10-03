/*=================================================================================================
   Copyright (c) 2016 Joel de Guzman

   Licensed under a Creative Commons Attribution-ShareAlike 4.0 International.
   http://creativecommons.org/licenses/by-sa/4.0/
=================================================================================================*/
#if !defined(PHOTON_GUI_LIB_GLYPHS_SEPTEMBER_26_2016)
#define PHOTON_GUI_LIB_GLYPHS_SEPTEMBER_26_2016

#include <photon/support/canvas.hpp>
#include <photon/support/text_utils.hpp>
#include <vector>
#include "cairo.h"

namespace photon
{
   ////////////////////////////////////////////////////////////////////////////////////////////////
   // glyphs: Text drawing and measuring utility
   ////////////////////////////////////////////////////////////////////////////////////////////////
   class glyphs
   {
   public:
                           glyphs(
                              char const* first, char const* last
                            , char const* face, float size
                            , int style = canvas::normal
                           );

                           glyphs(
                              char const* first, char const* last
                            , glyphs const& source
                           );

                           ~glyphs();

                           glyphs(glyphs&& rhs);
      glyphs&              operator=(glyphs&& rhs);

      void                 text(char const* first, char const* last);
      void                 draw(point pos, canvas& canvas_);
      void                 break_lines(float width, std::vector<glyphs>& lines);
      float                width() const;

                           // for_each F signature:
                           // bool f(char const* utf8, unsigned codepoint, float left, float right);
                           template <typename F>
      void                 for_each(F f);

      std::size_t          size() const      { return _last - _first; }
      char const*          begin() const     { return _first; }
      char const*          end() const       { return _last; }

      struct font_metrics
      {
         float             ascent;
         float             descent;
         float             leading;
      };

      font_metrics         metrics() const;

   private:
                           glyphs(glyphs const&) = delete;
      glyphs&              operator=(glyphs const& rhs) = delete;

                           glyphs(
                              char const* first, char const* last
                            , int glyph_start, int glyph_end
                            , int cluster_start, int cluster_end
                            , glyphs const& source
                            , bool strip_leading_spaces
                           );

      void                 build();

      using scaled_font = cairo_scaled_font_t;
      using glyph = cairo_glyph_t;
      using cluster = cairo_text_cluster_t;
      using cluster_flags = cairo_text_cluster_flags_t;

      char const*          _first;
      char const*          _last;
      scaled_font*         _scaled_font = nullptr;
      glyph*               _glyphs = nullptr;
      int                  _glyph_count;
      cluster*             _clusters = nullptr;
      int                  _cluster_count;
      cluster_flags        _clusterflags;
      bool                 _owns = true;
   };

   ////////////////////////////////////////////////////////////////////////////////////////////////
   template <typename F>
   inline void glyphs::for_each(F f)
   {
      if (_first == _last)
         return;
       
      unsigned  codepoint;
      unsigned  state = 0;
      int       glyph_index = 0;
      float     start_x = _glyphs->x;

      cairo_text_cluster_t* cluster = _clusters;
      for (auto i = _first; i != _last; ++i)
      {
         if (decode_utf8(state, codepoint, uint8_t(*i)) == utf8_accept)
         {
            cairo_glyph_t*  glyph = _glyphs + glyph_index;
            cairo_text_extents_t extents;
            cairo_scaled_font_glyph_extents(_scaled_font, glyph, 1, &extents);

            float x = glyph->x - start_x;
            if (!f(i, codepoint, x, x + extents.x_advance))
               break;
            glyph_index += cluster->num_glyphs;
            ++cluster;
         }
      }
   }
}

#endif
