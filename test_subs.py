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
            self.subs.list((), None, None, None)
        out = out.read()
        self.assertEqual(out, 'sub0\nsub1\n')
        with wrap_stdout() as out:
            self.subs.list(('sub0',), None, None, None)
        out = out.read()
        self.assertEqual(out, 'sub0\n')
        with wrap_stdout() as out:
            self.subs.list(('sub1',), None, None, None)
        out = out.read()
        self.assertEqual(out, 'sub1\n')

    def test_show_id(self):
        with wrap_stdout() as out:
            self.subs.list((), True, None, None)
        out = out.read()
        self.assertEqual(out, 'yt_id0\nyt_id1\n')
        with wrap_stdout() as out:
            self.subs.list(('sub0',), True, None, None)
        out = out.read()
        self.assertEqual(out, 'yt_id0\n')
        with wrap_stdout() as out:
            self.subs.list(('sub1',), True, None, None)
        out = out.read()
        self.assertEqual(out, 'yt_id1\n')

    def test_show_ids(self):
        with wrap_stdout() as out:
            self.subs.list((), None, True, None)
        out = out.read()
        self.assertEqual(out, 'yt_id0 sub0\nyt_id1 sub1\n')
        with wrap_stdout() as out:
            self.subs.list(('sub0',), None, True, None)
        out = out.read()
        self.assertEqual(out, 'yt_id0 sub0\n')
        with wrap_stdout() as out:
            self.subs.list(('sub1',), None, True, None)
        out = out.read()
        self.assertEqual(out, 'yt_id1 sub1\n')

    def test_unwatched(self):
        c = self.conn.cursor()
        c.execute('update videos set watched = 1')
        with wrap_stdout() as out:
            self.subs.list((), None, None, True)
        out = out.read()
        self.assertEqual(out, '')
        c.execute('update videos set watched = 0 where id = ?', (1,))
        with wrap_stdout() as out:
            self.subs.list(('sub1',), None, None, True)
        out = out.read()
        self.assertEqual(out, '')
        with wrap_stdout() as out:
            self.subs.list((), None, None, True)
        out = out.read()
        self.assertEqual(out, 'sub0\n')
        c.execute('update videos set watched = 1 where id = ?', (1,))
        c.execute('update videos set watched = 0 where id = ?', (5,))
        with wrap_stdout() as out:
            self.subs.list((), None, None, True)
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
            self.subs.list_videos((), None, None, None, None, None, None)
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
            self.subs.list_videos(
                ('yt_id1',), None, None, None, None, None, None)
        out = out.read()
        self.assertEqual(out,
            '[ ] title4\n'
            '[ ] title5\n')
        with wrap_stdout() as out:
            self.subs.list_videos(
                ('yt_id1', 'yt_id2'), None, None, None, None, None, None)
        out = out.read()
        self.assertEqual(out,
            'sub1\n'
            '  [ ] title4\n'
            '  [ ] title5\n'
            'sub2\n'
            '  [x] title6\n')

    def test_n(self):
        with wrap_stdout() as out:
            self.subs.list_videos((), 1, None, None, None, None, None)
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
            self.subs.list_videos(
                ('sub1',), None, True, None, None, None, None)
        out = out.read()
        self.assertEqual(out,
            '[ ] title4\n'
            '[ ] title5\n')

    def test_url(self):
        with wrap_stdout() as out:
            self.subs.list_videos((), None, None, True, None, None, None)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  [ ] https://www.youtube.com/watch?v=yt_id2\n'
            '  [x] https://www.youtube.com/watch?v=yt_id3\n'
            '  [ ] https://www.youtube.com/watch?v=yt_id4\n'
            '  [x] https://www.youtube.com/watch?v=yt_id5\n'
            'sub1\n'
            '  [ ] https://www.youtube.com/watch?v=yt_id6\n'
            '  [ ] https://www.youtube.com/watch?v=yt_id7\n'
            'sub2\n'
            '  [x] https://www.youtube.com/watch?v=yt_id8\n')

    def test_flat(self):
        with wrap_stdout() as out:
            self.subs.list_videos((), None, None, None, True, None, None)
        out = out.read()
        self.assertEqual(out,
            '[ ] title0\n'
            '[x] title1\n'
            '[ ] title2\n'
            '[x] title3\n'
            '[ ] title4\n'
            '[ ] title5\n'
            '[x] title6\n')

    def test_show_ids(self):
        with wrap_stdout() as out:
            self.subs.list_videos((), None, None, None, None, True, None)
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

    def test_watched(self):
        with wrap_stdout() as out:
            self.subs.list_videos((), None, None, None, None, None, True)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  title1\n'
            '  title3\n'
            'sub2\n'
            '  title6\n')
        with wrap_stdout() as out:
            self.subs.list_videos((), None, None, None, None, None, False)
        out = out.read()
        self.assertEqual(out,
            'sub0\n'
            '  title0\n'
            '  title2\n'
            'sub1\n'
            '  title4\n'
            '  title5\n')

    def test_mixed(self):
        with wrap_stdout() as out:
            self.subs.list_videos(
                ('sub0', 'sub2'), 2, True, None, True, True, True)
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

    def test_cache(self):
        now = int(datetime.datetime.now().timestamp())
        c = self.conn.cursor()
        c.execute('update subs set last_update = ?', (now,))
        self.subs.update((), 0, subs.Client(24 * 60, self.FakeYoutubeDL()))
        ret = min(
            y for x in c.execute('select distinct last_update from subs')
            for y in x)
        self.assertEqual(ret, now)

    def test_no_updates(self):
        ydl = self.FakeYoutubeDL({
            'https://www.youtube.com/channel/yt_id0': {'url': 'yt_id0_url'},
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id0_url': {'entries': ()},
            'yt_id1_url': {'entries': ()}})
        self.subs.update((), 0, subs.Client(0, ydl))
        ret = min(
            y for x in self.conn.cursor().execute(
                'select distinct last_update from subs')
            for y in x)
        self.assertNotEqual(ret, 0)

    def test_update(self):
        now = int(datetime.datetime.now().timestamp()) - 1
        ydl = self.FakeYoutubeDL({
            'https://www.youtube.com/channel/yt_id0': {'url': 'yt_id0_url'},
            'https://www.youtube.com/channel/yt_id1': {'url': 'yt_id1_url'},
            'yt_id0_url': {'entries': ({'id': 'yt_id8', 'title': 'title6'},)},
            'yt_id1_url': {
                'entries': (
                    {'id': 'yt_id9', 'title': 'title7'},
                    {'id': 'yt_id10', 'title': 'title8'})}})
        self.subs.update((), 0, subs.Client(0, ydl))
        c = self.conn.cursor()
        ret = min(
            y for x in c.execute(
                'select distinct last_update from subs')
            for y in x)
        self.assertGreater(ret, now)
        c.execute('select sub, yt_id, title from videos where id > 6')
        self.assertEqual(set(c), {
            (1, 'yt_id8', 'title6'),
            (2, 'yt_id9', 'title7'),
            (2, 'yt_id10', 'title8')})


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
        self.subs.watched(('yt_id3',), None, None, None, None, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(2,)])

    def test_watched_yt_ids_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(('yt_id3',), None, None, None, True, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(2,)])
        out = out.read()
        self.assertEqual(out, 'https://www.youtube.com/watch?v=yt_id3\n')

    def test_watched_subs(self):
        self.subs.watched(('sub0', 'sub2'), True, None, None, None, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (2,), (3,), (4,)])

    def test_watched_subs_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(('sub0', 'sub2'), True, None, None, True, False)
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
        self.subs.watched(('sub0', 'sub1'), None, True, None, None, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (5,)])

    def test_watched_oldest_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(('sub0', 'sub1'), None, True, None, True, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (5,)])
        out = out.read()
        self.assertEqual(out,
            'https://www.youtube.com/watch?v=yt_id2\n'
            'https://www.youtube.com/watch?v=yt_id6\n')

    def test_watched_older_than(self):
        self.subs.watched(('yt_id3',), None, None, True, None, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,)])

    def test_watched_older_than_print_url(self):
        with wrap_stdout() as out:
            self.subs.watched(('yt_id4',), None, None, True, True, False)
        c = self.conn.cursor()
        c.execute('select id from videos where watched == 1')
        self.assertEqual(c.fetchall(), [(1,), (2,)])
        out = out.read()
        self.assertEqual(out,
            'https://www.youtube.com/watch?v=yt_id2\n'
            'https://www.youtube.com/watch?v=yt_id3\n')

    def test_remove(self):
        with wrap_stdout() as out:
            self.subs.watched(('yt_id2',), None, None, None, None, False)
            self.subs.list_videos((), None, None, None, None, None, True)
        self.assertEqual(out.read(),
            'sub0\n'
            '  title0\n')
        with wrap_stdout() as out:
            self.subs.watched(('yt_id2',), None, None, None, None, True)
            self.subs.list_videos((), None, None, None, None, None, True)
        self.assertEqual(out.read(), '')
