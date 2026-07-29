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

/**
 * BgeMarkdownRender:
 *
 * Renders markdown input using regular GTK widgets.
 *
 * For code blocks with a specified language, BgeMarkdownRender will attempt to
 * apply gtksourceview styling.
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
  gboolean wrap_tables;
  int      spacing;

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
  PROP_WRAP_TABLES,
  PROP_SPACING,

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
  GPtrArray         *container_stack;
  int                indent;
  int                list_index;
  MD_CHAR            list_prefix;
  GPtrArray         *source_views;
  GString           *alt_text;
  gboolean           in_image;
  GtkWidget         *grid;
  guint              grid_rows;
  guint              grid_columns;
  guint              grid_row_idx;
  guint              grid_column_idx;
} ParseCtx;

static GtkBox *
current_container (ParseCtx *ctx)
{
  if (ctx->container_stack->len > 0)
    return g_ptr_array_index (ctx->container_stack, ctx->container_stack->len - 1);
  return ctx->box;
}

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
                 MD_FLAG_NOHTMLSPANS |
                 MD_FLAG_STRIKETHROUGH |
                 MD_FLAG_TABLES,
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
    case PROP_WRAP_TABLES:
      g_value_set_boolean (value, bge_markdown_render_get_wrap_tables (self));
      break;
    case PROP_SPACING:
      g_value_set_int (value, bge_markdown_render_get_spacing (self));
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
    case PROP_WRAP_TABLES:
      bge_markdown_render_set_wrap_tables (self, g_value_get_boolean (value));
      break;
    case PROP_SPACING:
      bge_markdown_render_set_spacing (self, g_value_get_int (value));
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

  /**
   * BgeMarkdownRender:markdown:
   *
   * Raw markdown text.
   */
  props[PROP_MARKDOWN] =
      g_param_spec_string (
          "markdown",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeMarkdownRender:dark:
   *
   * Whether to apply dark styling to widgets where applicable.
   */
  props[PROP_DARK] =
      g_param_spec_boolean (
          "dark",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeMarkdownRender:wrap-tables:
   *
   * Whether if table elements should scroll horizontally or wrap.
   */
  props[PROP_WRAP_TABLES] =
      g_param_spec_boolean (
          "wrap-tables",
          NULL, NULL, TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeMarkdownRender:spacing:
   *
   * Vertical spacing between rendered block-level elements, in pixels.
   */
  props[PROP_SPACING] =
      g_param_spec_int (
          "spacing",
          NULL, NULL,
          0, G_MAXINT, 7,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * BgeMarkdownRender::bind-inline-uri:
   * @markdown: the object that received the signal
   * @title: a string title
   * @src: a string source uri
   *
   * Emitted when an inline image is encountered during parsing. The callback
   * should respond with a new widget to add to represent the uri. If no
   * callback is registered for this signal, or if the callback returns NULL, a
   * [class@Gtk.Picture] is automatically constructed and used.
   *
   * Return: a newly allocated widget to add to the markdown view
   */
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
  self->spacing     = 7;
  self->wrap_tables = TRUE;

  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, self->spacing);
  gtk_widget_set_parent (self->box, GTK_WIDGET (self));

  self->box_children = g_ptr_array_new ();
  self->source_views = g_ptr_array_new ();
}

/**
 * bge_markdown_render_new:
 *
 * Creates a new `BgeMarkdownRender` object.
 *
 * Returns: The newly created `BgeMarkdownRender` object.
 */
GtkWidget *
bge_markdown_render_new (void)
{
  return g_object_new (BGE_TYPE_MARKDOWN_RENDER, NULL);
}

/**
 * bge_markdown_render_get_markdown:
 * @self: a `BgeMarkdown`
 *
 * Gets [property@Bge.MarkdownRender:markdown].
 *
 * Returns: (nullable) (transfer none): the value of the property
 */
const char *
bge_markdown_render_get_markdown (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), NULL);
  return self->markdown;
}

/**
 * bge_markdown_render_get_dark:
 * @self: a `BgeMarkdown`
 *
 * Gets [property@Bge.MarkdownRender:dark].
 *
 * Returns: the value of the property
 */
gboolean
bge_markdown_render_get_dark (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), FALSE);
  return self->dark;
}

/**
 * bge_markdown_render_set_markdown:
 * @self: a `BgeMarkdownRender`
 * @markdown: a string
 *
 * Sets [property@Bge.MarkdownRender:markdown].
 */
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

/**
 * bge_markdown_render_set_dark:
 * @self: a `BgeMarkdownRender`
 * @dark: a boolean
 *
 * Sets [property@Bge.MarkdownRender:dark].
 */
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

/**
 * bge_markdown_render_get_wrap_tables:
 * @self: a `BgeMarkdownRender`
 *
 * Gets [property@Bge.MarkdownRender:wrap-tables].
 *
 * Returns: the value of the property
 */
gboolean
bge_markdown_render_get_wrap_tables (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), FALSE);
  return self->wrap_tables;
}

/**
 * bge_markdown_render_set_wrap_tables:
 * @self: a `BgeMarkdownRender`
 * @wrap_tables: a boolean
 *
 * Sets [property@Bge.MarkdownRender:wrap-tables].
 */
void
bge_markdown_render_set_wrap_tables (BgeMarkdownRender *self,
                                     gboolean           wrap_tables)
{
  g_return_if_fail (BGE_IS_MARKDOWN_RENDER (self));

  wrap_tables = !!wrap_tables;
  if (self->wrap_tables == wrap_tables)
    return;

  self->wrap_tables = wrap_tables;
  regenerate (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WRAP_TABLES]);
}

/**
 * bge_markdown_render_get_spacing:
 * @self: a `BgeMarkdownRender`
 *
 * Gets [property@Bge.MarkdownRender:spacing].
 *
 * Returns: the value of the property
 */
int
bge_markdown_render_get_spacing (BgeMarkdownRender *self)
{
  g_return_val_if_fail (BGE_IS_MARKDOWN_RENDER (self), 0);
  return self->spacing;
}

/**
 * bge_markdown_render_set_spacing:
 * @self: a `BgeMarkdownRender`
 * @spacing: spacing in pixels
 *
 * Sets [property@Bge.MarkdownRender:spacing].
 */
void
bge_markdown_render_set_spacing (BgeMarkdownRender *self,
                                 int                spacing)
{
  g_return_if_fail (BGE_IS_MARKDOWN_RENDER (self));
  g_return_if_fail (spacing >= 0);

  if (spacing == self->spacing)
    return;

  self->spacing = spacing;
  gtk_box_set_spacing (GTK_BOX (self->box), spacing);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);
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

  ctx.self            = self;
  ctx.box             = GTK_BOX (self->box);
  ctx.box_children    = self->box_children;
  ctx.beginning       = self->markdown;
  ctx.markup          = NULL;
  ctx.block_stack     = g_array_new (FALSE, TRUE, sizeof (int));
  ctx.indent          = 0;
  ctx.list_index      = 0;
  ctx.list_prefix     = '\0';
  ctx.source_views    = self->source_views;
  ctx.container_stack = g_ptr_array_new ();

  iresult = md_parse (
      self->markdown,
      strlen (self->markdown),
      &parser,
      &ctx);

  if (ctx.markup != NULL)
    g_string_free (ctx.markup, TRUE);
  if (ctx.alt_text != NULL)
    g_string_free (ctx.alt_text, TRUE);
  if (ctx.grid != NULL)
    g_object_unref (ctx.grid);
  g_array_unref (ctx.block_stack);
  g_ptr_array_unref (ctx.container_stack);

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
      int current_type = 0;

      current_type = g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1);
      terminate_block (current_type, NULL, user_data);
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
  else if (type == MD_BLOCK_LI)
    {
      GtkWidget *prefix      = NULL;
      GtkWidget *row         = NULL;
      GtkWidget *content_box = NULL;
      int        parent      = -1;

      if (ctx->block_stack->len > 0)
        parent = g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1);

      if (parent == MD_BLOCK_OL)
        {
          g_autofree char *prefix_text = NULL;

          prefix_text = g_strdup_printf ("%d%c", ctx->list_index, ctx->list_prefix);
          prefix      = gtk_label_new (prefix_text);
          gtk_widget_add_css_class (prefix, "caption");
        }
      else
        {
          prefix = gtk_image_new_from_icon_name ("circle-filled-symbolic");
          gtk_image_set_pixel_size (GTK_IMAGE (prefix), 6);
          gtk_widget_set_margin_top (prefix, 6);
        }
      gtk_widget_add_css_class (prefix, "dimmed");
      gtk_widget_set_valign (prefix, GTK_ALIGN_START);

      content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
      gtk_widget_set_hexpand (content_box, TRUE);

      row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_widget_set_margin_start (row, 10);
      gtk_box_append (GTK_BOX (row), prefix);
      gtk_box_append (GTK_BOX (row), content_box);

      gtk_box_append (current_container (ctx), row);
      g_ptr_array_add (ctx->box_children, row);

      g_ptr_array_add (ctx->container_stack, content_box);

      ctx->list_index++;

      ctx->markup = g_string_new (NULL);
    }
  else if (type == MD_BLOCK_TABLE)
    {
      MD_BLOCK_TABLE_DETAIL *table_detail = detail;
      g_assert (ctx->grid == NULL);
      ctx->grid = gtk_grid_new ();
      gtk_widget_add_css_class (ctx->grid, "markdown-table");
      gtk_widget_set_halign (ctx->grid, GTK_ALIGN_START);
      ctx->grid_rows       = table_detail->head_row_count + table_detail->body_row_count;
      ctx->grid_columns    = table_detail->col_count;
      ctx->grid_row_idx    = 0;
      ctx->grid_column_idx = 0;
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

  if (type == MD_BLOCK_LI)
    {
      if (ctx->markup != NULL)
        terminate_block (type, detail, user_data);

      if (ctx->container_stack->len > 0)
        g_ptr_array_set_size (ctx->container_stack, ctx->container_stack->len - 1);
    }
  else
    {
      if (g_array_index (ctx->block_stack, int, ctx->block_stack->len - 1) >= 0)
        terminate_block (type, detail, user_data);
    }

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
      g_string_append (ctx->markup, "<i>");
      break;
    case MD_SPAN_STRONG:
      g_string_append (ctx->markup, "<b>");
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
      ctx->in_image = TRUE;
      ctx->alt_text = g_string_new (NULL);
      break;
    case MD_SPAN_CODE:
      g_string_append (ctx->markup, "<span font_family=\"monospace\" size=\"small\" background=\"#808080\" bgalpha=\"20%\">");
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
      g_string_append (ctx->markup, "</i>");
      break;
    case MD_SPAN_STRONG:
      g_string_append (ctx->markup, "</b>");
      break;
    case MD_SPAN_A:
      g_string_append (ctx->markup, "</a>");
      break;
    case MD_SPAN_IMG:
      {
        MD_SPAN_IMG_DETAIL *img_detail = detail;
        g_autofree char    *title      = NULL;
        g_autofree char    *src        = NULL;
        g_autofree char    *alt        = NULL;
        GtkWidget          *widget     = NULL;

        if (img_detail->title.text != NULL)
          title = g_strndup (img_detail->title.text, img_detail->title.size);
        if (img_detail->src.text != NULL)
          src = g_strndup (img_detail->src.text, img_detail->src.size);

        if (ctx->alt_text != NULL)
          {
            alt           = g_string_free (ctx->alt_text, FALSE);
            ctx->alt_text = NULL;
          }
        ctx->in_image = FALSE;

        g_signal_emit (ctx->self, signals[SIGNAL_BIND_INLINE_URI], 0,
                       title != NULL ? title : alt, src, &widget);

        if (widget != NULL)
          {
            gtk_box_append (current_container (ctx), widget);
            g_ptr_array_add (ctx->box_children, widget);
          }
      }
      break;
    case MD_SPAN_CODE:
      g_string_append (ctx->markup, "</span>");
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

  if (ctx->in_image)
    {
      g_string_append_len (ctx->alt_text, buf, size);
      return 0;
    }

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
  ParseCtx  *ctx   = user_data;
  GtkWidget *child = NULL;

  g_assert (ctx->block_stack->len > 0);

  if (ctx->markup != NULL && ctx->markup->len > 0)
    {
      const char *end  = ctx->markup->str + ctx->markup->len;
      const char *prev = g_utf8_find_prev_char (ctx->markup->str, end);

      if (prev != NULL)
        {
          gunichar last_char = g_utf8_get_char (prev);

          if (!g_unichar_isgraph (last_char))
            g_string_truncate (ctx->markup, prev - ctx->markup->str);
        }
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
        if (ctx->markup == NULL)
          ctx->indent--;
      }
      break;
    case MD_BLOCK_OL:
      {
        if (ctx->markup == NULL)
          ctx->indent--;
      }
      break;
    case MD_BLOCK_LI:
      {
        if (ctx->markup != NULL && ctx->markup->len > 0)
          {
            GtkWidget *label = gtk_label_new (ctx->markup->str);
            SET_DEFAULTS (label);
            gtk_widget_add_css_class (label, "body");
            gtk_box_append (current_container (ctx), label);
            g_ptr_array_add (ctx->box_children, label);
          }
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
        gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), 16);
        gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), 16);
        gtk_text_view_set_top_margin (GTK_TEXT_VIEW (view), 12);
        gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (view), 12);
        gtk_widget_add_css_class (view, "monospace");

        window = gtk_scrolled_window_new ();
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window), view);

        child = gtk_frame_new (NULL);
        gtk_frame_set_child (GTK_FRAME (child), window);
        gtk_widget_add_css_class (child, "code-block");
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

    case MD_BLOCK_TABLE:
      break;

    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_TR:
      break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
      {
        GtkWidget *label = NULL;

        g_assert (ctx->markup != NULL);

        label = gtk_label_new (ctx->markup->str);
        SET_DEFAULTS (label);
        gtk_label_set_wrap (GTK_LABEL (label), ctx->self->wrap_tables);

        if (type == MD_BLOCK_TH)
          gtk_widget_add_css_class (label, "heading");

        if (ctx->grid_row_idx % 2 == 1)
          gtk_widget_add_css_class (label, "row-odd");

        gtk_grid_attach (
            GTK_GRID (ctx->grid),
            label,
            ctx->grid_column_idx * 2, ctx->grid_row_idx,
            1, 1);

        ctx->grid_column_idx++;
        if (ctx->grid_column_idx >= ctx->grid_columns)
          {
            ctx->grid_row_idx++;
            ctx->grid_column_idx = 0;

            if (ctx->grid_row_idx >= ctx->grid_rows)
              {
                GtkWidget *grid = g_steal_pointer (&ctx->grid);

                if (!ctx->self->wrap_tables)
                  {
                    GtkWidget *scrolled = NULL;
                    scrolled            = gtk_scrolled_window_new ();

                    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
                    gtk_scrolled_window_set_propagate_natural_height (
                        GTK_SCROLLED_WINDOW (scrolled), TRUE);
                    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), grid);
                    child = scrolled;
                  }
                else
                  child = grid;
              }
          }
      }
      break;

    case MD_BLOCK_HTML:
    default:
      g_warning ("Unsupported markdown event (Did you use html?)");
      return 1;
    }

#undef SET_DEFAULTS

  if (child != NULL)
    {
      gtk_box_append (current_container (ctx), child);
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
