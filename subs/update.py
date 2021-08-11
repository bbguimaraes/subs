import operator
import sqlite3
import typing

def _fetch(client, delay, l):
    yt_id, name = l[-1], l[-2]
    try:
        entries = client.channel_entries(yt_id)
    except youtube_dl.utils.DownloadError as ex:
        print(f'failed to fetch {name}:', file=sys.stderr)
        raise ex
    ret = (*l[:-1], entries)
    if delay:
        time.sleep(delay)
    return ret

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

def fetch(client, pool, delay, l):
    return pool.imap_unordered(lambda x: _fetch(client, delay, x), l)

def update(c: sqlite3.Cursor, videos):
    videos = map(operator.itemgetter('id', 'title'), videos)
    videos = _remove_duplicates(videos)
    videos = filter(lambda x: not _video_exists(c, x[0]), videos)
    return list(videos)
