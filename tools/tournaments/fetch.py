# Downloads the wikitext of the Liquipedia pages that have the results of every CS:GO major.
# Usage: python fetch.py <out dir>
# Liquipedia asks API users to keep to one request every two seconds, this waits three.
import gzip
import json
import os
import sys
import time
import urllib.parse
import urllib.request

API = 'https://liquipedia.net/counterstrike/api.php'
HEADERS = {'User-Agent': 'Mozilla/5.0 (compatible; tournament-results-script/1.0)', 'Accept-Encoding': 'gzip'}

# the majors before the three-stage format have their groups and playoffs on the main page
MAIN_PAGES = [
    'DreamHack/2013/Winter',
    'ESL/Major Series One/2014/Katowice',
    'ESL/One/2014/Cologne',
    'DreamHack/2014/Winter',
    'ESL/One/2015/Katowice',
    'ESL/One/2015/Cologne',
    'DreamHack/2015/Cluj-Napoca',
    'MLG/2016/Columbus',
    'ESL/One/2016/Cologne',
    'ELEAGUE/2017/Major',
    'PGL/2017/Krakow',
    'BLAST/Major/2023/Paris',  # paris' champions stage page only shows a bracket defined here
]
STAGED = [
    'ELEAGUE/2018/Major',
    'FACEIT/2018/Major',
    'Intel Extreme Masters/Season XIII/World Championship',
    'StarLadder/2019/Major',
    'PGL/2021/Stockholm',
    'PGL/2022/Antwerp',
    'Intel Extreme Masters/2022/Rio',
    'BLAST/Major/2023/Paris',
]
PAGES = MAIN_PAGES + [base + '/' + stage for base in STAGED for stage in ('Challengers Stage', 'Legends Stage', 'Champions Stage')]


def page_path(out_dir, title):
    return os.path.join(out_dir, title.replace('/', '__').replace(' ', '_') + '.txt')


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for i, title in enumerate(PAGES):
        if i:
            time.sleep(3)
        params = {'action': 'query', 'prop': 'revisions', 'rvprop': 'content', 'rvslots': 'main',
                  'titles': title, 'redirects': 1, 'format': 'json', 'formatversion': 2}
        request = urllib.request.Request(API + '?' + urllib.parse.urlencode(params), headers=HEADERS)
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                data = gzip.decompress(data)
        result = json.loads(data)['query']['pages'][0]
        if 'revisions' not in result:
            sys.exit('missing page ' + title)
        text = result['revisions'][0]['slots']['main']['content']
        with open(page_path(out_dir, title), 'w', encoding='utf-8') as f:
            f.write(text)
        print('saved', title)


if __name__ == '__main__':
    main()
