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

#include <md4c.h>

#include "bge.h"

struct _BgeMarkdownRender
{
  GtkWidget parent_instance;

  char    *markdown;
  gboolean selectable;

  GtkWidget *box;
  GPtrArray *box_children;
};

G_DEFINE_FINAL_TYPE (BgeMarkdownRender, bge_markdown_render, GTK_TYPE_WIDGET);

enum
{
  PROP_0,

  PROP_MARKDOWN,
  PROP_SELECTABLE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
regenerate (BgeMarkdownRender *self);

typedef struct
{
  GtkBox    *box;
  GPtrArray *box_children;
  char      *beginning;
  GString   *markup;
  GArray    *block_stack;
  int        indent;
  int        list_index;
  MD_CHAR    list_prefix;
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
bge_markdown_render_dispose (GObject *object)
{
  BgeMarkdownRender *self = BGE_MARKDOWN_RENDER (object);

  g_clear_pointer (&self->markdown, g_free);

  g_clear_pointer (&self->box, gtk_widget_unparent);
  g_clear_pointer (&self->box_children, g_ptr_array_unref);

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
    case PROP_SELECTABLE:
      g_value_set_boolean (value, bge_markdown_render_get_selectable (self));
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
    case PROP_SELECTABLE:
      bge_markdown_render_set_selectable (self, g_value_get_boolean (value));
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

  props[PROP_SELECTABLE] =
      g_param_spec_boolean (
          "selectable",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  widget_class->get_request_mode = bge_markdown_render_get_request_mode;
  widget_class->measure          = bge_markdown_render_measure;
  widget_class->size_allocate    = bge_markdown_render_size_allocate;
}

static void
bge_markdown_render_init (BgeMarkdownRender *self)
{
  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (self->box, GTK_WIDGET (self));

  self->box_children = g_ptr_array_new ();
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
bge_markdown_render_get_selectable (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), FALSE);
  return self->selectable;
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

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARKDOWN]);
}

void
bge_markdown_render_set_selectable (BgeMarkdownRender *self,
                                    gboolean           selectable)
{
  g_return_if_fail (BGE_IS_MARKDOWN_RENDER (self));

  self->selectable = selectable;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTABLE]);
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

  if (self->markdown == NULL)
    return;

  ctx.box          = GTK_BOX (self->box);
  ctx.box_children = self->box_children;
  ctx.beginning    = self->markdown;
  ctx.markup       = NULL;
  ctx.block_stack  = g_array_new (FALSE, TRUE, sizeof (int));
  ctx.indent       = 0;
  ctx.list_index   = 0;
  ctx.list_prefix  = '\0';

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
      g_warning ("Images aren't implemented yet!");
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
      // g_warning ("Images aren't implemented yet!");
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
  ParseCtx        *ctx     = user_data;
  g_autofree char *escaped = NULL;

  g_assert (ctx->markup != NULL);

  if (type == MD_TEXT_SOFTBR &&
      ctx->markup->len > 0)
    g_string_append_c (ctx->markup, ' ');
  else if (type == MD_TEXT_BR &&
           ctx->markup->len > 0)
    g_string_append_c (ctx->markup, '\n');
  else
    {
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

        child = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (child);
      }
      break;
    case MD_BLOCK_QUOTE:
      {
        GtkWidget *bar   = NULL;
        GtkWidget *label = NULL;

        g_assert (ctx->markup != NULL);

        bar = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_size_request (bar, 10, -1);
        gtk_widget_set_margin_end (bar, 20);

        label = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (label);

        child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
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
        GtkWidget *label = NULL;

        label = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (label);
        gtk_widget_add_css_class (label, "monospace");
        gtk_widget_set_margin_start (label, 5);
        gtk_widget_set_margin_end (label, 5);
        gtk_widget_set_margin_top (label, 5);
        gtk_widget_set_margin_bottom (label, 5);

        child = gtk_frame_new (NULL);
        gtk_frame_set_child (GTK_FRAME (child), label);
      }
      break;

    case MD_BLOCK_P:
      {
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

/* End of bge-markdown-render.c */
