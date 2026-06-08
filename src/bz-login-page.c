/* bz-login-page.c
 *
 * Copyright 2025 Alexander Vanhee
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

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <webkit/webkit.h>

#include "bz-auth-state.h"
#include "bz-flathub-auth-provider.h"
#include "bz-global-net.h"
#include "bz-login-page.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"

struct _BzLoginPage
{
  AdwNavigationPage parent_instance;

  BzAuthState *auth_state;

  WebKitWebView *webview;
  gboolean       webkit_loaded;

  SoupSession   *session;
  SoupCookieJar *cookie_jar;

  GList                 *providers;
  BzFlathubAuthProvider *current_provider;
  char                  *auth_redirect_url;
  char                  *session_cookie;
  GDateTime             *session_cookie_expires;
  gboolean               oauth_completed;

  GtkStack            *main_stack;
  AdwStatusPage       *error_status_page;
  AdwPreferencesGroup *provider_preferences_group;
  GtkScrolledWindow   *browser_scroll;
};

G_DEFINE_FINAL_TYPE (BzLoginPage, bz_login_page, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,
  PROP_AUTH_STATE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static WebKitNetworkSession *
get_shared_network_session (void)
{
  static WebKitNetworkSession *shared_session = NULL;

  if (g_once_init_enter (&shared_session))
    {
      g_autofree char *data_dir                = NULL;
      g_autoptr (WebKitNetworkSession) session = NULL;

      data_dir = g_build_filename (g_get_user_data_dir (),
                                   "io.github.kolunmi.Bazaar",
                                   "webkit-data",
                                   NULL);
      session  = webkit_network_session_new (data_dir, NULL);

      g_once_init_leave (&shared_session, g_steal_pointer (&session));
    }

  return shared_session;
}

static void
show_error_take (BzLoginPage *self,
                 char        *message)
{
  g_autofree char *escaped_message = NULL;

  gtk_stack_set_visible_child_name (self->main_stack, "error");

  escaped_message = g_markup_escape_text (message, -1);
  adw_status_page_set_description (self->error_status_page, escaped_message);

  g_free (message);
}

static JsonObject *
parse_json_response (GBytes  *bytes,
                     GError **error)
{
  gboolean result               = FALSE;
  g_autoptr (JsonParser) parser = NULL;
  JsonNode *root                = NULL;

  parser = json_parser_new ();

  result = json_parser_load_from_data (
      parser,
      g_bytes_get_data (bytes, NULL),
      g_bytes_get_size (bytes),
      error);
  if (!result)
    return NULL;

  root = json_parser_get_root (parser);
  return json_node_dup_object (root);
}

static SoupMessage *
create_flathub_request (const char *method,
                        const char *route)
{
  g_autofree char *url        = NULL;
  g_autoptr (SoupMessage) msg = NULL;

  url = g_strdup_printf ("https://flathub.org/api/v2%s", route);
  msg = soup_message_new (method, url);

  soup_message_headers_append (soup_message_get_request_headers (msg),
                               "accept", "application/json");
  return g_steal_pointer (&msg);
}

static void
load_webkit_library (BzLoginPage *self)
{
  WebKitNetworkSession *network_session = NULL;

  if (self->webkit_loaded)
    return;

  network_session = get_shared_network_session ();

  self->webview = g_object_new (
      WEBKIT_TYPE_WEB_VIEW,
      "network-session", network_session,
      NULL);

  gtk_scrolled_window_set_child (
      self->browser_scroll,
      GTK_WIDGET (self->webview));

  self->webkit_loaded = TRUE;
}

static void
on_user_info_loaded (GObject      *source_object,
                     GAsyncResult *res,
                     GWeakRef     *wr);

static void
on_oauth_complete (GObject      *source_object,
                   GAsyncResult *res,
                   GWeakRef     *wr);

static void
complete_oauth (BzLoginPage *self,
                const char  *code,
                const char  *state,
                const char  *error)
{
  g_autoptr (JsonBuilder) builder     = NULL;
  g_autoptr (JsonGenerator) generator = NULL;
  g_autofree char *route              = NULL;
  g_autofree char *json_data          = NULL;
  g_autoptr (SoupMessage) msg         = NULL;

  gtk_stack_set_visible_child_name (self->main_stack, "loading");

  builder = json_builder_new ();
  json_builder_begin_object (builder);
  if (error != NULL)
    {
      json_builder_set_member_name (builder, "state");
      json_builder_add_string_value (builder, state);
      json_builder_set_member_name (builder, "error");
      json_builder_add_string_value (builder, error);
    }
  else
    {
      json_builder_set_member_name (builder, "code");
      json_builder_add_string_value (builder, code);
      json_builder_set_member_name (builder, "state");
      json_builder_add_string_value (builder, state);
    }
  json_builder_end_object (builder);

  generator = json_generator_new ();
  json_generator_set_root (generator, json_builder_get_root (builder));
  json_data = json_generator_to_data (generator, NULL);

  route = g_strdup_printf ("/auth/login/%s",
                           bz_flathub_auth_provider_get_method (self->current_provider));

  msg = soup_message_new ("POST", g_strdup_printf ("https://flathub.org/api/v2%s", route));
  soup_message_headers_append (soup_message_get_request_headers (msg),
                               "accept", "application/json");
  soup_message_headers_append (soup_message_get_request_headers (msg),
                               "Content-Type", "application/json");

  soup_message_set_request_body_from_bytes (msg, "application/json",
                                            g_bytes_new (json_data, strlen (json_data)));

  soup_session_send_and_read_async (
      self->session, msg, G_PRIORITY_DEFAULT,
      NULL, (GAsyncReadyCallback) on_oauth_complete,
      bz_track_weak (self));
}

static gboolean
on_decide_policy (BzLoginPage             *self,
                  WebKitPolicyDecision    *decision,
                  WebKitPolicyDecisionType decision_type,
                  WebKitWebView           *webview)
{
  WebKitNavigationAction *nav_action = NULL;
  WebKitURIRequest       *request    = NULL;
  const char             *uri        = NULL;
  g_autoptr (GUri) parsed_uri        = NULL;
  g_autoptr (GHashTable) params      = NULL;
  const char *code                   = NULL;
  const char *state                  = NULL;
  const char *error                  = NULL;

  if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
    return FALSE;

  nav_action = webkit_navigation_policy_decision_get_navigation_action (
      WEBKIT_NAVIGATION_POLICY_DECISION (decision));
  request = webkit_navigation_action_get_request (nav_action);
  uri     = webkit_uri_request_get_uri (request);

  if (uri == NULL || strstr (uri, "flathub.org") == NULL || strstr (uri, "/login/") == NULL)
    return FALSE;

  parsed_uri = g_uri_parse (uri, G_URI_FLAGS_NONE, NULL);
  if (parsed_uri == NULL)
    return FALSE;

  params = g_uri_parse_params (g_uri_get_query (parsed_uri), -1, "&",
                               G_URI_PARAMS_NONE, NULL);
  if (params == NULL)
    return FALSE;

  code  = g_hash_table_lookup (params, "code");
  state = g_hash_table_lookup (params, "state");
  error = g_hash_table_lookup (params, "error");

  if ((code != NULL && state != NULL && !self->oauth_completed) ||
      (error != NULL && state != NULL && !self->oauth_completed))
    {
      self->oauth_completed = TRUE;
      webkit_policy_decision_ignore (decision);
      complete_oauth (self, code, state, error);
      return TRUE;
    }

  return FALSE;
}

static void
get_user_info (BzLoginPage *self)
{
  g_autoptr (SoupMessage) msg = NULL;

  msg = create_flathub_request ("GET", "/auth/userinfo");
  soup_session_send_and_read_async (
      self->session, msg, G_PRIORITY_DEFAULT,
      NULL, (GAsyncReadyCallback) on_user_info_loaded,
      bz_track_weak (self));
}

static void
on_oauth_complete (GObject      *source_object,
                   GAsyncResult *res,
                   GWeakRef     *wr)
{
  g_autoptr (BzLoginPage) self     = NULL;
  g_autoptr (GError) local_error   = NULL;
  g_autoptr (GBytes) bytes         = NULL;
  g_autoptr (JsonObject) obj       = NULL;
  const char *status               = NULL;
  g_autoslist (SoupCookie) cookies = NULL;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  bytes = soup_session_send_and_read_finish (
      SOUP_SESSION (source_object), res, &local_error);
  if (bytes == NULL)
    {
      g_warning ("OAuth complete error: %s", local_error->message);
      show_error_take (self, g_strdup_printf ("Error: %s", local_error->message));
      goto done;
    }

  obj = parse_json_response (bytes, &local_error);
  if (obj == NULL)
    {
      g_warning ("Failed to parse OAuth response: %s", local_error->message);
      show_error_take (self, g_strdup_printf ("Error: %s", local_error->message));
      goto done;
    }

  status = json_object_get_string_member (obj, "status");

  cookies = soup_cookie_jar_all_cookies (self->cookie_jar);
  for (GSList *l = cookies; l != NULL; l = l->next)
    {
      SoupCookie *cookie = l->data;

      if (g_strcmp0 (soup_cookie_get_name (cookie), "session") == 0)
        {
          g_clear_pointer (&self->session_cookie, g_free);
          self->session_cookie = g_strdup (soup_cookie_get_value (cookie));
          g_clear_pointer (&self->session_cookie_expires, g_date_time_unref);
          self->session_cookie_expires = g_date_time_ref (soup_cookie_get_expires (cookie));
        }
    }

  if (g_strcmp0 (status, "ok") == 0 || g_strcmp0 (status, "success") == 0)
    get_user_info (self);
  else
    show_error_take (self, g_strdup ("Authentication failed"));

done:
  bz_weak_release (wr);
}

static void
on_user_info_loaded (GObject      *source_object,
                     GAsyncResult *res,
                     GWeakRef     *wr)
{
  g_autoptr (BzLoginPage) self = NULL;
  g_autoptr (GBytes) bytes     = NULL;
  g_autoptr (GError) error     = NULL;
  g_autoptr (JsonObject) obj   = NULL;
  JsonObject *default_account  = NULL;
  const char *displayname      = NULL;
  const char *avatar_url       = NULL;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  if (self->webview != NULL)
    webkit_web_view_load_uri (self->webview, "about:blank");

  bytes = soup_session_send_and_read_finish (SOUP_SESSION (source_object), res, &error);
  if (error != NULL)
    {
      g_warning ("User info load error: %s", error->message);
      show_error_take (self, g_strdup_printf ("Error: %s", error->message));
      goto done;
    }

  obj = parse_json_response (bytes, &error);
  if (obj == NULL)
    {
      g_warning ("Failed to parse user info: %s", error->message);
      show_error_take (self, g_strdup_printf ("Error: %s", error->message));
      goto done;
    }

  displayname = json_object_get_string_member (obj, "displayname");

  if (json_object_has_member (obj, "default_account"))
    {
      default_account = json_object_get_object_member (obj, "default_account");

      if (displayname == NULL && json_object_has_member (default_account, "login"))
        displayname = json_object_get_string_member (default_account, "login");

      if (json_object_has_member (default_account, "avatar"))
        avatar_url = json_object_get_string_member (default_account, "avatar");
    }

  if (displayname == NULL)
    displayname = "N/A";

  if (self->auth_state != NULL)
    bz_auth_state_set_authenticated (self->auth_state,
                                     displayname,
                                     self->session_cookie,
                                     self->session_cookie_expires,
                                     avatar_url);

  gtk_stack_set_visible_child_name (self->main_stack, "finish");

done:
  bz_weak_release (wr);
}

static void
on_login_response (GObject      *source_object,
                   GAsyncResult *res,
                   GWeakRef     *wr)
{
  g_autoptr (BzLoginPage) self   = NULL;
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GBytes) bytes       = NULL;
  g_autoptr (JsonObject) obj     = NULL;
  const char *redirect           = NULL;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  bytes = soup_session_send_and_read_finish (
      SOUP_SESSION (source_object), res, &local_error);
  if (bytes == NULL)
    {
      show_error_take (self, g_strdup_printf ("Error: %s", local_error->message));
      goto done;
    }

  obj = parse_json_response (bytes, &local_error);
  if (obj == NULL)
    {
      show_error_take (self, g_strdup_printf ("Error: %s", local_error->message));
      goto done;
    }

  redirect = json_object_get_string_member (obj, "redirect");
  if (redirect == NULL)
    {
      show_error_take (self, g_strdup ("Error: No redirect URL received"));
      goto done;
    }

  g_clear_pointer (&self->auth_redirect_url, g_free);
  self->auth_redirect_url = g_strdup (redirect);

  load_webkit_library (self);

  g_signal_connect_swapped (
      self->webview, "decide-policy",
      G_CALLBACK (on_decide_policy), self);

  gtk_stack_set_visible_child_name (self->main_stack, "browser");
  webkit_web_view_load_uri (self->webview, self->auth_redirect_url);

done:
  bz_weak_release (wr);
}

static void
on_provider_row_activated (BzLoginPage *self,
                           GtkButton   *button)
{
  BzFlathubAuthProvider *provider = NULL;
  g_autoptr (SoupMessage) msg     = NULL;
  g_autofree char *route          = NULL;

  provider = g_object_get_data (G_OBJECT (button), "provider");
  if (provider == NULL)
    return;

  self->current_provider = provider;
  self->oauth_completed  = FALSE;

  gtk_stack_set_visible_child_name (self->main_stack, "loading");

  route = g_strdup_printf ("/auth/login/%s",
                           bz_flathub_auth_provider_get_method (provider));
  msg   = create_flathub_request ("GET", route);

  soup_session_send_and_read_async (
      self->session, msg, G_PRIORITY_DEFAULT,
      NULL, (GAsyncReadyCallback) on_login_response,
      bz_track_weak (self));
}

static void
on_providers_loaded (GObject      *source_object,
                     GAsyncResult *res,
                     GWeakRef     *wr)
{
  g_autoptr (BzLoginPage) self   = NULL;
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GBytes) bytes       = NULL;
  g_autoptr (JsonParser) parser  = NULL;
  JsonNode  *root;
  JsonArray *array;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  bytes = soup_session_send_and_read_finish (SOUP_SESSION (source_object), res, &local_error);
  if (bytes == NULL)
    {
      show_error_take (self, g_strdup_printf ("Error loading providers: %s", local_error->message));
      goto done;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   g_bytes_get_data (bytes, NULL),
                                   g_bytes_get_size (bytes),
                                   &local_error))
    {
      show_error_take (self, g_strdup_printf ("Error parsing providers: %s", local_error->message));
      goto done;
    }

  root  = json_parser_get_root (parser);
  array = json_node_get_array (root);

  for (guint i = 0; i < json_array_get_length (array); i++)
    {
      JsonObject *provider_obj                   = NULL;
      g_autoptr (BzFlathubAuthProvider) provider = NULL;
      const char      *method                    = NULL;
      const char      *name                      = NULL;
      GtkWidget       *row                       = NULL;
      GtkWidget       *prefix_icon               = NULL;
      GtkWidget       *suffix_icon               = NULL;
      g_autofree char *icon_name                 = NULL;

      provider_obj = json_array_get_object_element (array, i);
      provider     = bz_flathub_auth_provider_new ();
      method       = json_object_get_string_member (provider_obj, "method");
      name         = json_object_get_string_member (provider_obj, "name");
      row          = adw_action_row_new ();
      icon_name    = g_strdup_printf ("io.github.kolunmi.Bazaar.%s", method);

      bz_flathub_auth_provider_set_name (provider, name);
      bz_flathub_auth_provider_set_method (provider, method);

      self->providers = g_list_append (self->providers, g_object_ref (provider));

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);

      prefix_icon = gtk_image_new_from_icon_name (icon_name);
      gtk_image_set_icon_size (GTK_IMAGE (prefix_icon), GTK_ICON_SIZE_LARGE);
      gtk_widget_add_css_class (prefix_icon, "lowres-icon");
      adw_action_row_add_prefix (ADW_ACTION_ROW (row), prefix_icon);

      suffix_icon = gtk_image_new_from_icon_name ("go-next-symbolic");
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), suffix_icon);

      g_object_set_data_full (G_OBJECT (row), "provider", g_object_ref (provider), g_object_unref);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), true);
      g_signal_connect_swapped (row, "activated", G_CALLBACK (on_provider_row_activated), self);

      adw_preferences_group_add (self->provider_preferences_group, row);
    }

  gtk_stack_set_visible_child_name (self->main_stack, "start");

done:
  bz_weak_release (wr);
}

static void
load_providers (BzLoginPage *self)
{
  g_autoptr (SoupMessage) msg = NULL;

  gtk_stack_set_visible_child_name (self->main_stack, "loading");

  msg = create_flathub_request ("GET", "/auth/login");
  soup_session_send_and_read_async (
      self->session, msg, G_PRIORITY_DEFAULT,
      NULL, (GAsyncReadyCallback) on_providers_loaded,
      bz_track_weak (self));
}

static void
on_close_clicked (GtkButton   *button,
                  BzLoginPage *self)
{
  GtkWidget *navigation_view = NULL;

  navigation_view = gtk_widget_get_ancestor (GTK_WIDGET (self),
                                             ADW_TYPE_NAVIGATION_VIEW);
  if (navigation_view != NULL)
    adw_navigation_view_pop (ADW_NAVIGATION_VIEW (navigation_view));
}

static void
bz_login_page_dispose (GObject *object)
{
  BzLoginPage *self = BZ_LOGIN_PAGE (object);

  g_clear_object (&self->auth_state);
  g_clear_object (&self->session);
  g_clear_object (&self->cookie_jar);
  g_clear_pointer (&self->session_cookie_expires, g_date_time_unref);
  g_clear_pointer (&self->auth_redirect_url, g_free);
  g_clear_pointer (&self->session_cookie, g_free);

  if (self->providers != NULL)
    {
      g_list_free_full (self->providers, (GDestroyNotify) g_object_unref);
      self->providers = NULL;
    }

  G_OBJECT_CLASS (bz_login_page_parent_class)->dispose (object);
}

static void
bz_login_page_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzLoginPage *self = BZ_LOGIN_PAGE (object);

  switch (prop_id)
    {
    case PROP_AUTH_STATE:
      g_value_set_object (value, self->auth_state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_login_page_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BzLoginPage *self = BZ_LOGIN_PAGE (object);

  switch (prop_id)
    {
    case PROP_AUTH_STATE:
      g_set_object (&self->auth_state, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
format_greeting (gpointer    object,
                 const char *name)
{
  if (name == NULL || name[0] == '\0')
    return g_strdup ("  ");
  return g_strdup_printf (_ ("Hello, %s!"), name);
}

static void
bz_login_page_class_init (BzLoginPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_login_page_dispose;
  object_class->get_property = bz_login_page_get_property;
  object_class->set_property = bz_login_page_set_property;

  properties[PROP_AUTH_STATE] =
      g_param_spec_object (
          "auth-state",
          NULL, NULL,
          BZ_TYPE_AUTH_STATE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/github/kolunmi/Bazaar/bz-login-page.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzLoginPage, main_stack);
  gtk_widget_class_bind_template_child (widget_class, BzLoginPage, error_status_page);
  gtk_widget_class_bind_template_child (widget_class, BzLoginPage, provider_preferences_group);
  gtk_widget_class_bind_template_child (widget_class, BzLoginPage, browser_scroll);

  gtk_widget_class_bind_template_callback (widget_class, on_close_clicked);
  gtk_widget_class_bind_template_callback (widget_class, format_greeting);
}

static void
bz_login_page_init (BzLoginPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->session = soup_session_new ();
  soup_session_set_proxy_resolver (self->session, bz_get_default_proxy_resolver ());

  self->cookie_jar = soup_cookie_jar_new ();
  soup_cookie_jar_set_accept_policy (self->cookie_jar, SOUP_COOKIE_JAR_ACCEPT_ALWAYS);
  soup_session_add_feature (self->session, SOUP_SESSION_FEATURE (self->cookie_jar));

  self->webkit_loaded   = FALSE;
  self->oauth_completed = FALSE;

  load_providers (self);
}

AdwNavigationPage *
bz_login_page_new (BzAuthState *auth_state)
{
  return g_object_new (BZ_TYPE_LOGIN_PAGE,
                       "auth-state", auth_state,
                       NULL);
}
