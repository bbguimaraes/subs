subs
====

Subscription manager.

A fast and uniform way of following accounts on video platforms without an
account, and without going through the platform's interface and notification
system (especially useful to avoid YouTube's web interface and conspicuously
forgetful notifications).  Currently handles:

- [LBRY][lbry], using the JSON-RPC API of a local [`lbrynet`][lbrynet] server
- [YouTube][youtube], using [`yt-dlp`][yt_dlp]

Video history is kept in a local [SQlite][sqlite] database and presented via
either a CLI or a `curses` TUI interface.  The application is programmable and
extendable using [Lua][lua].

Building
--------

```console
$ make
```

Running
-------

```console
$ subs --help
Usage: subs [OPTION...] [COMMAND [ARG...]]

Options:
    -h, --help      This help text.
    -v              Increase previous log level, can appear multiple
                    times.
    --log-level N   Set log level to `N`.
    -f, --file DB   Use database file `DB`, default: $XDG_DATA_HOME/subs/db.

Commands:
    db SQL          Execute database query
    ls [OPTIONS]    List subscriptions.
    videos          List videos.
    add TYPE NAME ID
                    Add a subscription.
    rm ID           Remove a subscription.
    tag add NAME    Create a tag.
    tag subs|videos TAG_ID ID...
                    Add tag TAG_ID to subscriptions/videos.
    watched [-r|--remove] ID
                    Mark videos as watched (`-r` to unmark)
    update [OPTIONS] [ID...]
                    Fetch new videos from subscriptions.
    tui             Start curses terminal interface.
```

Terminal interface:

```console
$ subs tui
```

[lbry]: https://lbry.com
[lbrynet]: https://github.com/lbryio/lbry-sdk.git
[lua]: https://www.lua.org
[sqlite]: https://www.sqlite.org
[youtube]: https://youtube.com
[yt_dlp]: https://github.com/yt-dlp/yt-dlp.git
