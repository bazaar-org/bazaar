/* bge-markdown-render.c
 *
 * Copyright 2025 Eva M
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "BGE::MARKDOWN-RENDER"

#include <gtksourceview/gtksource.h>
#include <md4c.h>

#include "bge-marshalers.h"
#include "bge.h"

struct _BgeMarkdownRender
{
  GtkWidget parent_instance;

  char    *markdown;
  gboolean dark;

  GtkWidget *box;
  GPtrArray *box_children;
  GPtrArray *source_views;
};

G_DEFINE_FINAL_TYPE (BgeMarkdownRender, bge_markdown_render, GTK_TYPE_WIDGET);

enum
{
  PROP_0,

  PROP_MARKDOWN,
  PROP_DARK,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_BIND_INLINE_URI,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
regenerate (BgeMarkdownRender *self);

typedef struct
{
  BgeMarkdownRender *self;
  GtkBox            *box;
  GPtrArray         *box_children;
  char              *beginning;
  GString           *markup;
  GArray            *block_stack;
  int                indent;
  int                list_index;
  MD_CHAR            list_prefix;
  GPtrArray         *source_views;
} ParseCtx;

static int
enter_block (MD_BLOCKTYPE type,
             void        *detail,
             void        *user_data);

static int
leave_block (MD_BLOCKTYPE type,
             void        *detail,
             void        *user_data);

static int
enter_span (MD_SPANTYPE type,
            void       *detail,
            void       *user_data);

static int
leave_span (MD_SPANTYPE type,
            void       *detail,
            void       *user_data);

static int
text (MD_TEXTTYPE    type,
      const MD_CHAR *buf,
      MD_SIZE        size,
      void          *user_data);

static const MD_PARSER parser = {
  .flags       = MD_FLAG_COLLAPSEWHITESPACE |
                 MD_FLAG_NOHTMLBLOCKS |
                 MD_FLAG_NOHTMLSPANS,
  .enter_block = enter_block,
  .leave_block = leave_block,
  .enter_span  = enter_span,
  .leave_span  = leave_span,
  .text        = text,
};

static int
terminate_block (MD_BLOCKTYPE type,
                 void        *detail,
                 void        *user_data);

static void
check_dark_mode (BgeMarkdownRender *self);

static void
bge_markdown_render_dispose (GObject *object)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (object);

  g_clear_pointer (&self->markdown, g_free);

  g_clear_pointer (&self->box, gtk_widget_unparent);
  g_clear_pointer (&self->box_children, g_ptr_array_unref);
  g_clear_pointer (&self->source_views, g_ptr_array_unref);

  G_OBJECT_CLASS (bge_markdown_render_parent_class)->dispose (object);
}

static void
bge_markdown_render_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (object);

  switch (prop_id)
    {
    case PROP_MARKDOWN:
      g_value_set_string (value, bge_markdown_render_get_markdown (self));
      break;
    case PROP_DARK:
      g_value_set_boolean (value, bge_markdown_render_get_dark (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_markdown_render_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (object);

  switch (prop_id)
    {
    case PROP_MARKDOWN:
      bge_markdown_render_set_markdown (self, g_value_get_string (value));
      break;
    case PROP_DARK:
      bge_markdown_render_set_dark (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GtkSizeRequestMode
bge_markdown_render_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
bge_markdown_render_measure (GtkWidget     *widget,
                             GtkOrientation orientation,
                             int            for_size,
                             int           *minimum,
                             int           *natural,
                             int           *minimum_baseline,
                             int           *natural_baseline)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (widget);

  gtk_widget_measure (
      self->box,
      orientation,
      for_size,
      minimum,
      natural,
      minimum_baseline,
      natural_baseline);
}

static void
bge_markdown_render_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (widget);

  if (gtk_widget_should_layout (self->box))
    gtk_widget_allocate (self->box, width, height, baseline, NULL);
}

static void
bge_markdown_render_class_init (BgeMarkdownRenderClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bge_markdown_render_set_property;
  object_class->get_property = bge_markdown_render_get_property;
  object_class->dispose      = bge_markdown_render_dispose;

  props[PROP_MARKDOWN] =
      g_param_spec_string (
          "markdown",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DARK] =
      g_param_spec_boolean (
          "dark",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_BIND_INLINE_URI] =
      g_signal_new (
          "bind-inline-uri",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          bge_marshal_OBJECT__STRING_STRING,
          GTK_TYPE_WIDGET,
          2,
          G_TYPE_STRING,
          G_TYPE_STRING);
  g_signal_set_va_marshaller (
      signals[SIGNAL_BIND_INLINE_URI],
      G_TYPE_FROM_CLASS (klass),
      bge_marshal_OBJECT__STRING_STRINGv);

  widget_class->get_request_mode = bge_markdown_render_get_request_mode;
  widget_class->measure          = bge_markdown_render_measure;
  widget_class->size_allocate    = bge_markdown_render_size_allocate;
}

static void
bge_markdown_render_init (BgeMarkdownRender *self)
{
  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 7);
  gtk_widget_set_parent (self->box, GTK_WIDGET (self));

  self->box_children = g_ptr_array_new ();
  self->source_views = g_ptr_array_new ();
}

GtkWidget *
bge_markdown_render_new (void)
{
  return g_object_new (BGE_TYPE_MARKDOWN_RENDER, NULL);
}

const char *
bge_markdown_render_get_markdown (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), NULL);
  return self->markdown;
}

gboolean
bge_markdown_render_get_dark (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), FALSE);
  return self->dark;
}

void
bge_markdown_render_set_markdown (BgeMarkdownRender *self,
                                  const char        *markdown)
{
  g_return_if_fail (BGE_IS_MARKDOWN_RENDER (self));

  g_clear_pointer (&self->markdown, g_free);
  if (markdown != NULL)
    self->markdown = g_strdup (markdown);

  regenerate (self);
  check_dark_mode (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARKDOWN]);
}

void
bge_markdown_render_set_dark (BgeMarkdownRender *self,
                              gboolean           dark)
{
  g_return_if_fail (BGE_IS_MARKDOWN_RENDER (self));

  if (!!dark == !!self->dark)
    return;

  self->dark = dark;
  check_dark_mode (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DARK]);
}

static void
regenerate (BgeMarkdownRender *self)
{
  int      iresult = 0;
  ParseCtx ctx     = { 0 };

  for (guint i = 0; i < self->box_children->len; i++)
    {
      GtkWidget *child = NULL;

      child = g_ptr_array_index (self->box_children, i);
      gtk_box_remove (GTK_BOX (self->box), child);
    }
  g_ptr_array_set_size (self->box_children, 0);
  g_ptr_array_set_size (self->source_views, 0);

  if (self->markdown == NULL)
    return;

  ctx.self         = self;
  ctx.box          = GTK_BOX (self->box);
  ctx.box_children = self->box_children;
  ctx.beginning    = self->markdown;
  ctx.markup       = NULL;
  ctx.block_stack  = g_array_new (FALSE, TRUE, sizeof (int));
  ctx.indent       = 0;
  ctx.list_index   = 0;
  ctx.list_prefix  = '\0';
  ctx.source_views = self->source_views;

  iresult = md_parse (
      self->markdown,
      strlen (self->markdown),
      &parser,
      &ctx);

  if (ctx.markup != NULL)
    g_string_free (ctx.markup, TRUE);
  g_array_unref (ctx.block_stack);

  if (iresult != 0)
    {
      g_warning ("Failed to parse markdown text");
      return;
    }
}

static int
enter_block (MD_BLOCKTYPE type,
             void        *detail,
             void        *user_data)
{
  ParseCtx *ctx = user_data;

  if (ctx->markup != NULL)
    {
      terminate_block (type, detail, user_data);
      g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1) = -1;
    }

  if (type == MD_BLOCK_UL)
    {
      MD_BLOCK_UL_DETAIL *ul_detail = detail;

      ctx->indent++;
      ctx->list_index  = 0;
      ctx->list_prefix = ul_detail->mark;
    }
  else if (type == MD_BLOCK_OL)
    {
      MD_BLOCK_OL_DETAIL *ol_detail = detail;

      ctx->indent++;
      ctx->list_index  = 0;
      ctx->list_prefix = ol_detail->mark_delimiter;
    }
  else
    ctx->markup = g_string_new (NULL);

  g_array_append_val (ctx->block_stack, type);

  return 0;
}

static int
leave_block (MD_BLOCKTYPE type,
             void        *detail,
             void        *user_data)
{
  ParseCtx *ctx = user_data;

  g_assert (ctx->block_stack->len > 0);
  if (g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1) >= 0)
    terminate_block (type, detail, user_data);
  g_array_set_size (ctx->block_stack, ctx->block_stack->len - 1);

  return 0;
}

static int
enter_span (MD_SPANTYPE type,
            void       *detail,
            void       *user_data)
{
  ParseCtx *ctx = user_data;

  g_assert (ctx->markup != NULL);

  switch (type)
    {
    case MD_SPAN_EM:
      g_string_append (ctx->markup, "<b>");
      break;
    case MD_SPAN_STRONG:
      g_string_append (ctx->markup, "<big>");
      break;
    case MD_SPAN_A:
      {
        MD_SPAN_A_DETAIL *a_detail = detail;
        g_autofree char  *href     = NULL;
        g_autofree char  *title    = NULL;

        href = g_strndup (a_detail->href.text, a_detail->href.size);
        if (a_detail->title.text != NULL)
          title = g_strndup (a_detail->title.text, a_detail->title.size);

        g_string_append_printf (
            ctx->markup,
            "<a href=\"%s\" title=\"%s\">",
            href,
            title != NULL ? title : href);
      }
      break;
    case MD_SPAN_IMG:
      break;
    case MD_SPAN_CODE:
      g_string_append (ctx->markup, "<tt>");
      break;
    case MD_SPAN_DEL:
      g_string_append (ctx->markup, "<s>");
      break;
    case MD_SPAN_U:
      g_string_append (ctx->markup, "<u>");
      break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    default:
      g_warning ("Unsupported markdown event (Did you use latex/wikilinks?)");
      return 1;
      break;
    }

  return 0;
}

static int
leave_span (MD_SPANTYPE type,
            void       *detail,
            void       *user_data)
{
  ParseCtx *ctx = user_data;

  g_assert (ctx->markup != NULL);

  switch (type)
    {
    case MD_SPAN_EM:
      g_string_append (ctx->markup, "</b>");
      break;
    case MD_SPAN_STRONG:
      g_string_append (ctx->markup, "</big>");
      break;
    case MD_SPAN_A:
      g_string_append (ctx->markup, "</a>");
      break;
    case MD_SPAN_IMG:
      {
        MD_SPAN_IMG_DETAIL *img_detail = detail;
        g_autofree char    *title      = NULL;
        g_autofree char    *src        = NULL;
        GtkWidget          *widget     = NULL;

        if (img_detail->title.text != NULL)
          title = g_strndup (img_detail->title.text, img_detail->title.size);
        if (img_detail->src.text != NULL)
          src = g_strndup (img_detail->src.text, img_detail->src.size);

        g_signal_emit (ctx->self, signals[SIGNAL_BIND_INLINE_URI], 0, title, src, &widget);
        if (widget != NULL)
          {
            gtk_widget_set_margin_start (widget, 10 * ctx->indent);
            gtk_box_append (ctx->box, widget);
            g_ptr_array_add (ctx->box_children, widget);
          }
      }
      break;
    case MD_SPAN_CODE:
      g_string_append (ctx->markup, "</tt>");
      break;
    case MD_SPAN_DEL:
      g_string_append (ctx->markup, "</s>");
      break;
    case MD_SPAN_U:
      g_string_append (ctx->markup, "</u>");
      break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    default:
      g_warning ("Unsupported markdown event (Did you use latex/wikilinks?)");
      return 1;
      break;
    }

  return 0;
}

static int
text (MD_TEXTTYPE    type,
      const MD_CHAR *buf,
      MD_SIZE        size,
      void          *user_data)
{
  ParseCtx *ctx   = user_data;
  int       block = -1;

  g_assert (ctx->markup != NULL);
  if (ctx->block_stack->len > 0)
    block = g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1);

  if (type == MD_TEXT_SOFTBR &&
      ctx->markup->len > 0)
    g_string_append_c (ctx->markup, ' ');
  else if (type == MD_TEXT_BR &&
           ctx->markup->len > 0)
    g_string_append_c (ctx->markup, '\n');
  else if (block == MD_BLOCK_CODE)
    g_string_append_len (ctx->markup, buf, size);
  else
    {
      g_autofree char *escaped = NULL;

      escaped = g_markup_escape_text (buf, size);
      g_string_append (ctx->markup, escaped);
    }

  return 0;
}

static int
terminate_block (MD_BLOCKTYPE type,
                 void        *detail,
                 void        *user_data)
{
  ParseCtx  *ctx    = user_data;
  int        parent = 0;
  GtkWidget *child  = NULL;

  g_assert (ctx->block_stack->len > 0);
  if (ctx->block_stack->len > 1)
    parent = g_array_index (ctx->block_stack, int, ctx->block_stack->len - 2);

  if (ctx->markup != NULL)
    {
      if (ctx->markup->len > 0 &&
          !g_unichar_isgraph (ctx->markup->str[ctx->markup->len - 1]))
        g_string_truncate (ctx->markup, ctx->markup->len - 1);
    }

#define SET_DEFAULTS(_label_widget)                                            \
  G_STMT_START                                                                 \
  {                                                                            \
    gtk_label_set_use_markup (GTK_LABEL (_label_widget), TRUE);                \
    gtk_label_set_wrap (GTK_LABEL (_label_widget), TRUE);                      \
    gtk_label_set_wrap_mode (GTK_LABEL (_label_widget), PANGO_WRAP_WORD_CHAR); \
    gtk_label_set_xalign (GTK_LABEL (_label_widget), 0.0);                     \
    gtk_label_set_selectable (GTK_LABEL (_label_widget), TRUE);                \
  }                                                                            \
  G_STMT_END

  switch (type)
    {
    case MD_BLOCK_DOC:
      {
        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        child = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (child);
      }
      break;
    case MD_BLOCK_QUOTE:
      {
        GtkWidget *bar   = NULL;
        GtkWidget *label = NULL;

        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        bar = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_size_request (bar, 10, -1);
        gtk_widget_set_margin_end (bar, 20);

        label = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (label);

        child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_append (GTK_BOX (child), bar);
        gtk_box_append (GTK_BOX (child), label);
      }
      break;
    case MD_BLOCK_UL:
      {
        // MD_BLOCK_UL_DETAIL *ul_detail = detail;

        if (ctx->markup == NULL)
          ctx->indent--;
      }
      break;
    case MD_BLOCK_OL:
      {
        // MD_BLOCK_OL_DETAIL *ol_detail = detail;

        if (ctx->markup == NULL)
          ctx->indent--;
      }
      break;
    case MD_BLOCK_LI:
      {
        // MD_BLOCK_LI_DETAIL *li_detail = detail;
        GtkWidget *prefix = NULL;
        GtkWidget *label  = NULL;

        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        g_assert (parent == MD_BLOCK_UL ||
                  parent == MD_BLOCK_OL);

        if (parent == MD_BLOCK_OL)
          {
            g_autofree char *prefix_text = NULL;

            prefix_text = g_strdup_printf ("%d%c", ctx->list_index, ctx->list_prefix);
            prefix      = gtk_label_new (prefix_text);
            gtk_widget_add_css_class (prefix, "caption");
          }
        else
          {
            /* TODO:

               `ctx->list_prefix` is '-', '+', '*'

               maybe handle these?
               */

            prefix = gtk_image_new_from_icon_name ("circle-filled-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (prefix), 6);
            gtk_widget_set_margin_top (prefix, 6);
          }
        gtk_widget_add_css_class (prefix, "dimmed");
        gtk_widget_set_valign (prefix, GTK_ALIGN_START);

        label = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (label);

        child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append (GTK_BOX (child), prefix);
        gtk_box_append (GTK_BOX (child), label);

        ctx->list_index++;
      }
      break;

    case MD_BLOCK_HR:
      child = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      break;

    case MD_BLOCK_H:
      {
        MD_BLOCK_H_DETAIL *h_detail  = detail;
        const char        *css_class = NULL;

        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        child = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (child);

        switch (h_detail->level)
          {
          case 1:
            css_class = "title-1";
            break;
          case 2:
            css_class = "title-2";
            break;
          case 3:
            css_class = "title-3";
            break;
          case 4:
            css_class = "title-4";
            break;
          case 5:
            css_class = "heading";
            break;
          case 6:
          default:
            css_class = "caption-heading";
            break;
          }
        gtk_widget_add_css_class (child, css_class);
      }
      break;

    case MD_BLOCK_CODE:
      {
        MD_BLOCK_CODE_DETAIL *code_detail  = detail;
        g_autofree char      *lang_id      = NULL;
        GtkSourceLanguage    *language     = NULL;
        g_autoptr (GtkSourceBuffer) buffer = NULL;
        GtkWidget *view                    = NULL;
        GtkWidget *window                  = NULL;

        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        if (code_detail->lang.text != NULL)
          {
            lang_id  = g_strndup (code_detail->lang.text, code_detail->lang.size);
            language = gtk_source_language_manager_get_language (
                gtk_source_language_manager_get_default (), lang_id);
          }

        if (language != NULL)
          buffer = gtk_source_buffer_new_with_language (language);
        else
          buffer = gtk_source_buffer_new (NULL);
        gtk_text_buffer_set_text (
            GTK_TEXT_BUFFER (buffer),
            ctx->markup->str, ctx->markup->len);

        view = gtk_source_view_new_with_buffer (buffer);
        g_ptr_array_add (ctx->source_views, view);
        gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
        gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), TRUE);
        gtk_widget_add_css_class (view, "monospace");

        window = gtk_scrolled_window_new ();
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window), view);

        child = gtk_frame_new (lang_id);
        gtk_frame_set_child (GTK_FRAME (child), window);
      }
      break;

    case MD_BLOCK_P:
      {
        g_assert (ctx->markup != NULL);
        if (ctx->markup->len == 0)
          break;

        child = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (child);
        gtk_widget_add_css_class (child, "body");
      }
      break;

    case MD_BLOCK_HTML:
    case MD_BLOCK_TABLE:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_TR:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
    default:
      g_warning ("Unsupported markdown event (Did you use html/tables?)");
      return 1;
    }

#undef SET_DEFAULTS

  if (child != NULL)
    {
      gtk_widget_set_margin_start (child, 10 * ctx->indent);
      gtk_box_append (ctx->box, child);
      g_ptr_array_add (ctx->box_children, child);
    }

  if (ctx->markup != NULL)
    {
      g_string_free (ctx->markup, TRUE);
      ctx->markup = NULL;
    }

  return 0;
}

static void
check_dark_mode (BgeMarkdownRender *self)
{
  const char           *id     = NULL;
  GtkSourceStyleScheme *scheme = NULL;

  if (self->dark)
    id = "Adwaita-dark";
  else
    id = "Adwaita";

  scheme = gtk_source_style_scheme_manager_get_scheme (
      gtk_source_style_scheme_manager_get_default (),
      id);
  for (guint i = 0; i < self->source_views->len; i++)
    {
      GtkSourceView *view   = NULL;
      GtkTextBuffer *buffer = NULL;

      view   = g_ptr_array_index (self->source_views, i);
      buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
      if (scheme != NULL)
        {
          gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (buffer), TRUE);
          gtk_source_buffer_set_style_scheme (GTK_SOURCE_BUFFER (buffer), scheme);
        }
      else
        gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (buffer), FALSE);
    }
}

/* End of bge-markdown-render.c */
