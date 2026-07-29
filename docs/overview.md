# Bazaar Overview

These docs are intended for distributors of Bazaar to learn how to configure the
application.

## Features
Here is a short list of some of the things Bazaar can do, beyond the basics of
managing software through the GUI:

* Queue an arbitrary number of transactions (a catch-all term for installations,
  updates, and removals) during which you may close all windows or continue to
  append new transactions freely

* Display a "Curated" page which you may extensively customize with YAML.
* This is a way to highlight applications which you believe might be of
  interest.
  * Curated configuration files are constantly monitored for filesystem events,
    changes immediately reflected in the GUI
  * Any number of curated configuration files; they will concatenate in the GUI
    in the order they were provided

* Access data from [Flathub](https://flathub.org/), such as the latest or most
  popular applications, and allow users to sign in to Flathub to manage
  bookmarked applications

* Run in the background and respond to desktop search queries with application
  info, using the same search routine as in-application
  - GNOME will work out of the box, as Bazaar implements the
    `org.gnome.Shell.SearchProvider2` dbus interface
  - KDE Plasma will require a [krunner
    plugin](https://github.com/ublue-os/krunner-bazaar)

* Hide applications you do not want users to see with blocklists, which is
  useful for discouraging the use of certain packages which you deem to be
  broken.

* Manage an arbitrary amount of windows and keep them synchronized

* Communicate with, and invoke operations on, the main daemon through the
  command line

## CLI Usage

You can start the bazaar daemon like this:

```
bazaar-daemon [ARGS] [PACKAGE PATH/URI]
```

`[PACKAGE PATH/URI]` could be a `.flatpakref` file. flatpak+https and regular
https is supported.

To avoid spawning an initial window, use:

```
bazaar-daemon --no-window
```

## Comptime Configuration

The only compile time meson option you should concern yourself with for
production is `hardcoded_main_config_path`. This embeds a path to the main
configuration file into Bazaar. If this is not defined at compile time, Bazaar
will never attempt to read a main config. See the next section of this document
to see an example config file.

## Environment Variables

Bazaar reads the following environment variables, which may be of interest:

* `BAZAAR_DESKTOP_SEARCH_PROVIDER_ICON_SIZE`: may be read as an unsigned integer
from 1 to 256 to specify the square pixel size of icons sent to the desktop
search provider. This should be configured to be a higher value than its default
value of 24x24 when on KDE, as krunner paints result icons as much larger than
gnome-shell does. This way, the images will not appear blurry.

* `BAZAAR_N_DOWNLOAD_WORKERS`: may be read as an unsigned integer greater than 0
to specify the number of image download worker subprocesses that should be
spawned and managed by Bazaar to fetch screenshots, icons, and other images. By
default, Bazaar spawns 8 download workers. Download workers are always killed
when Bazaar has no active windows and ensured when Bazaar returns to having 1 or
more windows.

## Main Configuration

This is the primary YAML configuration file for bazaar, as designated by the
`hardcoded_main_config_path` meson option. Here, you will point bazaar to where
your other configs are located. You can also define hooks in this file. See the
"Hooks" section of this document for an overview of the hooks system and an
example of integration into the main config.

### Example

```yaml
yaml-blocklist-paths:
  - /path/to/yaml/blocklist.yaml
  - /path/to/another/yaml/blocklist.yaml
  # Flatpak path with host-etc permission
  - /run/host/etc/bazaar/blocklist.yaml
txt-blocklist-paths:
  - /path/to/txt/blocklist.txt
  - /path/to/another/txt/blocklist.txt
  # Flatpak path with host-etc permission
  - /run/host/etc/bazaar/blocklist.txt
curated-config-paths:
  - /path/to/yaml/file.yaml
  - /path/to/another/yaml/file.yaml
  # Flatpak path with host-etc permission
  - /run/host/etc/bazaar/curated.yaml
# Make the app start on the curated page instead of the explore page, try to only use if you have an interesting news section
start-on-curated: true
```

### EOL Overrides

In this file, you can also specify a list of apps for which to hide EOL warnings
and therefore prevent users from being deterred from installing them:

```yaml
override-eol-markings:
  - com.obsproject.Studio
  - net.lutris.Lutris
```

(At the time of writing, the projects listed above rely on EOL runtimes but are
otherwise widely used)

## Blocklists

Blocklists are a way to ensure that users will never interact with a certain
application inside Bazaar by searching or browsing, either on the Flathub page
or via search.

Blocked applications which are already installed still appear in
the library or when there are updates available for them and via the system's
flatpak configuration. Under no circumstance does Bazaar touch the underlying
flatpak configuration in order to block or allow apps. The `flatpak` command
line tool is unaffected.

### Blocklist Types

There are two kinds of blocklists supported by Bazaar:

#### YAML Blocklists

For those who are learn more effectively by seeing an example:

```yaml
  blocklists:
    - priority: 0
      block-regex:
        # block all ids matching this regex unconditionally
        - com\.place\..*
    - priority: -1 # lower number = more priority
      conditions:
        - match-locale:
            # only apply this rule if we have the Arabic locale
            regex: ar
      allow:
        - com.place.App3
        - com.place.App5
      allow-regex:
        - com\.place\..*\.ar
    - priority: -1
      conditions:
        - match-locale:
            regex: en.*
          # invert the result of this condition; so all locales which don't match
          # the regex
          post-process: invert
        - match-envvar:
            var: PATH
            regex: .*/usr/local/bin.*
      block:
        - com.other.App1
      allow:
        - com.place.App1
        - com.place.App2
```

YAML blocklists files are comprised of a list of sub "blocklists," which to
Bazaar means a **list of appids to block or allow**. These are optionally
**applied based on a list of provided conditions** and **composed with other
blocklists in the list based on a priority**.

Each blocklist may contain these fields:

* `priority`: a number which tells Bazaar how to layer this blocklist with the
  others, or how "important" this blocklist is. **A lower number means more
  priority**.

* `conditions`: a list of conditions to apply. Two condition types exist at the
  moment: `match-locale` and `match-envvar`. TODO: describe these

* `block`: a list of appids to block. These are matched without regex.

* `block-regex`: a list of appids to block. These allow regex syntax for special matching.

* `allow`: a list of appids to allow. These are matched without regex.

* `allow-regex`: a list of appids to allow. These allow regex syntax for special matching.

#### Basic Newline Separated TXT Blocklists

This type of blocklist is what Bazaar originally supported. They still function,
but it is encouraged to use YAML blocklists if any sort of pattern matching or
conditional inclusion is required.

```
# comments are supported
com.jetbrains.CLion
com.valvesoftware.Steam

# empty lines are allowed

io.neovim.nvim
net.lutris.Lutris
```

Warning: **Incorrect Buzzer** TXT blocklists do not support regular expressions.
This matches the literal text provided

```
com\.place\..*
```

### Blocklist FAQ

#### No Worky

Check that the path of the blocklist exists and that Bazaar can access it. This command is useful for debugging this:

```
flatpak run --command=bash io.github.kolunmi.Bazaar
```

The `/etc` of the host system accessed from a Flatpak requires the `host-etc` permission.

This means `/etc/bazaar/banner.png` turns into `/run/host/etc/bazaar/banner.png`.

#### I want to to block a list of applications all the time, and also another list only on desktop environment X:

Say you want to block Bazaar and Steam unconditionally and Kate and KWrite if
the desktop is Plasma. You can use an environment variable condition to check
the value of `XDG_CURRENT_DESKTOP`:

```yaml
blocklists:
  - block:
      - io.github.kolunmi.Bazaar
      - com.valvesoftware.Steam

  - block:
      - org.kde.kate
      - org.kde.kwrite
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: KDE
```

Let's add another for GNOME:
```yaml
blocklists:
  - block:
      - io.github.kolunmi.Bazaar
      - com.valvesoftware.Steam

  - block:
      - org.kde.kate
      - org.kde.kwrite
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: KDE

  - block:
      - org.gnome.eog
      - org.gnome.Extensions
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: GNOME
```

#### What about blocking an application by default, but allowing it if a condition resolves to true?

Taking from the previous example, let's allow Steam on KDE and GNOME, but block
it on other desktops. We'll need to give the DE-specific blocklists more
priority:

```yaml
blocklists:
  - priority: 1
    block:
      - io.github.kolunmi.Bazaar
      - com.valvesoftware.Steam

  - block:
      - org.kde.kate
      - org.kde.kwrite
    allow:
      - com.valvesoftware.Steam
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: KDE

  - block:
      - org.gnome.eog
      - org.gnome.Extensions
    allow:
      - com.valvesoftware.Steam
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: GNOME
```

You could also make a new blocklist matching both `KDE` and `GNOME`:

```yaml
blocklists:
  - priority: 1
    block:
      - io.github.kolunmi.Bazaar
      - com.valvesoftware.Steam

  - block:
      - org.kde.kate
      - org.kde.kwrite
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: KDE

  - block:
      - org.gnome.eog
      - org.gnome.Extensions
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: GNOME

  - allow:
      - com.valvesoftware.Steam
    conditions:
      - match-envvar:
          var: XDG_CURRENT_DESKTOP
          regex: KDE|GNOME
```

## Curated Section

If Bazaar is provided a non-zero amount of curated configs, an extra tab will
appear on the window's header bar called "Curated". This section is intended for
distributors to curate applications for users with a customizable interface.
Curated configs are YAML files. They are constantly monitored by Bazaar for
filesystem events, so when the config changes, Bazaar will automatically reload
the content.

Right now, curated configs are essentially composed of a vertical list of
elements which appear stacked on top of each other inside of a scrollable
viewport in the order they appear in the YAML. Each element has certain
properties you can customize.

Bazaar maps the appids you provide to the best matching "entry group" from the
table of applications it was able to pull from remote sources (Simply put, an
entry group in Bazaar is a collection of applications which share the same appid
but come from different sources or installations). The entry group has a
designated "ui entry" which was determined to have the most useful content
associated with it as it pertains to presenting things like icons, descriptions,
screenshots, etc to the user, usually from Flathub when available.

When the user selects an app, they are brought to a "full view" where they can
see a bunch of information stored inside or referenced by the ui entry and
choose to invoke transactions on the entry group, like installation or removal.

### Example

An example can be found in docs/example.yaml

### Integrate the curated section + blocklist with the official Flatpak for Administrators/Vendors

For more practical examples check out the configuration from [Bluefin](https://github.com/projectbluefin/common/tree/a868eba107b91c4eae60b6d1d6d2e2cdf05eb1c8/system_files/bluefin/etc/bazaar) and [Aurora](https://github.com/get-aurora-dev/common/tree/0d86028dd0d737d1d0eee08205c33fc91997f155/system_files/shared/etc/bazaar).

Bazaar by default looks for a config file in `/etc/bazaar` or `/run/host/etc/bazaar` inside the sandbox, this is [configured on build time](https://github.com/flathub/io.github.kolunmi.Bazaar/blob/709faccd8c4198c5fdabf20eb4a98db98a5aa1c6/io.github.kolunmi.Bazaar.yaml#L43-L46) This needs permission to `/etc` which can be granted with the `filesystem=host-etc` permission, the build on Flathub doesn't have this permission by default.

This is not super straightforward to setup currently as Flatpak doesn't support overriding permissions in `/etc` or `/usr` yet, so you have to resort to `systemd-tmpfiles` to create this permission override in `/var/lib/flatpak/overrides/io.github.kolunmi.Bazaar`.

Here is how they did it:

- [tmpfiles](https://github.com/get-aurora-dev/common/blob/0d86028dd0d737d1d0eee08205c33fc91997f155/system_files/shared/usr/lib/tmpfiles.d/bazaar-flatpak.conf)

- [actual permission override](https://github.com/get-aurora-dev/common/blob/0d86028dd0d737d1d0eee08205c33fc91997f155/system_files/shared/usr/share/ublue-os/flatpak-overrides/io.github.kolunmi.Bazaar), the filepath for this doesn't really matter, just a way for you to ship the symlink with tmpfiles

## Article markdown

Curated articles support standard markdown, rendered with GTK widgets (so its
not a webview).

**Supported:**
- Basic markdown
- Inline code
- Code blocks (with syntax highlighting if language is specified)
- Ordered and unordered lists
- Tables
- Horizontal rules
- Links

**Not supported**:
- Raw HTML
- Latex
- Other weird things
- Column alignment in tables

### Inline images & app tiles

Regular image syntax (`![title](https://example.com/image.png)`) renders a
normal image/screenshot.

Two special URI schemes can be used instead to render app tiles inline:

* `appstream://<appid>`: looks up a real app by its appid and renders like any
  other app button.
* `bazaar-hook://?id=<id>&title=<title>&subtitle=<subtitle>&icon=<icon-uri>`:
  renders a fake tile that is not backed by a real app. Clicking it runs an
  `article-app` hook instead of opening a full app view.

You can mix multiple tiles of either kind into one row by comma-separating them
inside a single image tag:

```markdown
![](appstream://org.gnome.Calculator,bazaar-hook://?id=neovim&title=Neovim&subtitle=Vim-fork%20focused%20on%20extensibility%20and%20useablitiy&icon=https://gitlab.com/uploads/-/system/project/avatar/56714943/nvim-icon.png)
```

Every curated article already shows the title from the curated section at the
top, so please do not include it in the markdown itself.

## Hooks

Hooks are an advanced feature of Bazaar. In essence, they allow you to
programmatically react to events and define dialogs with which you can query
user input. Currently, these are the event signals you can subscribe to:

* `view-app`: run the hook when the user clicks on an app tile or otherwise
  views an application in the main app page

* `article-app`: run the hook when the user clicks a `bazaar-hook://` tile
  inside a curated article

* `before-transaction`: run the hook right as the user invokes a transaction

* `after-transaction`: run the hook after a transaction completes

Hooks are run like a signal emission. After an event occurs, hooks that are
found to be of the appropriate type are evaluated in an order of priority.
Higher priority hooks have the ability to stop the emission from propagating
further downwards. In the case of some events, like "before-transaction", a hook
can also hint to Bazaar some action to take, in this case whether the
transaction should be aborted.

A shell snippet which is defined by you is evaluated with `/bin/sh -c` multiple
times over the course of a hook's execution. An invocation of the shell snippet
is referred to as a "stage". Your shell snippet (which of course could just
invoke another script written in whatever language you prefer) will be provided
a number of environment variables which together will describe the current
stage. Your snippet must react accordingly by printing a response to stdout,
which Bazaar will read back.

This opens up a lot of possibilities for customization; here are a few examples:

* You would like a certain appid to be added to steam after the user installs
  it, so you register a hook on "after-transaction" to query the user's
  permission with a custom dialog. If they confirm, your script will go forward
  with the task of setting up a steam shortcut.

* You would like to prevent users from installing a certain appid, as some other
  method of installation, such as a system package, would provide a superior
  experience. A blocklist could achieve this, but you don't like the idea of
  hiding anything from the user. A hook subscribed to the "before-transaction"
  event could issue a warning and ask for extra confirmation. If the user
  decides to listen to the warning, you can signal to Bazaar that the
  transaction should be aborted.

Here is an overview of the environment variables the shell snippet will receive:

* `BAZAAR_HOOK_INITIATED_UNIX_STAMP`: the unix timestamp in seconds at which
  this hook was first invoked (the number of seconds that have elapsed since
  1970-01-01 00:00:00 UTC)

* `BAZAAR_HOOK_INITIATED_UNIX_STAMP_USEC`: the unix timestamp in microseconds at
  which this hook was first invoked (the number of microseconds that have
  elapsed since 1970-01-01 00:00:00 UTC)

* `BAZAAR_HOOK_STAGE_IDX`: the number of stages this hook has run so far

* `BAZAAR_HOOK_ID`: the value of the "id" mapping

* `BAZAAR_HOOK_TYPE`: the value of the "when" mapping

* `BAZAAR_HOOK_WAS_ABORTED`: "true" if a dialog aborted the hook

* `BAZAAR_HOOK_DIALOG_ID`: if applicable, the id of the current dialog

* `BAZAAR_HOOK_DIALOG_RESPONSE_ID`: if applicable, the user response given
  through the current dialog

* `BAZAAR_APPID`: if applicable, the appid of the entry Bazaar is currently
  dealing with. Use this for hooks that are not related to transactions. If your
  hook deals with transactions, such as `before-transaction` or
  `after-transaction`, use `BAZAAR_TS_APPID` instead.

* `BAZAAR_TS_APPID`: if applicable, the appid of the entry Bazaar is currently
  dealing with. Use this for hooks that are related to transactions. If your
  hook does not deal with transactions, such as `view-app`, use `BAZAAR_APPID`
  instead.

* `BAZAAR_TS_TYPE`: if applicable, the type of transaction being run. Can be
  "install", "update", or "removal".

* `BAZAAR_HOOK_STAGE`: the stage at which the hook is running. This will
  indicate what the shell body is instructed to do at this time. The shell body
  must respond by outputting to stdout with a valid answer; the structure of a
  valid answer will depend on the stage, and if the structure is not valid the
  hook will be abandoned. The shell body might be run multiple times by Bazaar
  over the course of a hook with this variable set to differing values in order
  know how to orchestrate events in the UI, so the shell body must be able to
  branch depending on the value. The value may be any of the following:

  * `setup`: the hook is starting. Respond with "ok" to continue the
    execution of this hook, or "pass" to skip it and move on to the
    next registered hook

  * `setup-dialog` Bazaar is ready to ask the user a question with one of the
    dialogs you've defined inside the "dialogs" mapping. `BAZAAR_HOOK_DIALOG_ID`
    will tell you which one. Respond with "ok" to spawn the dialog, or "pass" to
    skip the dialog.

  * `teardown-dialog` Bazaar has received input from the user after asking them
    a question with one of the dialogs you've defined inside the "dialogs"
    mapping. `BAZAAR_HOOK_DIALOG_ID` will tell you which one.
    `BAZAAR_HOOK_DIALOG_RESPONSE_ID` will tell you the response the user chose.
    Respond with "ok" to continue, or "abort" to stop the execution of this
    hook.

  * `catch` One of your dialogs has aborted. This is your chance to handle the
    error. Respond with "recover" to continue, or "abort" to confirm that the
    execution of this hook should indeed skip to the teardown stage.

  * `action` Everything so far has gone according to plan, so it is time to take
    whatever external action this hook exists for. Bazaar requires no response
    at this time.

  * `teardown` The hook is cleaning up. Respond with "continue" to propagate the
    signal emission to lower priority hooks, or "stop" to indicate the emission
    should stop. Alternatively, if this hook type should hint to Bazaar an
    action to take (such as the "before-transaction" hook), respond with
    "confirm" to hint that the action should be taken, or "deny" to prevent the
    action from being taken. Both "confirm" and "deny" imply the effect of
    "stop", and "stop" implies "confirm".

### Example

Hooks are defined in the main yaml config as indicated by the
`hardcoded_main_config_path` comptime var. Here is a basic example demonstrating
how to define a hook:

`hardcoded_main_config_path`:
```yaml
hooks:
  - id: handle-jetbrains
    when: before-transaction
    dialogs:
      - id: jetbrains-warning
        title: >-
          JetBrains IDEs are not supported in this format
        # If true, render inline markup commands in body; see
        # https://docs.gtk.org/Pango/pango_markup.html
        body-use-markup: true
        body: >-
          This is a <a href="https://www.jetbrains.com/">JetBrains</a>
          application and is not officially supported on Flatpak. We
          recommend using the Toolbox app to manage JetBrains IDEs.
        # Determines which option will be assumed if the user hits the
        # escape key or otherwise cancels the dialog
        default-response-id: cancel
        options:
          - id: cancel
            string: "Cancel"
          - id: goto-web
            string: "Download JetBrains Toolbox"
            # can be "destructive" or "suggested" or omit for no
            # styling
            style: suggested
    shell: exec /absolute/path/to/bazaar-jetbrains-hook.bash
    # This gives you the opportunity to add a basic regex check on an incoming
    # appid before incurring the overhead of spawning a subprocess. This
    # improves performance and reduces script boilerplate for long lists of
    # hooks that execute on specific appids.
    check-appid-regex: ^com\.jetbrains\..*$
```

`/absolute/path/to/bazaar-jetbrains-hook.bash`:
```bash
  #!/usr/bin/env bash


  handle_setup_stage() {

      # only proceed if the user is installing something
      if [ "$BAZAAR_TS_TYPE" = install ]; then
          case "$BAZAAR_TS_APPID" in
              com\.jetbrains\.*)
                  # since the appid belongs to jetbrains, we continue
                  # with the hook
                  echo 'ok'
                  ;;
              *)
                  # otherwise, skip this hook
                  echo 'pass'
                  ;;
          esac
      else
          echo 'pass'
      fi

  }


  handle_setup_dialog_stage() {

      # we don't need to do anything here right now, just let Bazaar
      # know we should continue setting up the dialog
      echo 'ok'

  }


  handle_teardown_dialog_stage() {

      case "$BAZAAR_HOOK_DIALOG_RESPONSE_ID" in
          goto-web)
              # if the user pressed "Download JetBrains Toolbox",
              # continue
              echo 'ok'
              ;;
          *)
              # otherwise, let's not do anything
              echo 'abort'
              ;;
      esac

  }


  handle_catch_stage() {

      # this only happens if the `teardown-dialog` stage echoed "abort",
      # we could echo "recover" at this point to still go to the
      # `action` stage, but we have no reason to do that right now
      echo 'abort'

  }


  handle_action_stage() {

      # this is where we do the thing! it is important to use `nohup`
      # here so bazaar doesn't hang
      nohup xdg-open 'https://www.jetbrains.com/toolbox-app/'

  }


  handle_teardown_stage() {

      # Let's always prevent the user from installing JetBrains stuff
      echo 'deny'

  }


  # Branch based on the stage
  case "$BAZAAR_HOOK_STAGE" in
      setup) handle_setup_stage ;;
      setup-dialog) handle_setup_dialog_stage ;;
      teardown-dialog) handle_teardown_dialog_stage ;;
      catch) handle_catch_stage ;;
      action) handle_action_stage ;;
      teardown) handle_teardown_stage ;;
  esac


  # exit successfully
  exit 0
```

## Search Biases

Search biases are a mechanism provided by Bazaar to influence search results.
Given a regex string with which to match a search query, a bias can replace the
matched text with a new string and/or adjust the scores of certain appids
according to a defined function if a match occurs. This is particularly useful
in the following cases:

* An application is, for example, an image editor, but doesn't show up in the
  results for "image editor" because its metadata doesn't contain those search
  tokens for whatever reason. A search bias can ensure that the app appears
  despite this.

* You want an abbreviation or phrase to be expanded into a version which is more
  likely to yield better results. For example, Bazaar hardcodes the expansion of
  the abbreviation `dl` to `download`, which is more helpful when searching
  metadata.

### Boost Functions

Right now, there are two kinds of boost functions you can specify to influence
the score of certain appids in the event of a regex match: linear and
exponential:

#### Linear

Linear functions require a `slope` and a `y-intercept` and are evaluated like
this:

```
new-score = y-intercept + (slope * original-score)
```

#### Exponential

Exponential functions require a `factor` and a `y-intercept` and are evaluated
like this:

```
new-score = y-intercept * (factor ^ original-score)
```

### Examples

Search biases are defined in the main yaml config as indicated by the
`hardcoded_main_config_path` comptime var. Here is a basic example demonstrating
how to define search biases:

```yaml
search-biases:
  # Brief regex review:
  #   `^` indicates the beginning of the string
  #   `$` indicates the end of the string
  #   `\b` indicates a word boundary
  #   `(?i)` means case insensitive

  # Convert queries like "smb server" to "samba server"
  - regex: \b(?i)smb\b
    convert-to: samba

  # Give your favorite matrix clients a simple boost of 100.0 to their score
  - regex: \b(?i)matrix\b
    boost-appids:
      - org.gnome.Fractal
      - im.riot.Riot
    linear-boost:
      slope: 1.0
      y-intercept: 100.0

  # Convert the search _and_ boost appids
  - regex: ^(?i)deck$
    convert-to: steam deck
    boost-appids:
      - com.github.Matoking.protontricks
      - com.steamgriddb.steam-rom-manager
      - com.valvesoftware.SteamLink
    linear-boost:
      slope: 1.2
      y-intercept: 20.0

  # Biases are applied in the order that you specify them, so in both the case
  # that the user types "deck" or "steam deck launcher", the following will be
  # applied:
  - regex: \bsteam deck\b
    boost-appids:
      - net.retrodeck.retrodeck
    exponential-boost:
      factor: 2.0
      y-intercept: 5.0
```

Pro tip: pressing `ctrl-alt-d` in Bazaar activates debug mode, which enables you
to see the scores of app results on the search page. This is useful for
debugging search biases!

## Translations in YAML Configs

For any string scalar property in YAML configs parsed by bazaar, you can
optionally provide a map of language code ids -> translated strings. This is for
strings only, thus it doesn't apply to scalars which are explicitly parsed as
numbers, etc.

For instance, this is valid:

```yaml
# ...
      category:
        title: "My Favorite Apps"
        subtitle: "These are really good and you should download them!"
# ...
```

This is also valid:

```yaml
# ...
      category:
        title:
          en: "My Favorite Apps"
          es: "Mis Aplicaciones Favoritas"
          ko: "내가 가장 좋아하는 앱들"
        subtitle:
          en: "These are really good and you should download them!"
          es: "¡Son realmente buenos y deberías descargarlos!"
          ko: "이것들은 정말 좋으니 꼭 다운로드하세요!"
# ...
```

(I just used google translate for these, sorry if they are bad)
