#!/bin/env python3
import contextlib
import datetime
import io
import sqlite3
import sys
import unittest

import subs


@contextlib.contextmanager
def wrap_stdout():
    stdout = sys.stdout
    ret = io.StringIO()
    sys.stdout = ret
    try:
        yield ret
    finally:
        ret.seek(0)
        sys.stdout = stdout


class TestList(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(':memory:')
        self.subs = subs.Subscriptions(0, self.conn)
        self.subs.init()
        c = self.conn.cursor()
        c.executemany(
            'insert into subs (yt_id, name) values (?, ?)', (
                ('yt_id0', 'sub0'), ('yt_id1', 'sub1')))
        c.executemany(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)', (
                (1, 'yt_id2', 'title0'),
                (1, 'yt_id3', 'title1'),
                (1, 'yt_id4', 'title2'),
                (1, 'yt_id5', 'title3'),
                (2, 'yt_id6', 'title4'),
                (2, 'yt_id7', 'title5')))

    def test_list(self):
        with wrap_stdout() as out:
            self.subs.list()
        out = out.read()
        self.assertEqual(out, 'sub0\nsub1\n')
        with wrap_stdout() as out:
            self.subs.list(ids=('sub0',))
        out = out.read()
        self.assertEqual(out, 'sub0\n')
        with wrap_stdout() as out:
            self.subs.list(ids=('sub1',))
        out = out.read()
        self.assertEqual(out, 'sub1\n')

    def test_show_id(self):
        with wrap_stdout() as out:
            self.subs.list(fields=('yt_id',))
        out = out.read()
        self.assertEqual(out, 'yt_id0\nyt_id1\n')
        with wrap_stdout() as out:
            self.subs.list(ids=('sub0',), fields=('yt_id',))
        out = out.read()
        self.assertEqual(out, 'yt_id0\n')
        with wrap_stdout() as out:
            self.subs.list(ids=('sub1',), fields=('yt_id',))
        out = out.read()
        self.assertEqual(out, 'yt_id1\n')

    def test_unwatched(self):
        c = self.conn.cursor()
        c.execute('update videos set watched = 1')
        with wrap_stdout() as out:
            self.subs.list(unwatched=True)
        out = out.read()
        self.assertEqual(out, '')
        c.execute('update videos set watched = 0 where id = ?', (1,))
        with wrap_stdout() as out:
            self.subs.list(ids=('sub1',), unwatched=True)
        out = out.read()
        self.assertEqual(out, '')
        with wrap_stdout() as out:
            self.subs.list(unwatched=True)
        out = out.read()
        self.assertEqual(out, 'sub0\n')
        c.execute('update videos set watched = 1 where id = ?', (1,))
        c.execute('update videos set watched = 0 where id = ?', (5,))
        with wrap_stdout() as out:
            self.subs.list(unwatched=True)
        out = out.read()
        self.assertEqual(out, 'sub1\n')


class TestListVideos(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(':memory:')
        self.subs = subs.Subscriptions(0, self.conn)
        self.subs.init()
        c = self.conn.cursor()
        c.executemany(
            'insert into subs (yt_id, name) values (?, ?)', (
                ('yt_id0', 'sub0'), ('yt_id1', 'sub1'), ('yt_id2', 'sub2')))
        c.executemany(
            'insert into videos (sub, yt_id, title, watched)'
            ' values (?, ?, ?, ?)', (
                (1, 'yt_id2', 'title0', 0),
                (1, 'yt_id3', 'title1', 1),
                (1, 'yt_id4', 'title2', 0),
                (1, 'yt_id5', 'title3', 1),
                (2, 'yt_id6', 'title4', 0),
                (2, 'yt_id7', 'title5', 0),
                (3, 'yt_id8', 'title6', 1)))

    def test_list(self):
        with wrap_stdout() as out:
            self.subs.list_videos()
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n'
            '  [x] title1\n'
            '  [ ] title2\n'
            '  [x] title3\n'
            'sub1\n'
            '  [ ] title4\n'
            '  [ ] title5\n'
            'sub2\n'
            '  [x] title6\n')

    def test_subscriptions(self):
        with wrap_stdout() as out:
            self.subs.list_videos(subscriptions=('yt_id1',))
        out = out.read()
        self.assertEqual(out,
            '[ ] title4\n'
            '[ ] title5\n')
        with wrap_stdout() as out:
            self.subs.list_videos(subscriptions=('yt_id1', 'yt_id2'))
        out = out.read()
        self.assertEqual(out,
            'sub1\n'
            '  [ ] title4\n'
            '  [ ] title5\n'
            'sub2\n'
            '  [x] title6\n')

    def test_n(self):
        with wrap_stdout() as out:
            self.subs.list_videos(n=1)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n'
            'sub1\n'
            '  [ ] title4\n'
            'sub2\n'
            '  [x] title6\n')

    def test_by_name(self):
        with wrap_stdout() as out:
            self.subs.list_videos(by_name=True, subscriptions=('sub1',))
        out = out.read()
        self.assertEqual(out,
            '[ ] title4\n'
            '[ ] title5\n')

    def test_flat(self):
        with wrap_stdout() as out:
            self.subs.list_videos(flat=True)
        out = out.read()
        self.assertEqual(out,
            '[ ] title0\n'
            '[x] title1\n'
            '[ ] title2\n'
            '[x] title3\n'
            '[ ] title4\n'
            '[ ] title5\n'
            '[x] title6\n')

    def test_watched(self):
        with wrap_stdout() as out:
            self.subs.list_videos(watched=True)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [x] title1\n'
            '  [x] title3\n'
            'sub2\n'
            '  [x] title6\n')
        with wrap_stdout() as out:
            self.subs.list_videos(watched=False)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n'
            '  [ ] title2\n'
            'sub1\n'
            '  [ ] title4\n'
            '  [ ] title5\n')

    def test_fields(self):
        self.assertRaisesRegex(
            ValueError, '^invalid fields: invalid$',
            self.subs.list_videos, fields=('invalid',))
        with wrap_stdout() as out:
            self.subs.list_videos(fields=('title',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  title0\n'
            '  title1\n'
            '  title2\n'
            '  title3\n'
            'sub1\n'
            '  title4\n'
            '  title5\n'
            'sub2\n'
            '  title6\n')
        with wrap_stdout() as out:
            self.subs.list_videos(fields=('yt_id',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  yt_id2\n'
            '  yt_id3\n'
            '  yt_id4\n'
            '  yt_id5\n'
            'sub1\n'
            '  yt_id6\n'
            '  yt_id7\n'
            'sub2\n'
            '  yt_id8\n')
        with wrap_stdout() as out:
            self.subs.list_videos(fields=('watched',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ]\n'
            '  [x]\n'
            '  [ ]\n'
            '  [x]\n'
            'sub1\n'
            '  [ ]\n'
            '  [ ]\n'
            'sub2\n'
            '  [x]\n')
        with wrap_stdout() as out:
            self.subs.list_videos(fields=('watched', 'yt_id', 'title'))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] yt_id2 title0\n'
            '  [x] yt_id3 title1\n'
            '  [ ] yt_id4 title2\n'
            '  [x] yt_id5 title3\n'
            'sub1\n'
            '  [ ] yt_id6 title4\n'
            '  [ ] yt_id7 title5\n'
            'sub2\n'
            '  [x] yt_id8 title6\n')

    def test_mixed(self):
        with wrap_stdout() as out:
            self.subs.list_videos(
                subscriptions=('sub0', 'sub2'),
                fields=('yt_id', 'title'),
                n=2, by_name=True, flat=True, watched=True)
        out = out.read()
        self.assertEqual(out,
            'yt_id3 title1\n'
            'yt_id5 title3\n'
            'yt_id8 title6\n')


class TestUpdate(unittest.TestCase):
    class FakeYoutubeDL(object):
        def __init__(self, info=None):
            self.info = info

        def extract_info(self, url, download):
            return self.info[url]

    def setUp(self):
        self.conn = sqlite3.connect(':memory:')
        now = datetime.datetime.now()
        self.now = now
        self.subs = subs.Subscriptions(0, self.conn, now=lambda: now)
        self.subs.init()
        c = self.conn.cursor()
        c.executemany(
            'insert into subs (yt_id, name) values (?, ?)', (
                ('yt_id0', 'sub0'), ('yt_id1', 'sub1')))
        c.executemany(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)', (
                (1, 'yt_id2', 'title0'),
                (1, 'yt_id3', 'title1'),
                (1, 'yt_id4', 'title2'),
                (1, 'yt_id5', 'title3'),
                (2, 'yt_id6', 'title4'),
                (2, 'yt_id7', 'title5')))

    def test_cache(self):
        last_update = int(self.now.timestamp()) - 1
        c = self.conn.cursor()
        c.execute('update subs set last_update = ?', (last_update,))
        self.subs.update(
            (), cache=1, client=subs.Client(24 * 60, self.FakeYoutubeDL()))
        ret = min(
            y for x in c.execute('select distinct last_update from subs')
            for y in x)
        self.assertEqual(ret, last_update)

    def test_no_updates(self):
        ydl = self.FakeYoutubeDL({
            'https://www.youtube.com/channel/yt_id0': {'url': 'yt_id0_url'},
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id0_url': {'entries': ()},
            'yt_id1_url': {'entries': ()}})
        self.subs.update((), client=subs.Client(0, ydl))
        ret = min(
            y for x in self.conn.cursor().execute(
                'select distinct last_update from subs')
            for y in x)
        self.assertNotEqual(ret, 0)

    def test_update(self):
        ydl = self.FakeYoutubeDL({
            'https://www.youtube.com/channel/yt_id0': {'url': 'yt_id0_url'},
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id0_url': {'entries': ({'id': 'yt_id8', 'title': 'title6'},)},
            'yt_id1_url': {
                'entries': (
                    {'id': 'yt_id9', 'title': 'title7'},
                    {'id': 'yt_id10', 'title': 'title8'})}})
        self.subs.update((), client=subs.Client(0, ydl))
        c = self.conn.cursor()
        ret = {
            y for x in c.execute(
                'select distinct last_update from subs')
            for y in x}
        self.assertEqual(ret, {int(self.now.timestamp())})
        c.execute('select sub, yt_id, title from videos where id > 6')
        self.assertEqual(set(c), {
            (1, 'yt_id8', 'title6'),
            (2, 'yt_id9', 'title7'),
            (2, 'yt_id10', 'title8')})

    def test_set_last_video(self):
        c = self.conn.cursor()
        last_video = lambda: \
            c.execute('select id, last_video from subs').fetchall()
        self.assertEqual(last_video(), [(1, 0), (2, 0)])
        resp = {
            'https://www.youtube.com/channel/yt_id0': {'url': 'yt_id0_url'},
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id0_url': {'entries': ({'id': 'yt_id8', 'title': 'title6'},)},
            'yt_id1_url': {'entries': ()}}
        self.subs.update((), client=subs.Client(0, self.FakeYoutubeDL(resp)))
        self.assertEqual(
            last_video(),
            [(1, int(self.now.timestamp())), (2, 0)])

    def test_last_video(self):
        c = self.conn.cursor()
        c.execute(
            'update subs set last_video = ? where id = ?',
            (int(self.now.timestamp()), 2))
        resp = {
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id1_url': {'entries': ({'id': 'yt_id8', 'title': 'title6'},)}}
        self.subs.update(
            (), last_video=0, client=subs.Client(0, self.FakeYoutubeDL(resp)))
        c.execute('select count(*) from videos where sub == 2')
        self.assertEqual(c.fetchone()[0], 3)


class TestTag(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(':memory:')
        self.subs = subs.Subscriptions(0, self.conn)
        self.subs.init()
        c = self.conn.cursor()
        c.executemany(
            'insert into subs (yt_id, name) values (?, ?)', (
                ('yt_id0', 'sub0'), ('yt_id1', 'sub1')))
        c.executemany(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)', (
                (1, 'yt_id2', 'title0'),
                (1, 'yt_id3', 'title1'),
                (1, 'yt_id4', 'title2'),
                (1, 'yt_id5', 'title3'),
                (2, 'yt_id6', 'title4'),
                (2, 'yt_id7', 'title5')))
        c = self.conn.cursor()
        with wrap_stdout():
            self.subs.tag(tag='tag0', items=('yt_id2', 'yt_id3'))
            self.subs.tag(tag='tag1', items=('yt_id3', 'yt_id6'))

    def test_untagged(self):
        with wrap_stdout() as out:
            self.subs.list_videos(untagged=True)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title2\n'
            '  [ ] title3\n'
            'sub1\n'
            '  [ ] title5\n')

    def test_tag(self):
        with wrap_stdout() as out:
            self.subs.list_videos(tags=('tag0',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n'
            '  [ ] title1\n')
        with wrap_stdout() as out:
            self.subs.list_videos(tags=('tag1',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title1\n'
            'sub1\n'
            '  [ ] title4\n')
        with wrap_stdout() as out:
            self.subs.list_videos(tags=('tag0', 'tag1'))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n'
            '  [ ] title1\n'
            'sub1\n'
            '  [ ] title4\n')

    def test_remove(self):
        c = self.conn.cursor()
        with wrap_stdout():
            self.subs.tag(remove=True, tag='tag0', items=('yt_id3',))
            self.subs.tag(remove=True, tag='tag1', items=('yt_id6',))
        with wrap_stdout() as out:
            self.subs.list_videos(tags=('tag0',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title0\n')
        with wrap_stdout() as out:
            self.subs.list_videos(tags=('tag1',))
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] title1\n')


class TestWatched(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(':memory:')
        self.subs = subs.Subscriptions(0, self.conn)
        self.subs.init()
        c = self.conn.cursor()
        c.executemany(
            'insert into subs (yt_id, name) values (?, ?)', (
                ('yt_id0', 'sub0'), ('yt_id1', 'sub1')))
        c.executemany(
            'insert into videos (sub, yt_id, title) values (?, ?, ?)', (
                (1, 'yt_id2', 'title0'),
                (1, 'yt_id3', 'title1'),
                (1, 'yt_id4', 'title2'),
                (1, 'yt_id5', 'title3'),
                (2, 'yt_id6', 'title4'),
                (2, 'yt_id7', 'title5')))

    def test_watched_yt_ids(self):
        self.subs.watched(items=('yt_id3',))
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(2,)])

    def test_watched_yt_ids_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(items=('yt_id3',), url=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(2,)])
        out = out.read()
        self.assertEqual(out, 'https://www.youtube.com/watch?v=yt_id3\n')

    def test_watched_subs(self):
        self.subs.watched(items=('sub0', 'sub2'), subs=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (2,), (3,), (4,)])

    def test_watched_subs_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(items=('sub0', 'sub2'), subs=True, url=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (2,), (3,), (4,)])
        out = out.read()
        self.assertEqual(out,
            'https://www.youtube.com/watch?v=yt_id2\n'
            'https://www.youtube.com/watch?v=yt_id3\n'
            'https://www.youtube.com/watch?v=yt_id4\n'
            'https://www.youtube.com/watch?v=yt_id5\n')

    def test_watched_oldest(self):
        self.subs.watched(items=('sub0', 'sub1'), oldest=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (5,)])

    def test_watched_oldest_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(items=('sub0', 'sub1'), oldest=True, url=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (5,)])
        out = out.read()
        self.assertEqual(out,
            'https://www.youtube.com/watch?v=yt_id2\n'
            'https://www.youtube.com/watch?v=yt_id6\n')

    def test_watched_older_than(self):
        self.subs.watched(items=('yt_id3',), older_than=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,)])

    def test_watched_older_than_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(items=('yt_id4',), older_than=True, url=True)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (2,)])
        out = out.read()
        self.assertEqual(out,
            'https://www.youtube.com/watch?v=yt_id2\n'
            'https://www.youtube.com/watch?v=yt_id3\n')

    def test_remove(self):
        with wrap_stdout() as out:
            self.subs.watched(items=('yt_id2',))
            self.subs.list_videos(watched=True)
        self.assertEqual(out.read(),
            'sub0\n'
            '  [x] title0\n')
        with wrap_stdout() as out:
            self.subs.watched(items=('yt_id2',), remove=True)
            self.subs.list_videos(watched=True)
        self.assertEqual(out.read(), '')
