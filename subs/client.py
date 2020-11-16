import datetime

import youtube_dl


class Client(object):
    CHANNEL_URL = 'https://www.youtube.com/channel/{}/videos'
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
        return self.info(self.CHANNEL_URL.format(yt_id))['entries']

    def upload_date(self, yt_id: str):
        d = self.info(self.VIDEO_URL.format(yt_id))['upload_date']
        ts = datetime.datetime.strptime(d, '%Y%m%d').timestamp()
        return int(ts)
