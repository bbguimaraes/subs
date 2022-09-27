import operator
import sqlite3
import typing

def _fetch(client, l):
    return (*l[:-1], client.channel_entries(l[-1]))

def _remove_duplicates(l: typing.Collection[dict]):
    ret = []
    seen: typing.Set[str] = set()
    for x in l:
        yt_id, *_ = x
        if yt_id in seen:
            continue
        seen.add(yt_id)
        ret.append(x)
    return ret

def _video_exists(c: sqlite3.Cursor, yt_id: str):
    return bool(c
        .execute('select 1 from videos where yt_id == ?', (yt_id,))
        .fetchall())

def fetch(client, pool, l):
    return pool.imap_unordered(lambda x: _fetch(client, x), l)

def update(c: sqlite3.Cursor, videos):
    videos = map(operator.itemgetter('id', 'title'), videos)
    videos = _remove_duplicates(videos)
    videos = filter(lambda x: not _video_exists(c, x[0]), videos)
    return list(videos)
