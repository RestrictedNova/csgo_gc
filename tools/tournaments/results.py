# Turns the Liquipedia pages fetch.py saved into csgo_gc/tournament_data.cpp: every map played at
# every CS:GO major with its teams, score and the stage id the game shows for it.
# Usage: python results.py <pages dir> <csgo dir> <output .cpp>
#   <csgo dir> is the game's csgo folder, for scripts/items/items_game.txt and resource/csgo_english.txt
#
# Liquipedia's team names are mapped to the game's team ids with teams.json ("*" for every event,
# event ids for names that meant another team there). Every team has to have stickers at the event
# it played, which catches wrong mappings.
import calendar
import datetime
import json
import os
import re
import sys
import unicodedata

from fetch import page_path

# events and their pages: (title, kind, only this bracket). main pages have the groups and playoffs
EVENTS = {
    1: [('DreamHack/2013/Winter', 'main', None)],
    3: [('ESL/Major Series One/2014/Katowice', 'main', None)],
    4: [('ESL/One/2014/Cologne', 'main', None)],
    5: [('DreamHack/2014/Winter', 'main', None)],
    6: [('ESL/One/2015/Katowice', 'main', None)],
    7: [('ESL/One/2015/Cologne', 'main', None)],
    8: [('DreamHack/2015/Cluj-Napoca', 'main', None)],
    9: [('MLG/2016/Columbus', 'main', None)],
    10: [('ESL/One/2016/Cologne', 'main', None)],
    11: [('ELEAGUE/2017/Major', 'main', None)],
    12: [('PGL/2017/Krakow', 'main', None)],
}
for _event, _base in {
    13: 'ELEAGUE/2018/Major',
    14: 'FACEIT/2018/Major',
    15: 'Intel Extreme Masters/Season XIII/World Championship',
    16: 'StarLadder/2019/Major',
    18: 'PGL/2021/Stockholm',
    19: 'PGL/2022/Antwerp',
    20: 'Intel Extreme Masters/2022/Rio',
    21: 'BLAST/Major/2023/Paris',
}.items():
    EVENTS[_event] = [(_base + '/Challengers Stage', 'challengers', None), (_base + '/Legends Stage', 'group', None),
                      (_base + '/Champions Stage', 'playoffs', None)]
EVENTS[21][2] = ('BLAST/Major/2023/Paris', 'playoffs', 'z8GFN6kpnw')

# the all-star showmatches valve made teams for
SHOWMATCH_TEAMS = {
    1: {'team lurppis': 22, 'team thorin': 23},  # "Team Tomi" and "Team Duncan"
    9: {'all-stars north america': 64, 'all-stars europe': 65},
}

MAPS = {
    'Ancient': 'de_ancient', 'Anubis': 'de_anubis', 'Cache': 'de_cache', 'Cobblestone': 'de_cbble',
    'Dust II': 'de_dust2', 'Inferno': 'de_inferno', 'Mirage': 'de_mirage', 'Nuke': 'de_nuke',
    'Overpass': 'de_overpass', 'Train': 'de_train', 'Train SE': 'de_train', 'Vertigo': 'de_vertigo',
}

TIMEZONES = {'BRT': -3, 'BST': 1, 'CEST': 2, 'CET': 1, 'EDT': -4, 'EET': 2, 'EST': -5, 'GMT': 0, 'UTC': 0}

# CSGO_Tournament_Event_Stage_Display_<id>: single map, maps of a best of three
STAGES = {
    'group': {'first': 2, 'winners': (15, (43, 44, 45)), 'elimination': (16, (46, 47, 48)),
              'decider': (17, (21, 22, 23)), 'upper': 24, 'lower': 25, 'middle': 26},
    'challengers': {'first': 27, 'winners': (28, (37, 38, 39)), 'elimination': (29, (40, 41, 42)),
                    'decider': (30, (34, 35, 36)), 'upper': 31, 'lower': 32, 'middle': 33},
}
PLAYOFF_STAGES = [(11, 12, 13), (8, 9, 10), (5, 6, 7)]  # final, semifinals, quarterfinals
ALL_STAR_STAGE = 14


# --- game files

def read_text(path):
    data = open(path, 'rb').read()
    if data.startswith(b'\xff\xfe'):
        return data.decode('utf-16-le')[1:]
    return data.decode('utf-8-sig', errors='replace')


def parse_keyvalues(text):
    tokens = re.findall(r'"([^"]*)"|([{}])', text)

    def parse(i):
        obj = {}
        while i < len(tokens):
            string, brace = tokens[i]
            if brace == '}':
                return obj, i + 1
            key = string
            string, brace = tokens[i + 1]
            if brace == '{':
                value, i = parse(i + 2)
            else:
                value, i = string, i + 2
            if isinstance(obj.get(key), dict) and isinstance(value, dict):
                for k, v in value.items():
                    obj[key].setdefault(k, v)
            else:
                obj.setdefault(key, value)
        return obj, i

    return parse(0)[0]


def load_game(csgo_dir):
    game = parse_keyvalues(read_text(os.path.join(csgo_dir, 'scripts', 'items', 'items_game.txt')))['items_game']
    english = read_text(os.path.join(csgo_dir, 'resource', 'csgo_english.txt'))
    strings = {k.lower(): v for k, v in re.findall(r'"([^"]+)"\s+"([^"]*)"', english)}

    sticker_teams = {}
    for kit in game['sticker_kits'].values():
        if isinstance(kit, dict) and kit.get('tournament_event_id') and kit.get('tournament_team_id', '0') != '0' \
                and not kit.get('tournament_player_id'):
            sticker_teams.setdefault(int(kit['tournament_event_id']), set()).add(int(kit['tournament_team_id']))

    teams = {int(i): (t.get('tag'), strings.get('csgo_teamid_%s' % i)) for i, t in game['pro_teams'].items()}
    events = {e: (strings['csgo_tournament_event_name_%d' % e], strings['csgo_tournament_event_location_%d' % e])
              for e in EVENTS}
    return sticker_teams, teams, events


# --- wikitext

def strip_comments(text):
    text = re.sub(r'<!--.*?-->', '', text, flags=re.S)
    return re.sub(r'</?(noinclude|includeonly|onlyinclude)>', '', text)


def template_end(text, start):
    depth, i = 0, start
    while i < len(text):
        if text.startswith('{{', i):
            depth, i = depth + 1, i + 2
        elif text.startswith('}}', i):
            depth, i = depth - 1, i + 2
            if depth == 0:
                return i
        else:
            i += 1
    raise ValueError('unbalanced template at %d' % start)


def parse_template(text):
    text = text.strip()
    assert text.startswith('{{') and text.endswith('}}'), text[:80]
    inner = text[2:-2]
    parts, depth, link, current, i = [], 0, 0, [], 0
    while i < len(inner):
        two = inner[i:i + 2]
        if two in ('{{', '}}', '[[', ']]'):
            depth += {'{{': 1, '}}': -1}.get(two, 0)
            link += {'[[': 1, ']]': -1}.get(two, 0)
            current.append(two)
            i += 2
            continue
        if inner[i] == '|' and depth == 0 and link == 0:
            parts.append(''.join(current))
            current = []
        else:
            current.append(inner[i])
        i += 1
    parts.append(''.join(current))
    positional, named = [], {}
    for part in parts[1:]:
        key, eq, value = part.partition('=')
        if eq and re.fullmatch(r'\s*[\w ]+\s*', key):
            named[key.strip()] = value.strip()
        else:
            positional.append(part.strip())
    return parts[0].strip(), positional, named


def find_templates(text, names):
    for match in re.finditer(r'\{\{\s*(%s)\s*\|' % '|'.join(map(re.escape, names)), text):
        yield text[match.start():template_end(text, match.start())]


def parse_date(value):
    tz = re.search(r'\{\{[Aa]bbr/([A-Z]+)\}\}', value)
    value = re.sub(r'\{\{.*?\}\}', '', value).strip()
    m = re.fullmatch(r'(\w+) (\d+), (\d{4})(?: - (\d+):(\d+))?', value)
    if not m:
        raise ValueError('bad date ' + value)
    hour, minute = (int(m.group(4)), int(m.group(5))) if m.group(4) else (12, 0)
    local = datetime.datetime(int(m.group(3)), list(calendar.month_name).index(m.group(1)), int(m.group(2)),
                              hour, minute, tzinfo=datetime.timezone.utc)
    return int(local.timestamp()) - (TIMEZONES[tz.group(1)] if tz else 0) * 3600


def map_score(named):
    if any(re.fullmatch(r't1(t|ct)', k) for k in named):
        def rounds(team):
            return sum(int(v) for k, v in named.items() if re.fullmatch(r'(o\d+)?%s(t|ct)' % team, k) and v.strip())
        return rounds('t1'), rounds('t2')
    if named.get('score1', '').isdigit() and named.get('score2', '').isdigit():
        return int(named['score1']), int(named['score2'])
    return None


def opponent_name(value):
    _, positional, named = parse_template(value)
    return (named.get('template') or (positional[0] if positional else '')).strip().lower()


def parse_match(text, teams):
    _, _, named = parse_template(text)
    maps, bestof = [], 1
    for i in range(1, 6):
        value = named.get('map%d' % i)
        if not value:
            continue
        bestof = i
        _, _, m = parse_template(value)
        if m.get('finished') == 'skip' or m.get('map') in (None, '', 'Unknown'):
            continue
        score = map_score(m)
        if score and score != (0, 0):
            maps.append({'map': MAPS[m['map']], 'score': score, 'number': i})
    if not maps:
        return None
    return {'time': parse_date(named['date']), 'teams': teams, 'maps': maps, 'bestof': bestof}


def page_matches(text, kind, only, event, aliases, unknown):
    text = strip_comments(text)
    found = []

    def team(name):
        team_id = aliases.get(str(event), {}).get(name, aliases['*'].get(name))
        if team_id is None:
            unknown.add(name)
        return team_id

    if kind == 'main':
        for container in find_templates(text, ['SingleMatch']):
            for value in parse_template(container)[1]:
                if not value.startswith('{{Match'):
                    continue
                named = parse_template(value)[2]
                ids = [SHOWMATCH_TEAMS.get(event, {}).get(opponent_name(named[k])) for k in ('opponent1', 'opponent2')]
                match = parse_match(value, ids) if None not in ids else None
                if match:
                    match['kind'] = 'showmatch'
                    match['stages'] = [ALL_STAR_STAGE] * len(match['maps'])
                    found.append(match)

    seen = set()
    for container in find_templates(text, ['Matchlist', 'Bracket']):
        name, positional, named = parse_template(container)
        if only and named.get('id') != only:
            continue
        is_playoffs = name == 'Bracket' and positional and positional[0] == 'Bracket/8'
        container_kind = kind
        if kind == 'main':
            container_kind = 'playoffs' if is_playoffs else 'group'
        elif kind == 'playoffs' and not is_playoffs:
            raise ValueError('unexpected %s on a playoffs page' % name)
        for key, value in list(named.items()) + [(None, p) for p in positional]:
            if not value.startswith('{{Match'):
                continue
            match_named = parse_template(value)[2]
            ids = [team(opponent_name(match_named[k])) for k in ('opponent1', 'opponent2')]
            match = parse_match(value, ids)
            if not match or (tuple(ids), match['time']) in seen:
                continue
            seen.add((tuple(ids), match['time']))
            match['kind'] = container_kind
            match['round'] = int(re.match(r'R(\d+)M', key).group(1)) if container_kind == 'playoffs' else None
            found.append(match)
    return found


def assign_stages(matches):
    playoffs = [m for m in matches if m['kind'] == 'playoffs']
    rounds = sorted({m['round'] for m in playoffs}, reverse=True)
    for m in playoffs:
        ids = PLAYOFF_STAGES[rounds.index(m['round'])]
        m['stages'] = [ids[min(map_['number'], 3) - 1] for map_ in m['maps']]

    # group and swiss matches are named after the teams' records going in: gsl groups advance at
    # two wins, swiss at three
    for kind, ids in STAGES.items():
        stage = sorted((m for m in matches if m['kind'] == kind), key=lambda m: m['time'])
        if not stage:
            continue
        for m in stage:
            won = sum(a > b for a, b in (x['score'] for x in m['maps']))
            m['winner'] = 0 if won * 2 > len(m['maps']) else 1
        final = {}
        for m in stage:
            for side, team in enumerate(m['teams']):
                w, l = final.get(team, (0, 0))
                final[team] = (w + (side == m['winner']), l + (side != m['winner']))
        last = max(w for w, _ in final.values()) - 1
        record = {}
        for m in stage:
            (w0, l0), (w1, l1) = (record.get(t, (0, 0)) for t in m['teams'])
            if (w0, l0, w1, l1) == (0, 0, 0, 0):
                category = 'first'
            elif w0 == w1 == l0 == l1 == last:
                category = 'decider'
            elif w0 == w1 == last:
                category = 'winners'
            elif l0 == l1 == last:
                category = 'elimination'
            elif w0 > l0 and w1 > l1:
                category = 'upper'
            elif w0 < l0 and w1 < l1:
                category = 'lower'
            else:
                category = 'middle'
            value = ids[category]
            if isinstance(value, tuple):
                single, triple = value
                m['stages'] = [triple[min(x['number'], 3) - 1] if m['bestof'] > 1 else single for x in m['maps']]
            else:
                m['stages'] = [value] * len(m['maps'])
            for side, team in enumerate(m['teams']):
                w, l = record.get(team, (0, 0))
                record[team] = (w + (side == m['winner']), l + (side != m['winner']))


def check(event, matches, sticker_teams):
    problems = []
    for m in matches:
        if m['kind'] != 'showmatch':
            for team in m['teams']:
                if team not in sticker_teams.get(event, set()) and event in sticker_teams:
                    problems.append('event %d: team %d has no stickers at this event' % (event, team))
        for x in m['maps']:
            high, low = max(x['score']), min(x['score'])
            if not ((high == 16 and low <= 14) or (high > 16 and high - low in (2, 3, 4) and low >= 15)):
                problems.append('event %d: odd score %s on %s' % (event, x['score'], x['map']))
    return problems


# --- output

def identifier(text):
    text = unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode()
    words = re.sub(r'[^A-Za-z0-9 ]', '', text.replace('-', ' ')).split()
    name = ''.join(w[0].upper() + w[1:] for w in words)
    return 'Team' + name if name[0].isdigit() else name


def write_cpp(path, results, teams, events):
    used = sorted({t for matches in results.values() for m in matches for t in m['teams']})
    names = {t: identifier(teams[t][1]) for t in used}
    clashes = [t for t in used if sum(n.lower() == names[t].lower() for n in names.values()) > 1]
    for t in clashes:
        names[t] += str(t)
    lines = [
        '// Every map played at every CS:GO major, for Watch -> Tournaments and souvenir packages.',
        '// Generated by tools/tournaments/results.py from Liquipedia\'s results, with the team ids, tags',
        '// and names the game has. Change the script and run it again instead of editing this.',
        '',
        '#include "stdafx.h"',
        '#include "tournament.h"',
        '',
        'namespace',
        '{',
        '',
        'constexpr uint64_t Id(uint32_t eventId, uint32_t n)',
        '{',
        '    return (static_cast<uint64_t>(eventId) << 32) | n;',
        '}',
        '',
    ]
    for t in used:
        tag, name = teams[t]
        lines.append('constexpr TournamentTeamInfo %s{ %d, "%s", "%s" };' % (names[t], t, tag, name))

    labels = {'challengers': 'challengers stage', 'showmatch': 'all-star showmatch'}
    tables = []
    for event, matches in results.items():
        event_name, location = events[event]
        table = identifier(location)
        tables.append((event, event_name, table))
        staged = any(m['kind'] == 'challengers' for m in matches)
        lines += ['', '// ' + event_name, 'constexpr TournamentMatch %s[] = {' % table]

        # one row per map in the order they started (series ran side by side), maps after the
        # first of a series start about 75 minutes apart
        rows = sorted(((m['time'] + i * 4500, m, x, stage) for m in matches
                       for i, (x, stage) in enumerate(zip(m['maps'], m['stages']))), key=lambda r: r[0])
        kind = None
        for n, (time, m, x, stage) in enumerate(rows, 1):
            if m['kind'] != kind:
                kind = m['kind']
                label = labels.get(kind) or {'group': 'legends stage' if staged else 'group stage',
                                             'playoffs': 'champions stage' if staged else 'playoffs'}[kind]
                lines.append('    // ' + label)
            lines.append('    { Id(%d, %d), %d, %d, &%s, &%s, "%s", %d, %d },' % (
                event, n, time, stage, names[m['teams'][0]], names[m['teams'][1]],
                x['map'], x['score'][0], x['score'][1]))
        lines.append('};')

    lines += ['', 'constexpr TournamentInfo Events[] = {']
    lines += ['    { %d, "%s", %s },' % e for e in tables]
    lines += ['};', '', '} // namespace', '', 'std::span<const TournamentInfo> AllTournaments()', '{',
              '    return Events;', '}', '']
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


def main():
    pages_dir, csgo_dir, out_path = sys.argv[1:4]
    aliases = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'teams.json'), encoding='utf-8'))
    sticker_teams, teams, events = load_game(csgo_dir)

    results, problems = {}, []
    for event, pages in EVENTS.items():
        unknown, matches = set(), []
        for title, kind, only in pages:
            text = open(page_path(pages_dir, title), encoding='utf-8').read()
            matches += page_matches(text, kind, only, event, aliases, unknown)
        if unknown:
            problems.append('event %d: no team ids for %s, add them to teams.json' % (event, sorted(unknown)))
            continue
        assign_stages(matches)
        matches.sort(key=lambda m: m['time'])
        problems += check(event, matches, sticker_teams)
        results[event] = matches
        print('%-40s %3d matches, %3d maps' % (events[event][0], len(matches), sum(len(m['maps']) for m in matches)))

    if problems:
        sys.exit('\n'.join(problems))
    write_cpp(out_path, results, teams, events)


if __name__ == '__main__':
    main()
