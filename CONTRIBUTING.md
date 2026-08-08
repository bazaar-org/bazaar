# Contributing Guide

Thank you for contributing to Bazaar! Here are some instructions to get you started.

* [New Contributor Guide](#contributing-guide)
  * [Ways to Contribute](#ways-to-contribute)
  * [Find an Issue](#find-an-issue)
  * [Ask for Help](#ask-for-help)
  * [Pull Request Lifecycle](#pull-request-lifecycle)
  * [Development Environment Setup](#development-environment-setup)
  * [Sign Your Commits](#sign-your-commits)
  * [Pull Request Checklist](#pull-request-checklist)

Welcome! We are glad that you are here! 💖

As you get started, you are in the best position to give us feedback on areas of
our project that we need help with including:

* Problems found during setting up a new developer environment
* Documentation
* Bugs in our automation scripts and actions

If anything doesn't make sense, or doesn't work when you run it, please open a
bug report and let us know!

## Ways to Contribute

We welcome many different types of contributions including:

* New features
* Builds, CI/CD
* Bug fixes
* Documentation
* Issue Triage
* Answering questions in Discussions
* Release management
* Translations - please see the [Damned Lies Module](https://l10n.gnome.org/module/bazaar/).

## Find an Issue

These are the issues that need the most amount of attention and would be an effective way to get started:

- [Help Wanted issues](https://github.com/bazaar-org/bazaar/issues?q=is%3Aissue%20state%3Aopen%20label%3A%22help%20wanted%22)
- [Good first issues](https://github.com/bazaar-org/bazaar/issues?q=is%3Aissue%20state%3Aopen%20label%3A%22good%20first%20issue%22)

Sometimes there won’t be any issues with these labels. That’s ok! There is
likely still something for you to work on. If you want to contribute but you
don’t know where to start or can't find a suitable issue then feel free to post on the [discussion forum](https://github.com/bazaar-org/bazaar/discussions)

Once you see an issue that you'd like to work on, please post a comment saying
that you want to work on it. Something like "I want to work on this" is fine.

## Ask for Help

The best way to reach us with a question when contributing is to ask on:

* The original github issue you want to contribute to
* The [discussions](https://github.com/bazaar-org/bazaar/discussions) area

## Building

GNOME Builder or

```sh
just build-flatpak
```

See [Flatpak Docs](https://docs.flatpak.org/en/latest/flatpak-builder.html)

### Find out which version is installed for bug reports

```sh
flatpak info io.github.kolunmi.Bazaar
```

### Verbose output
```sh
G_MESSAGES_DEBUG=all flatpak run io.github.kolunmi.Bazaar
```

## Pull Request Lifecycle

[Instructions](https://contribute.cncf.io/maintainers/github/templates/required/contributing/#pull-request-lifecycle)

⚠️ **Explain your pull request process**

## Sign Your Commits

[Instructions](https://contribute.cncf.io/maintainers/github/templates/required/contributing/#sign-your-commits)

## Pull Request Checklist

When you submit your pull request, or you push new commits to it, our automated
systems will run some checks on your new code. We require that your pull request
passes these checks, but we also have more criteria than just that before we can
accept and merge it. We recommend that you check the following things locally
before you submit your code:

- [ ] Use the GNU Style Guide
- [ ] Format your commits using `clang-format`; see [.clang-format](/.clang-format)
- [ ] Follow the [GNOME Commit Style](https://handbook.gnome.org/development/commit-messages.html)
