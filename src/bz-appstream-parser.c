/* bz-appstream-parser.c */

#define G_LOG_DOMAIN  "BAZAAR::APPSTREAM-PARSER"
#define BAZAAR_MODULE "appstream-parser"

#include "config.h"

#include <glib/gi18n.h>

#include "bz-appstream-parser.h"
#include "bz-async-texture.h"
#include "bz-category-flags.h"
#include "bz-flatpak-entry.h"
#include "bz-io.h"
#include "bz-release.h"
#include "bz-url.h"
#include "bz-util.h"
#include "bz-verification-status.h"

static guint
parse_control_value (const char *value)
{
  if (g_strcmp0 (value, "pointing") == 0)
    return BZ_CONTROL_POINTING;
  else if (g_strcmp0 (value, "keyboard") == 0)
    return BZ_CONTROL_KEYBOARD;
  else if (g_strcmp0 (value, "console") == 0)
    return BZ_CONTROL_CONSOLE;
  else if (g_strcmp0 (value, "tablet") == 0)
    return BZ_CONTROL_TABLET;
  else if (g_strcmp0 (value, "touch") == 0)
    return BZ_CONTROL_TOUCH;
  else if (g_strcmp0 (value, "gamepad") == 0)
    return BZ_CONTROL_GAMEPAD;
  else if (g_strcmp0 (value, "tv-remote") == 0)
    return BZ_CONTROL_TV_REMOTE;
  else if (g_strcmp0 (value, "voice") == 0)
    return BZ_CONTROL_VOICE;
  else if (g_strcmp0 (value, "vision") == 0)
    return BZ_CONTROL_VISION;
  else
    return 0;
}

static gboolean
calculate_is_mobile_friendly (guint required_controls,
                              guint recommended_controls,
                              guint supported_controls)
{
  return (required_controls & BZ_CONTROL_TOUCH) != 0 ||
         (recommended_controls & BZ_CONTROL_TOUCH) != 0 ||
         (supported_controls & BZ_CONTROL_TOUCH) != 0;
}

static char *
proxy_screenshot_url (const char *url, gboolean high_quality)
{
  g_autofree char *src         = NULL;
  g_autofree char *encoded_url = NULL;
  const char      *suffix      = NULL;

  if (g_str_has_prefix (url, "https://dl.flathub.org/repo/screenshots/"))
    {
      suffix = url + strlen ("https://dl.flathub.org/repo/screenshots/");
      src    = g_strdup_printf ("https://dl.flathub.org/media/%s", suffix);
    }
  else if (g_str_has_prefix (url, "https://dl.flathub.org/"))
    {
      src = g_strdup (url);
    }
  else
    {
      return g_strdup (url);
    }

  encoded_url = g_base64_encode ((const guchar *) src, strlen (src));
  g_strdelimit (encoded_url, "=", '\0');

  for (char *p = encoded_url; *p; p++)
    {
      if (*p == '+')
        *p = '-';
      if (*p == '/')
        *p = '_';
    }

  return g_strdup_printf (
      "https://imgproxy.flathub.org/insecure/%s/%s",
      high_quality ? "q:90/f:avif" : "dpr:1/f:avif/rs:fill-down",
      encoded_url);
}

static GdkPaintable *
find_screenshot (GPtrArray  *images,
                 const char *caption,
                 gboolean    match_highest,
                 guint       target_width,
                 guint       target_height,
                 gboolean    require_flathub,
                 const char *module_dir,
                 const char *unique_id_checksum,
                 const char *cache_filename,
                 char      **out_caption)
{
  const char *best_url      = NULL;
  gint        best_diff     = G_MAXINT;
  guint       best_res      = 0;
  guint       target_pixels = target_width * target_height;

  if (images == NULL)
    return NULL;

  for (guint j = 0; j < images->len; j++)
    {
      AsImage    *image_obj = g_ptr_array_index (images, j);
      const char *url       = as_image_get_url (image_obj);
      guint       width     = as_image_get_width (image_obj);
      guint       height    = as_image_get_height (image_obj);
      guint       pixels    = width * height;

      if (url == NULL)
        continue;

      if (require_flathub && !g_str_has_prefix (url, "https://dl.flathub.org/"))
        continue;

      if (match_highest)
        {
          if (best_url == NULL || pixels > best_res)
            {
              best_url = url;
              best_res = pixels;
            }
        }
      else
        {
          gint diff = ABS ((gint) pixels - (gint) target_pixels);
          if (best_url == NULL || diff < best_diff)
            {
              best_url  = url;
              best_diff = diff;
            }
        }
    }

  if (best_url != NULL)
    {
      g_autoptr (GFile) screenshot_file = NULL;
      g_autoptr (GFile) cache_file      = NULL;
      g_autofree char *proxied_url      = NULL;
      BzAsyncTexture  *texture          = NULL;

      proxied_url     = proxy_screenshot_url (best_url, match_highest);
      screenshot_file = g_file_new_for_uri (proxied_url);
      cache_file      = g_file_new_build_filename (
          module_dir, unique_id_checksum, cache_filename, NULL);

      texture = bz_async_texture_new_lazy (screenshot_file, cache_file);

      if (out_caption != NULL)
        *out_caption = g_strdup (caption ? caption : "");

      return GDK_PAINTABLE (texture);
    }

  return NULL;
}

gboolean
bz_appstream_parser_populate_entry (BzEntry     *entry,
                                    AsComponent *component,
                                    const char  *appstream_dir,
                                    const char  *remote_name,
                                    const char  *module_dir,
                                    const char  *unique_id_checksum,
                                    const char  *id,
                                    guint        kinds,
                                    GError     **error)
{
  AsDeveloper   *developer_obj                         = NULL;
  GPtrArray     *screenshots                           = NULL;
  AsReleaseList *releases                              = NULL;
  GPtrArray     *releases_arr                          = NULL;
  GPtrArray     *icons                                 = NULL;
  AsBranding    *branding                              = NULL;
  GPtrArray     *requires_relations                    = NULL;
  GPtrArray     *recommends_relations                  = NULL;
  GPtrArray     *supports_relations                    = NULL;
  const char    *title                                 = NULL;
  const char    *description                           = NULL;
  const char    *metadata_license                      = NULL;
  const char    *project_license                       = NULL;
  gboolean       is_floss                              = FALSE;
  const char    *project_group                         = NULL;
  const char    *developer                             = NULL;
  const char    *developer_id                          = NULL;
  const char    *long_description                      = NULL;
  const char    *project_url                           = NULL;
  g_autoptr (GPtrArray) as_search_tokens               = NULL;
  g_autofree char *search_tokens                       = NULL;
  g_autoptr (GdkPaintable) icon_paintable              = NULL;
  g_autoptr (GIcon) mini_icon                          = NULL;
  g_autoptr (GListStore) screenshot_paintables         = NULL;
  g_autoptr (GListStore) screenshot_captions           = NULL;
  g_autoptr (GdkPaintable) thumbnail_paintable         = NULL;
  g_autoptr (GListStore) share_urls                    = NULL;
  g_autofree char *donation_url                        = NULL;
  g_autofree char *forge_url                           = NULL;
  g_autofree char *ratings_summary                     = NULL;
  g_autoptr (GListStore) version_history               = NULL;
  const char *accent_color_light                       = NULL;
  const char *accent_color_dark                        = NULL;
  guint       required_controls                        = 0;
  guint       recommended_controls                     = 0;
  guint       supported_controls                       = 0;
  gint        min_display_length                       = 0;
  gint        max_display_length                       = 0;
  gboolean    is_mobile_friendly                       = FALSE;
  g_autoptr (AsContentRating) content_rating           = NULL;
  GPtrArray *as_keywords                               = NULL;
  g_autoptr (GListStore) keywords                      = NULL;
  GPtrArray      *as_categories                        = NULL;
  BzCategoryFlags categories                           = BZ_CATEGORY_FLAGS_NONE;
  g_autoptr (BzVerificationStatus) verification_status = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (entry), FALSE);
  g_return_val_if_fail (AS_IS_COMPONENT (component), FALSE);

  title = as_component_get_name (component);
  if (title == NULL)
    title = as_component_get_id (component);

  description      = as_component_get_summary (component);
  metadata_license = as_component_get_metadata_license (component);
  project_license  = as_component_get_project_license (component);
  is_floss         = as_component_is_floss (component);
  project_group    = as_component_get_project_group (component);
  project_url      = as_component_get_url (component, AS_URL_KIND_HOMEPAGE);
  as_search_tokens = as_component_get_search_tokens (component);

  developer_obj = as_component_get_developer (component);
  if (developer_obj != NULL)
    {
      developer    = as_developer_get_name (developer_obj);
      developer_id = as_developer_get_id (developer_obj);
    }

  long_description = as_component_get_description (component);

  screenshots = as_component_get_screenshots_all (component);
  if (screenshots != NULL)
    {
      screenshot_paintables = g_list_store_new (BZ_TYPE_ASYNC_TEXTURE);
      screenshot_captions   = g_list_store_new (GTK_TYPE_STRING_OBJECT);

      for (guint i = 0; i < screenshots->len; i++)
        {
          AsScreenshot    *screenshot        = NULL;
          GPtrArray       *images            = NULL;
          const gchar     *caption           = NULL;
          g_autofree char *caption_str       = NULL;
          g_autoptr (GdkPaintable) paintable = NULL;
          g_autofree char *cache_name        = NULL;

          screenshot = g_ptr_array_index (screenshots, i);
          images     = as_screenshot_get_images_all (screenshot);
          caption    = as_screenshot_get_caption (screenshot);

          if (i == 0 && thumbnail_paintable == NULL)
            {
              thumbnail_paintable = find_screenshot (images, caption, FALSE, 400, 300, TRUE,
                                                     module_dir, unique_id_checksum, "thumbnail", NULL);
              if (thumbnail_paintable == NULL)
                thumbnail_paintable = find_screenshot (images, caption, FALSE, 400, 300, FALSE,
                                                       module_dir, unique_id_checksum, "thumbnail", NULL);
            }

          cache_name = g_strdup_printf ("screenshot_%u", i);
          paintable  = find_screenshot (images, caption, TRUE, 0, 0, TRUE,
                                        module_dir, unique_id_checksum, cache_name, &caption_str);
          if (paintable == NULL)
            paintable = find_screenshot (images, caption, TRUE, 0, 0, FALSE,
                                         module_dir, unique_id_checksum, cache_name, &caption_str);

          if (paintable != NULL)
            {
              g_autoptr (GtkStringObject) caption_obj = NULL;

              g_list_store_append (screenshot_paintables, paintable);
              caption_obj = gtk_string_object_new (caption_str);
              g_list_store_append (screenshot_captions, caption_obj);
            }
        }
    }

  share_urls = g_list_store_new (BZ_TYPE_URL);
  if (kinds & BZ_ENTRY_KIND_APPLICATION &&
      g_strcmp0 (remote_name, "flathub") == 0)
    {
      g_autofree char *flathub_url = NULL;
      g_autoptr (BzUrl) url        = NULL;

      flathub_url = g_strdup_printf ("https://flathub.org/apps/%s", id);

      url = bz_url_new ();
      bz_url_set_name (url, C_ ("Project URL Type", "Flathub Page"));
      bz_url_set_url (url, flathub_url);
      bz_url_set_icon_name (url, "flathub-symbolic");

      g_list_store_append (share_urls, url);
    }

  for (int e = AS_URL_KIND_UNKNOWN + 1; e < AS_URL_KIND_LAST; e++)
    {
      const char *url = NULL;

      url = as_component_get_url (component, e);
      if (url != NULL)
        {
          const char *enum_string     = NULL;
          const char *icon_name       = NULL;
          g_autoptr (BzUrl) share_url = NULL;

          switch (e)
            {
            case AS_URL_KIND_HOMEPAGE:
              enum_string = C_ ("Project URL Type", "Project Website");
              icon_name   = "globe-symbolic";
              break;
            case AS_URL_KIND_BUGTRACKER:
              enum_string = C_ ("Project URL Type", "Issue Tracker");
              icon_name   = "computer-fail-symbolic";
              break;
            case AS_URL_KIND_FAQ:
              enum_string = C_ ("Project URL Type", "FAQ");
              icon_name   = "help-faq-symbolic";
              break;
            case AS_URL_KIND_HELP:
              enum_string = C_ ("Project URL Type", "Help");
              icon_name   = "help-browser-symbolic";
              break;
            case AS_URL_KIND_DONATION:
              enum_string = C_ ("Project URL Type", "Donate");
              icon_name   = "heart-filled-symbolic";
              g_clear_pointer (&donation_url, g_free);
              donation_url = g_strdup (url);
              break;
            case AS_URL_KIND_TRANSLATE:
              enum_string = C_ ("Project URL Type", "Translate");
              icon_name   = "translations-symbolic";
              break;
            case AS_URL_KIND_CONTACT:
              enum_string = C_ ("Project URL Type", "Contact");
              icon_name   = "mail-send-symbolic";
              break;
            case AS_URL_KIND_VCS_BROWSER:
              enum_string = C_ ("Project URL Type", "Source Code");
              icon_name   = "code-symbolic";
              g_clear_pointer (&forge_url, g_free);
              forge_url = g_strdup (url);
              break;
            case AS_URL_KIND_CONTRIBUTE:
              enum_string = C_ ("Project URL Type", "Contribute");
              icon_name   = "system-users-symbolic";
              break;
            default:
              break;
            }

          share_url = g_object_new (
              BZ_TYPE_URL,
              "name", enum_string,
              "url", url,
              "icon-name", icon_name,
              NULL);
          g_list_store_append (share_urls, share_url);
        }
    }
  if (g_list_model_get_n_items (G_LIST_MODEL (share_urls)) == 0)
    g_clear_object (&share_urls);

  releases = as_component_load_releases (component, TRUE, error);
  if (releases == NULL)
    return FALSE;
  releases_arr = as_release_list_get_entries (releases);
  if (releases_arr != NULL)
    {
      version_history = g_list_store_new (BZ_TYPE_RELEASE);

      for (guint i = 0; i < releases_arr->len; i++)
        {
          AsRelease  *as_release          = NULL;
          const char *release_description = NULL;
          g_autoptr (BzRelease) release   = NULL;

          as_release = g_ptr_array_index (releases_arr, i);

          release_description = as_release_get_description (as_release);

          release = g_object_new (
              BZ_TYPE_RELEASE,
              "description", release_description,
              "timestamp", as_release_get_timestamp (as_release),
              "url", as_release_get_url (as_release, AS_RELEASE_URL_KIND_DETAILS),
              "version", as_release_get_version (as_release),
              NULL);
          g_list_store_append (version_history, release);
        }
    }

  icons = as_component_get_icons (component);
  if (icons != NULL)
    {
      g_autofree char *select          = NULL;
      gboolean         select_is_local = FALSE;
      int              select_width    = 0;
      int              select_height   = 0;

      for (guint i = 0; i < icons->len; i++)
        {
          AsIcon  *icon     = NULL;
          int      width    = 0;
          int      height   = 0;
          gboolean is_local = FALSE;

          icon     = g_ptr_array_index (icons, i);
          width    = as_icon_get_width (icon);
          height   = as_icon_get_height (icon);
          is_local = as_icon_get_kind (icon) != AS_ICON_KIND_REMOTE;

          if (select == NULL ||
              (is_local && !select_is_local) ||
              (width > select_width && height > select_height))
            {
              if (is_local)
                {
                  const char      *filename   = NULL;
                  g_autofree char *resolution = NULL;
                  g_autofree char *path       = NULL;

                  filename = as_icon_get_filename (icon);
                  if (filename == NULL)
                    continue;

                  resolution = g_strdup_printf ("%dx%d", width, height);
                  path       = g_build_filename (
                      appstream_dir,
                      "icons",
                      "flatpak",
                      resolution,
                      filename,
                      NULL);
                  if (!g_file_test (path, G_FILE_TEST_EXISTS))
                    continue;

                  g_clear_pointer (&select, g_free);
                  select          = g_steal_pointer (&path);
                  select_is_local = TRUE;
                  select_width    = width;
                  select_height   = height;
                }
              else
                {
                  const char *url = NULL;

                  url = as_icon_get_url (icon);
                  if (url == NULL)
                    continue;

                  g_clear_pointer (&select, g_free);
                  select          = g_strdup (url);
                  select_is_local = FALSE;
                  select_width    = width;
                  select_height   = height;
                }
            }
        }

      if (select != NULL)
        {
          g_autofree char *select_uri  = NULL;
          g_autoptr (GFile) source     = NULL;
          g_autoptr (GFile) cache_into = NULL;
          BzAsyncTexture *texture      = NULL;

          if (select_is_local)
            select_uri = g_strdup_printf ("file://%s", select);
          else
            select_uri = g_steal_pointer (&select);

          source     = g_file_new_for_uri (select_uri);
          cache_into = g_file_new_build_filename (
              module_dir, unique_id_checksum, "icon-paintable.png", NULL);

          texture        = bz_async_texture_new_lazy (source, cache_into);
          icon_paintable = GDK_PAINTABLE (texture);

          if (select_is_local)
            mini_icon = bz_load_mini_icon_sync (unique_id_checksum, select);
        }
    }

  branding = as_component_get_branding (component);
  if (branding != NULL)
    {
      accent_color_light = as_branding_get_color (
          branding, AS_COLOR_KIND_PRIMARY, AS_COLOR_SCHEME_KIND_LIGHT);
      accent_color_dark = as_branding_get_color (
          branding, AS_COLOR_KIND_PRIMARY, AS_COLOR_SCHEME_KIND_DARK);
    }

  content_rating = as_component_get_content_rating (component, "oars-1.1");
  if (content_rating != NULL)
    {
      g_object_ref (content_rating);
    }
  else
    {
      content_rating = as_component_get_content_rating (component, "oars-1.0");
      if (content_rating != NULL)
        g_object_ref (content_rating);
    }

  requires_relations   = as_component_get_requires (component);
  recommends_relations = as_component_get_recommends (component);
  supports_relations   = as_component_get_supports (component);

  if (requires_relations != NULL)
    {
      for (guint i = 0; i < requires_relations->len; i++)
        {
          AsRelation        *relation  = g_ptr_array_index (requires_relations, i);
          AsRelationItemKind item_kind = as_relation_get_item_kind (relation);

          if (item_kind == AS_RELATION_ITEM_KIND_CONTROL)
            {
              AsControlKind control_kind = as_relation_get_value_control_kind (relation);
              const char   *control_str  = as_control_kind_to_string (control_kind);
              if (control_str != NULL)
                required_controls |= parse_control_value (control_str);
            }
          else if (item_kind == AS_RELATION_ITEM_KIND_DISPLAY_LENGTH)
            {
              AsRelationCompare compare = as_relation_get_compare (relation);
              gint              value   = as_relation_get_value_int (relation);
              if (compare == AS_RELATION_COMPARE_GE)
                min_display_length = value;
            }
        }
    }

  if (recommends_relations != NULL)
    {
      for (guint i = 0; i < recommends_relations->len; i++)
        {
          AsRelation        *relation  = g_ptr_array_index (recommends_relations, i);
          AsRelationItemKind item_kind = as_relation_get_item_kind (relation);

          if (item_kind == AS_RELATION_ITEM_KIND_CONTROL)
            {
              AsControlKind control_kind = as_relation_get_value_control_kind (relation);
              const char   *control_str  = as_control_kind_to_string (control_kind);
              if (control_str != NULL)
                recommended_controls |= parse_control_value (control_str);
            }
        }
    }

  if (supports_relations != NULL)
    {
      for (guint i = 0; i < supports_relations->len; i++)
        {
          AsRelation        *relation  = g_ptr_array_index (supports_relations, i);
          AsRelationItemKind item_kind = as_relation_get_item_kind (relation);

          if (item_kind == AS_RELATION_ITEM_KIND_CONTROL)
            {
              AsControlKind control_kind = as_relation_get_value_control_kind (relation);
              const char   *control_str  = as_control_kind_to_string (control_kind);
              if (control_str != NULL)
                supported_controls |= parse_control_value (control_str);
            }
          else if (item_kind == AS_RELATION_ITEM_KIND_DISPLAY_LENGTH)
            {
              AsRelationCompare compare = as_relation_get_compare (relation);
              gint              value   = as_relation_get_value_int (relation);
              if (compare == AS_RELATION_COMPARE_LE)
                max_display_length = value;
            }
        }
    }

  is_mobile_friendly = calculate_is_mobile_friendly (required_controls,
                                                     recommended_controls,
                                                     supported_controls);

  if (as_search_tokens != NULL)
    {
      g_autoptr (GStrvBuilder) builder = NULL;
      g_auto (GStrv) strv              = NULL;

      builder = g_strv_builder_new ();
      for (guint i = 0; i < as_search_tokens->len; i++)
        {
          const char *token = NULL;

          token = g_ptr_array_index (as_search_tokens, i);
          g_strv_builder_add (builder, token);
        }

      strv          = g_strv_builder_end (builder);
      search_tokens = g_strjoinv (" ", strv);
    }

  as_keywords = as_component_get_keywords (component);
  if (as_keywords != NULL && as_keywords->len > 0)
    {
      keywords = g_list_store_new (GTK_TYPE_STRING_OBJECT);

      for (guint i = 0; i < as_keywords->len; i++)
        {
          const char *keyword                     = NULL;
          g_autoptr (GtkStringObject) keyword_obj = NULL;

          keyword     = g_ptr_array_index (as_keywords, i);
          keyword_obj = gtk_string_object_new (keyword);
          g_list_store_append (keywords, keyword_obj);
        }
    }

  as_categories = as_component_get_categories (component);
  if (as_categories != NULL)
    {
      for (guint i = 0; i < as_categories->len; i++)
        {
          const char *name = NULL;

          name       = g_ptr_array_index (as_categories, i);
          categories = bz_category_flags_add (categories, name);
        }
    }

  if (g_strcmp0 (remote_name, "flathub") == 0)
    {
      const char *verified_str     = NULL;
      const char *method           = NULL;
      const char *website          = NULL;
      const char *login_name       = NULL;
      const char *login_provider   = NULL;
      const char *timestamp        = NULL;
      const char *login_is_org_str = NULL;
      gboolean    verified         = FALSE;
      gboolean    login_is_org     = FALSE;
      GHashTable *custom_fields    = NULL;

      custom_fields = as_component_get_custom (component);

      if (custom_fields != NULL)
        {
          verified_str     = g_hash_table_lookup (custom_fields, "flathub::verification::verified");
          method           = g_hash_table_lookup (custom_fields, "flathub::verification::method");
          website          = g_hash_table_lookup (custom_fields, "flathub::verification::website");
          login_name       = g_hash_table_lookup (custom_fields, "flathub::verification::login_name");
          login_provider   = g_hash_table_lookup (custom_fields, "flathub::verification::login_provider");
          timestamp        = g_hash_table_lookup (custom_fields, "flathub::verification::timestamp");
          login_is_org_str = g_hash_table_lookup (custom_fields, "flathub::verification::login_is_organization");
        }

      verified     = (verified_str != NULL && g_strcmp0 (verified_str, "true") == 0);
      login_is_org = (login_is_org_str != NULL && g_strcmp0 (login_is_org_str, "true") == 0);

      verification_status = bz_verification_status_new ();
      g_object_set (verification_status,
                    "verified", verified,
                    "method", method,
                    "website", website,
                    "login-name", login_name,
                    "login-provider", login_provider,
                    "timestamp", timestamp,
                    "login-is-organization", login_is_org,
                    NULL);
    }

  g_object_set (
      entry,
      "title", title,
      "description", description,
      "long-description", long_description,
      "url", project_url,
      "search-tokens", search_tokens,
      "metadata-license", metadata_license,
      "project-license", project_license,
      "is-floss", is_floss,
      "project-group", project_group,
      "developer", developer,
      "developer-id", developer_id,
      "icon-paintable", icon_paintable,
      "mini-icon", mini_icon,
      "screenshot-paintables", screenshot_paintables,
      "screenshot-captions", screenshot_captions,
      "thumbnail-paintable", thumbnail_paintable,
      "share-urls", share_urls,
      "donation-url", donation_url,
      "forge-url", forge_url,
      "ratings-summary", ratings_summary,
      "version-history", version_history,
      "light-accent-color", accent_color_light,
      "dark-accent-color", accent_color_dark,
      "required-controls", required_controls,
      "recommended-controls", recommended_controls,
      "supported-controls", supported_controls,
      "min-display-length", min_display_length,
      "max-display-length", max_display_length,
      "is-mobile-friendly", is_mobile_friendly,
      "content-rating", content_rating,
      "keywords", keywords,
      "categories", categories,
      "verification-status", verification_status,
      NULL);

  return TRUE;
}

BzEntry *
bz_appstream_parser_entry_from_metainfo (GFile   *metainfo_file,
                                         GFile   *icon_file,
                                         GError **error)
{
  g_autoptr (AsMetadata) mdata  = as_metadata_new ();
  AsComponent     *component    = NULL;
  g_autofree char *xml_contents = NULL;
  g_autofree char *xml_path     = NULL;
  g_autofree char *xml_dir      = NULL;
  g_autofree char *module_dir   = NULL;
  g_autofree char *checksum     = NULL;
  gsize            xml_length   = 0;
  g_autoptr (BzEntry) entry     = NULL;

  g_return_val_if_fail (G_IS_FILE (metainfo_file), NULL);

  xml_path = g_file_get_path (metainfo_file);

  if (!g_file_get_contents (xml_path, &xml_contents, &xml_length, error))
    return NULL;

  as_metadata_parse_data (mdata, xml_contents, xml_length, AS_FORMAT_KIND_XML, error);
  if (*error != NULL)
    return NULL;

  component = as_metadata_get_component (mdata);
  if (component == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No component found in metainfo file");
      return NULL;
    }

  xml_dir    = g_path_get_dirname (xml_path);
  module_dir = bz_dup_module_dir ();
  checksum   = g_compute_checksum_for_string (G_CHECKSUM_MD5, xml_path, -1);

  entry = BZ_ENTRY (g_object_new (BZ_TYPE_FLATPAK_ENTRY, NULL));

  bz_appstream_parser_populate_entry (
      entry, component,
      xml_dir,
      "local-preview",
      module_dir,
      checksum,
      as_component_get_id (component),
      BZ_ENTRY_KIND_APPLICATION,
      NULL);

  g_object_set (entry, "remote-repo-name", "local-preview", NULL);

  if (icon_file != NULL)
    {
      g_autoptr (GFile) cache_into = NULL;

      cache_into = g_file_new_build_filename (
          module_dir, checksum, "icon-paintable.png", NULL);

      g_object_set (
          entry,
          "icon-paintable", GDK_PAINTABLE (bz_async_texture_new_lazy (icon_file, cache_into)),
          NULL);
    }

  return g_steal_pointer (&entry);
}

AsComponent *
bz_parse_component_for_node (XbNode  *node,
                             GError **error)
{
  g_autofree char *component_xml  = NULL;
  g_autoptr (AsMetadata) metadata = NULL;
  AsComponent *component          = NULL;
  gboolean     result             = FALSE;

  component_xml = xb_node_export (node, XB_NODE_EXPORT_FLAG_NONE, error);
  if (component_xml == NULL)
    return NULL;

  metadata = as_metadata_new ();
  result   = as_metadata_parse_data (
      metadata,
      component_xml,
      -1,
      AS_FORMAT_KIND_XML,
      error);
  if (!result)
    return NULL;

  component = as_metadata_get_component (metadata);
  return bz_object_maybe_ref (component);
}

GBytes *
bz_decompress_gz_bytes (GBytes       *compressed,
                        GCancellable *cancellable,
                        GError      **error)
{
  g_autoptr (GZlibDecompressor) decompressor = NULL;
  g_autoptr (GInputStream) mem_stream        = NULL;
  g_autoptr (GInputStream) conv_stream       = NULL;
  g_autoptr (GOutputStream) output           = NULL;
  gssize bytes_spliced                       = 0;

  decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
  mem_stream   = g_memory_input_stream_new_from_bytes (compressed);
  conv_stream  = g_converter_input_stream_new (mem_stream, G_CONVERTER (decompressor));
  output       = g_memory_output_stream_new_resizable ();

  bytes_spliced = g_output_stream_splice (
      output, conv_stream,
      G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
      cancellable,
      error);
  if (bytes_spliced < 0)
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
}
