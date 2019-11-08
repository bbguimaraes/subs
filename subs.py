#!/usr/bin/env python3
import argparse
import datetime
import itertools
import operator
import os
import sqlite3
import sys
import typing
import urllib.parse
import xml.etree.ElementTree
import youtube_dl


def main(argv: typing.Sequence[str]):
    args = parse_args(argv)
    args.file = args.file or db_file()
    with sqlite3.connect(args.file) as conn:
        subs = Subscriptions(args.verbose, conn)
        subs.init()
        cmd = args.cmd
        args_d = vars(args)
        for _ in map(args_d.__delitem__, ('cmd', 'file', 'verbose')): pass
        cmd(subs, **args_d)


def parse_args(argv: typing.Sequence[str]):
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        description='Manage Youtube subscriptions',
        epilog='''\
Add subscriptions using `add` or `import`.
\n
Fetch subscription information using `update` (takes a relatively long time
depending on how fast Youtube responds).
\n
List subscriptions using `ls`, videos using `videos`.
''')
    sub = parser.add_subparsers(dest='cmd', required=True)
    parser.add_argument('-v', '--verbose', action='count', default=0)
    parser.add_argument('-f', '--file',
        type=str, metavar='DB',
        help='Path to the database file, default: $XDG_DATA_HOME/subs/db')
    raw_parser = sub.add_parser('raw')
    raw_parser.set_defaults(cmd=lambda s, a: print(s.raw(a.raw)))
    raw_parser.add_argument('raw', type=str, nargs='+')
    count_parser = sub.add_parser('count')
    count_parser.set_defaults(cmd=Subscriptions.count)
    ls_parser = sub.add_parser('ls')
    ls_parser.set_defaults(cmd=Subscriptions.list)
    ls_parser.add_argument('ids', type=str, nargs='*')
    ls_parser.add_argument('--id', action='store_true', dest='show_id')
    ls_parser.add_argument('--show-ids', action='store_true')
    ls_parser.add_argument('--unwatched', action='store_true')
    videos_parser = sub.add_parser('videos')
    videos_parser.set_defaults(cmd=Subscriptions.list_videos)
    videos_parser.add_argument('subscriptions', type=str, nargs='*')
    videos_parser.add_argument('-n', type=int)
    videos_parser.add_argument('--by-name', action='store_true')
    videos_parser.add_argument('--url', action='store_true')
    videos_parser.add_argument('--flat', action='store_true')
    videos_parser.add_argument('--show-ids', action='store_true')
    videos_parser.add_argument('--watched', action='store_true', default=None)
    videos_parser.add_argument('--unwatched',
        action='store_false', dest='watched')
    import_parser = sub.add_parser('import',
        help='https://www.youtube.com/subscription_manager')
    import_parser.set_defaults(cmd=Subscriptions.import_xml)
    import_parser.add_argument('path', type=str)
    add_parser = sub.add_parser('add')
    add_parser.set_defaults(cmd=Subscriptions.add)
    add_parser.add_argument('name', type=str)
    add_parser.add_argument('yt_id', type=str, metavar='id')
    update_parser = sub.add_parser('update')
    update_parser.set_defaults(cmd=Subscriptions.update)
    update_parser.add_argument('items', type=str, nargs='*')
    update_parser.add_argument('--cache',
        type=int, metavar='s', help='''\
only update subscriptions that were last fetched `s` seconds ago or earlier
(default: 24 * 60 * 60)
''')
    watched_parser = sub.add_parser('watched')
    watched_parser.set_defaults(cmd=Subscriptions.watched)
    watched_parser.add_argument('items', type=str, nargs='*')
    watched_parser.add_argument('--all', action='store_true', dest='subs')
    watched_parser.add_argument('--oldest', action='store_true')
    watched_parser.add_argument('--older-than', action='store_true')
    watched_parser.add_argument('--url', action='store_true')
    watched_parser.add_argument('--remove', action='store_true')
    return parser.parse_args(argv)


def db_file():
    home = os.environ.get('XDG_DATA_HOME') \
        or os.path.join(os.environ.get('HOME'), '.local', 'share')
    d = os.path.join(home, 'subs')
    if not os.path.exists(d) and os.path.exists(home):
        os.mkdir(d)
    ret = os.path.join(d, 'db')
    return ret


class Client(object):
    CHANNEL_URL = 'https://www.youtube.com/channel/{}'
    VIDEO_URL = 'https://www.youtube.com/watch?v={}'

    class logger(object):
        warning = print
        error = print

        def __init__(self, verbose):
            self.debug = (lambda *_: None) if verbose < 2 else print

    def __init__(self, verbose: int, ydl=None):
        self._opts = {'logger': self.logger(verbose), 'extract_flat': True}
        self._ydl = ydl or youtube_dl.YoutubeDL(self._opts)

    def info(self, url: str):
        return self._ydl.extract_info(url, download=False)

    def channel_entries(self, yt_id: str):
        url = self.info(self.CHANNEL_URL.format(yt_id))['url']
        return self.info(url)['entries']

    def upload_date(self, yt_id: str):
        d = self.info(self.VIDEO_URL.format(yt_id))['upload_date']
        ts = datetime.datetime.strptime(d, '%Y%m%d').timestamp()
        return int(ts)


class Query(object):
    @classmethod
    def make_args(cls, n: int): return ', '.join(('?',) * n)

    def __init__(self, table: str):
        self.table = table
        self.fields: typing.List[str] = []
        self.joins: typing.List[str] = []
        self.wheres: typing.List[str] = []
        self.group_bys: typing.List[str] = []
        self.order_bys: typing.List[str] = []

    def add_fields(self, *name: str):
        self.fields.extend(name)

    def add_joins(self, *joins: str):
        self.joins.extend(joins)

    def add_filter(self, *exprs: str):
        self.wheres.extend(exprs)

    def add_group(self, *exprs: str):
        self.group_bys.extend(exprs)

    def add_order(self, *exprs: str):
        self.order_bys.extend(exprs)

    def _query(self, l):
        if self.wheres:
            l.append('where')
            l.append(' and '.join(self.wheres))
        if self.group_bys:
            l.append('group by')
            l.append(', '.join(self.group_bys))
        if self.order_bys:
            l.append('order by')
            l.append(', '.join(self.order_bys))
        return ' '.join(l)

    def query(self, distinct=False):
        ret = ['select']
        if distinct:
            ret.append('distinct')
        ret.extend((', '.join(self.fields), 'from', self.table))
        if self.joins:
            ret.append(' '.join(self.joins))
        return self._query(ret)

    def update(self, q):
        return self._query(['update', self.table, 'set', q])


class Subscriptions(object):
    def __init__(
            self, verbose: int, conn: sqlite3.Connection,
            now: typing.Callable[[], datetime.datetime]=None):
        self._verbose = verbose
        self._conn = conn
        self._now = now or datetime.datetime.now

    def _log(self, *msg): self._verbose and print(*msg)

    def raw(self, l: typing.Iterable):
        c = self._conn.cursor()
        return [c.execute(x).fetchall() for x in l]

    def init(self):
        c = self._conn.cursor()
        c.execute('select name from sqlite_master where type="table"')
        tables = {'subs', 'videos'} - {x[0] for x in c}
        if 'subs' in tables:
            self._log('creating table subs')
            c.execute(
                'create table subs ('
                'id integer not null primary key,'
                ' yt_id text unique not null,'
                ' name text null,'
                ' last_update integer not null default(0))')
        if 'videos' in tables:
            self._log('creating table videos')
            c.execute(
                'create table videos ('
                'id integer not null primary key,'
                ' sub integer not null,'
                ' yt_id text unique not null,'
                ' title text not null,'
                ' watched boolean not null default(0),'
                ' foreign key(sub) references subs(id))')

    def list(
            self, ids: typing.Sequence[str],
            show_id: bool, show_ids: bool, unwatched: bool):
        assert(bool(show_id) + bool(show_ids) <= 1)
        q = Query(table='subs')
        q.add_order('subs.id')
        if show_id:
            q.add_fields('subs.yt_id')
        elif show_ids:
            q.add_fields('subs.yt_id', 'subs.name')
        else:
            q.add_fields('subs.name')
        if unwatched:
            q.add_joins(
                'join videos on subs.id == videos.sub and videos.watched == 0')
        if ids:
            q.add_filter('name in ({})'.format(Query.make_args(len(ids))))
        if unwatched:
            q.add_group('subs.id')
        c = self._conn.cursor().execute(q.query(), ids)
        for _ in map(print, map(' '.join, c)): pass

    def list_videos(
            self, subscriptions: typing.Collection[str], n: int,
            by_name: bool, url: bool, flat: bool,
            show_ids: bool, watched: bool):
        q = Query('videos')
        q.add_joins('join subs on subs.id == videos.sub')
        q.add_fields('subs.id', 'subs.name', 'videos.watched')
        q.add_order('subs.id', 'videos.id')
        if show_ids or url:
            q.add_fields('videos.yt_id')
        if not url:
            q.add_fields('videos.title')
        args: typing.List[typing.Union[str, int]] = []
        if subscriptions:
            if by_name:
                q.add_filter('({})'.format(
                    ' or '.join('subs.name glob ?' for _ in subscriptions)))
            else:
                q.add_filter('subs.yt_id in ({})'.format(
                    Query.make_args(len(subscriptions))))
            args.extend(subscriptions)
        if watched is not None:
            q.add_filter('(videos.watched == ?)')
            args.append(int(watched))
        c = self._conn.cursor().execute(q.query(), args)
        n_subs = len(subscriptions)
        for (_, name), l in itertools.groupby(c, lambda x: x[:2]):
            if n is not None:
                l = itertools.islice(l, n)
            if not flat and n_subs != 1:
                print(name)
            for _, _, vwatched, *l in l:
                if not flat and n_subs != 1:
                    print(end='  ')
                if watched is None:
                    print(end='[' + ' x'[vwatched] + '] ')
                if url:
                    print(Client.VIDEO_URL.format(*l))
                else:
                    print(*l)

    def count(self):
        c = self._conn.execute(
            'select subs.name, count(*), sum(videos.watched = 1)'
            ' from subs join videos'
            ' where subs.id == videos.sub'
            ' group by subs.id')
        total = [0, 0]
        fmt = lambda w, n: (w / n, n - w, w, n)
        for name, n, watched in c:
            print(*fmt(watched, n), name)
            total[0] += watched
            total[1] += n
        print(*fmt(*total), 'total')

    def import_xml(self, path: str):
        tree = xml.etree.ElementTree.ElementTree(file=path)
        subs = tree.find('.//*[@title="YouTube Subscriptions"]')
        skip = lambda e, msg: print(
            'warning: skipping, {}: {}'.format(
                msg, xml.etree.ElementTree.tostring(e).decode('utf-8')),
            file=sys.stderr)
        add = []
        for x in subs or ():
            title = x.get('title')
            if title is None:
                skip(x, 'no title')
                continue
            url = x.get('xmlUrl')
            if url is None:
                skip(x, 'no xmlUrl')
                continue
            qs = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
            yt_id = qs.get('channel_id')
            if not yt_id:
                skip(x, 'invalid xmlUrl')
                continue
            add.append((title, yt_id[0]))
        c = self._conn.cursor()
        add = list(filter(lambda x: not self._sub_exists(c, x[1]), add))
        if not add:
            return
        self._log('adding subscriptions:', [x[0] for x in add])
        c.executemany('insert into subs (name, yt_id) values (?, ?)', add)

    def _sub_exists(self, c: sqlite3.Cursor, yt_id: str):
        return bool(c
            .execute('select 1 from subs where yt_id == ?', (yt_id,))
            .fetchall())

    def add(self, yt_id: str, name: str):
        self._conn.cursor().execute(
            'insert into subs (yt_id, name) values (?, ?)',
            (yt_id, name))

    def update(self, items: typing.Collection[str], cache: int, client=None):
        now = int(self._now().timestamp())
        cache = now - (cache if cache is not None else 24 * 60 * 60)
        c = self._conn.cursor()
        count = lambda: c.execute('select count(*) from videos').fetchone()[0]
        initial_count = count()
        q = Query('subs')
        q.add_fields('id', 'name', 'yt_id')
        q.add_filter('last_update < ?')
        if items:
            q.add_filter('name in ({})'.format(Query.make_args(len(items))))
        subs = c.execute(q.query(), (cache, *items)).fetchall()
        if client is None:
            client = Client(self._verbose)
        for sub_id, name, yt_id in subs:
            self._log('updating', name)
            videos = client.channel_entries(yt_id)
            self._log('found', len(videos), 'videos')
            videos = filter(
                lambda x: not self._video_exists(c, x[0]),
                map(operator.itemgetter('id', 'title'), reversed(videos)))
            for vid, title in videos:
                self._log('adding video', vid, '-', title)
                self._add_video(c, sub_id, vid, title)
            self._update_sub(c, sub_id, now)
            c.execute('commit')
        for _ in c: pass
        self._log(f'{count() - initial_count} new videos added after @{cache}')

    def _video_exists(self, c: sqlite3.Cursor, yt_id: str):
        return bool(c
            .execute('select 1 from videos where yt_id == ?', (yt_id,))
            .fetchall())

    def _add_video(
            self, c: sqlite3.Cursor, sub_id: str, yt_id: str, title: str):
        c.execute(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)',
            (sub_id, yt_id, title))

    def _update_sub(self, c: sqlite3.Cursor, sub_id: str, last_update: int):
        c.execute(
            'update subs set last_update = ? where id == ?',
            (last_update, sub_id))

    def watched(
            self, items: typing.Collection[str],
            subs: bool, oldest: bool, older_than: bool, url: bool,
            remove: bool=False):
        assert(bool(subs) + bool(oldest) + bool(older_than) <= 1)
        q = Query('videos')
        if subs:
            q.add_filter(
                'id in (select videos.id'
                ' from subs join videos on subs.id == videos.sub'
                ' where subs.name in ({}) and videos.watched == 0'
                ' order by videos.id)'.format(
                    Query.make_args(len(items))))
        elif oldest:
            q.add_filter(
                'id in (select min(videos.id)'
                ' from subs join videos on subs.id == videos.sub'
                ' where subs.name in ({}) and videos.watched == 0'
                ' group by subs.id  order by videos.id)'.format(
                    Query.make_args(len(items))))
        elif older_than:
            q.add_filter(
                'id in (select j.id from videos'
                ' join videos j on videos.sub == j.sub and videos.id > j.id'
                ' where videos.yt_id in ({}))'.format(
                    Query.make_args(len(items))))
        else:
            q.add_filter('yt_id in ({})'.format(Query.make_args(len(items))))
        c = self._conn.cursor()
        ids: typing.Collection[typing.Union[str, int]]
        if url:
            q.add_fields('id, yt_id')
            items = c .execute(q.query(), items).fetchall()
            q = Query('videos')
            q.add_fields('id')
            q.add_filter('id in ({})'.format(Query.make_args(len(items))))
            ids = list(map(operator.itemgetter(0), items))
        else:
            ids = items
        c.execute(q.update('watched = ?'), (not remove, *ids))
        if url:
            for x in items:
                print(Client.VIDEO_URL.format(x[1]))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
