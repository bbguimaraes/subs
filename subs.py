#!/usr/bin/env python3
import argparse
import datetime
import itertools
import operator
import os
import sqlite3
import sys
import urllib.parse
import xml.etree.ElementTree
import youtube_dl


def main(argv):
    args = parse_args(argv)
    args.file = args.file or db_file()
    with sqlite3.connect(args.file) as conn:
        subs = Subscriptions(args.verbose, conn)
        subs.init()
        cmd = args.cmd
        args_d = vars(args)
        for _ in map(args_d.__delitem__, ('cmd', 'file', 'verbose')): pass
        cmd(subs, **args_d)


def parse_args(argv):
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

    def __init__(self, verbose, ydl=None):
        self._opts = {'logger': self.logger(verbose), 'extract_flat': True}
        self._ydl = ydl or youtube_dl.YoutubeDL(self._opts)

    def info(self, url):
        return self._ydl.extract_info(url, download=False)

    def channel_entries(self, yt_id):
        url = self.info(self.CHANNEL_URL.format(yt_id))['url']
        return self.info(url)['entries']

    def upload_date(self, yt_id):
        d = self.info(self.VIDEO_URL.format(yt_id))['upload_date']
        ts = datetime.datetime.strptime(d, '%Y%m%d').timestamp()
        return int(ts)


class Subscriptions(object):
    def __init__(self, verbose, conn, now=None):
        self._verbose = verbose
        self._conn = conn
        self._now = now or datetime.datetime.now

    def _log(self, *msg): self._verbose and print(*msg)

    @classmethod
    def _make_args(cls, n): return ', '.join(('?',) * n)

    def raw(self, l):
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

    def list(self, ids, show_id, show_ids, unwatched):
        assert(bool(show_id) + bool(show_ids) <= 1)
        q = ['select']
        if show_id:
            q.append('subs.yt_id')
        elif show_ids:
            q.append('subs.yt_id, subs.name')
        else:
            q.append('subs.name')
        q.append('from subs')
        if unwatched:
            q.append(
                'join videos on subs.id == videos.sub and videos.watched == 0')
        if ids:
            q.append('where name in ({})'.format(
                Subscriptions._make_args(len(ids))))
        if unwatched:
            q.append('group by subs.id')
        q.append('order by subs.id')
        c = self._conn.cursor()
        c.execute(' '.join(q), ids)
        for _ in map(print, map(' '.join, c)): pass

    def list_videos(
            self, subscriptions, n, by_name, url, flat, show_ids, watched):
        fields = ['subs.id', 'subs.name', 'videos.watched']
        if show_ids or url:
            fields.append('videos.yt_id')
        if not url:
            fields.append('videos.title')
        ql = ['select', ', '.join(fields)]
        ql.append('from subs join videos on subs.id = videos.sub')
        args = []
        where = []
        if subscriptions:
            if by_name:
                where.append('({})'.format(
                    ' or '.join('subs.name glob ?' for _ in subscriptions)))
            else:
                where.append('subs.yt_id in ({})'.format(
                    Subscriptions._make_args(len(subscriptions))))
            args.extend(subscriptions)
        if watched is not None:
            where.append('(videos.watched == ?)')
            args.append(int(watched))
        if where:
            ql.append('where ' + ' and '.join(where))
        ql.append('order by subs.id, videos.id')
        q = ' '.join(ql)
        c = self._conn.cursor()
        c.execute(q, args)
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
        for name, n, watched in c:
            print(n, watched / n, name)
            total[0] += watched
            total[1] += n
        print(total[1], total[0] / total[1], 'total')

    def import_xml(self, path):
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

    def _sub_exists(self, c, yt_id):
        return bool(c
            .execute('select 1 from subs where yt_id == ?', (yt_id,))
            .fetchall())

    def add(self, yt_id, name):
        self._conn.cursor().execute(
            'insert into subs (yt_id, name) values (?, ?)',
            (yt_id, name))

    def update(self, items, cache, client=None):
        now = int(self._now().timestamp())
        cache = now - (cache if cache is not None else 24 * 60 * 60)
        c = self._conn.cursor()
        count = lambda: c.execute('select count(*) from videos').fetchone()[0]
        initial_count = count()
        ql = ['select id, name, yt_id from subs where last_update < ?']
        if items:
            ql.append('and name in ({})'.format(
                Subscriptions._make_args(len(items))))
        subs = c.execute(' '.join(ql), (cache, *items)).fetchall()
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

    def _video_exists(self, c, yt_id):
        return bool(c
            .execute('select 1 from videos where yt_id == ?', (yt_id,))
            .fetchall())

    def _add_video(self, c, sub_id, yt_id, title):
        c.execute(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)',
            (sub_id, yt_id, title))

    def _update_sub(self, c, sub_id, last_update):
        c.execute(
            'update subs set last_update = ? where id == ?',
            (last_update, sub_id))

    def watched(self, items, subs, oldest, older_than, url, remove):
        assert(bool(subs) + bool(oldest) + bool(older_than) <= 1)
        if subs:
            q = (
                'id in (select videos.id'
                ' from subs join videos on subs.id == videos.sub'
                ' where subs.name in ({}) and videos.watched == 0'
                ' order by videos.id)').format(
                    Subscriptions._make_args(len(items)))
        elif oldest:
            q = (
                'id in (select min(videos.id)'
                ' from subs join videos on subs.id == videos.sub'
                ' where subs.name in ({}) and videos.watched == 0'
                ' group by subs.id  order by videos.id)').format(
                    Subscriptions._make_args(len(items)))
        elif older_than:
            q = (
                'id in (select j.id from videos'
                ' join videos j on videos.sub == j.sub and videos.id > j.id'
                ' where videos.yt_id in ({}))'.format(
                    Subscriptions._make_args(len(items))))
        else:
            q = 'yt_id in ({})'.format(Subscriptions._make_args(len(items)))
        c = self._conn.cursor()
        if url:
            ql = ['select id, yt_id from videos where', q]
            items = c .execute(' '.join(ql), items).fetchall()
            q = 'id in ({})'.format(Subscriptions._make_args(len(items)))
            ids = list(map(operator.itemgetter(0), items))
        else:
            ids = items
        ql = ['update videos set watched = ? where', q]
        q = ' '.join(ql)
        c.execute(q, (not remove, *ids))
        if url:
            for x in items:
                print(Client.VIDEO_URL.format(x[1]))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
