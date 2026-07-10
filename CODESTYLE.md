# Bazaar Code Style Rules

Thanks for your interest in contributing to Bazaar! Here are the code style
rules which you must adhere to in order to keep a clean and consistent codebase:

## Formatting

Bazaar uses the GNU C style. The `.clang-format` file at the root project
directory will configure `clang-format` to follow the proper conventions.

## General Rules

* _Always_ prefer using `g_auto`, `g_autofree`, `g_autoptr`, `g_autolist`, and
  `g_autoslist` over calling `*_unref` or `*_free` manually. If a variable needs
  to escape the scope, either increment its reference count or use
  `g_steal_pointer`.

* When you are otherwise forced to call `*_unref` or `*_free` manually, _always_
  use `g_clear_pointer` or `g_clear_object`. This prevents use-after-free bugs.

* _Always_ prefer using the libdex API over the older GTask API for async
  operations. It is cleaner and easier to read, and it prevents callbacks from
  cluttering the source file. This often means spawning a fiber and using
  `dex_await`. To wrap a call to a function which uses GTask, use
  [dex_async_pair_new](https://gnome.pages.gitlab.gnome.org/libdex/libdex-1/class.AsyncPair.html).
  See `src/bz-download-worker.c` for an example of this.

* _Always_ declare variables first and initialize them to `0` or `NULL`.
  Function calls must not be made in the declaration section of a scope.

* The general layout of a `.c` file must be in this order:

  1. License comment with your name mentioned
  2. `#include "config.h"`
  3. Include external headers, like `#include <gtk/gtk.h>`
  4. Include internal headers, like `#include "env.h"`
  5. If applicable, define the class's internal struct
  6. If applicable, `G_DEFINE_TYPE (...)` or equivalent
  7. If applicable, the property enums and `static GParamSpec *props[LAST_PROP] = { 0 };`
  8. Declare static functions (which are not virtual methods)
  9. If applicable, define class virtual methods (`dispose`, `class_init`,
     `snapshot`, etc)
  10. Define the public API (non-`static` functions which were declared in this
      `.c` file's associated `.h` header file)
  11. Define the static functions which were previously declared in this file
