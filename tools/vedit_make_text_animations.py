"""VEDIT: CapCut-style text animation presets (character pops, word springs, colour pops, loops)
written as recipes for the existing text animator engine (presets/text-animations/*.json).

Original recipes for VEDIT (GPLv3). Run from the app folder:

    python tools/vedit_make_text_animations.py
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'presets', 'text-animations')

STAGGER = ['stagger']


def inout(pid, label, category, sample, order, defaults, props, slots=('in', 'out'), extra=None):
    d = {'format': 1, 'id': pid, 'label': label, 'category': category, 'sampleText': sample,
         'slots': list(slots), 'order': order, 'defaults': defaults,
         'animators': [{'selectors': STAGGER, 'props': props}]}
    if extra:
        d.update(extra)
    return d


def loop(pid, label, category, sample, order, animators):
    return {'format': 1, 'id': pid, 'label': label, 'category': category, 'sampleText': sample,
            'slots': ['loop'], 'order': order, 'animators': animators}


def wiggle(domain, hz, phase, lo=-100, hi=100, wave='sine', tphase=0):
    sel = {'driver': 'wiggle', 'domain': domain, 'wave': wave, 'frequencyHz': hz,
           'spatialPhaseDeg': phase, 'minAmount': lo, 'maxAmount': hi}
    if tphase:
        sel['temporalPhaseDeg'] = tphase
    return [sel]


PRESETS = [
    inout('vedit-pop-char', 'Pop by letter', 'character', 'Pop pop', 300,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.35, 'ease': 'back'},
          {'scale': 0, 'opacity': 0}),
    inout('vedit-zoom-char', 'Grow by letter', 'character', 'Grow', 301,
          {'unit': 'character', 'stagger': 0.05, 'duration': 0.4, 'ease': 'easeOut'},
          {'scale': 300, 'opacity': 0}),
    inout('vedit-drop-char', 'Drop by letter', 'character', 'Drop', 302,
          {'unit': 'character', 'stagger': 0.05, 'duration': 0.6, 'ease': 'bounce'},
          {'position': {'x': 0, 'y': -1.4, 'unit': 'em'}, 'opacity': 0}),
    inout('vedit-wave-in', 'Wave in', 'character', 'Wave in', 303,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.45, 'ease': 'back'},
          {'position': {'x': 0, 'y': 0.7, 'unit': 'em'}, 'opacity': 0}),
    inout('vedit-random-char', 'Appear at random', 'character', 'Random', 304,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.2, 'ease': 'linear', 'order': 'random'},
          {'opacity': 0}),
    inout('vedit-color-pop', 'Colour pop', 'colour', 'Colour', 305,
          {'unit': 'character', 'stagger': 0.05, 'duration': 0.5, 'ease': 'back'},
          {'scale': 150, 'fillColor': '#ffffd400', 'opacity': 0}),
    inout('vedit-spring-word', 'Spring by word', 'word', 'Spring word', 306,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.5, 'ease': 'back'},
          {'scale': 0, 'rotation': -12, 'opacity': 0}),
    inout('vedit-flip-word', 'Flip by word', 'word', 'Flip word', 307,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.45, 'ease': 'easeOut'},
          {'scaleY': 0, 'opacity': 0}),
    inout('vedit-slide-word', 'Slide by word', 'word', 'Slide word', 308,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.45, 'ease': 'easeOut'},
          {'position': {'x': 1.5, 'y': 0, 'unit': 'em'}, 'opacity': 0}),
    inout('vedit-rotate-word', 'Turn by word', 'word', 'Turn word', 309,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.5, 'ease': 'easeOut'},
          {'rotation': 90, 'opacity': 0}),
    inout('vedit-blur-zoom', 'Soft zoom', 'basic', 'Soft zoom', 310,
          {'duration': 0.6, 'ease': 'easeOut'},
          {'blur': 20, 'scale': 160, 'opacity': 0}),
    inout('vedit-stretch', 'Open sideways', 'kinetic', 'Open', 311,
          {'duration': 0.45, 'ease': 'back'},
          {'scaleX': 0, 'opacity': 0}),
    inout('vedit-slam', 'Slam', 'kinetic', 'SLAM', 312,
          {'duration': 0.3, 'ease': 'easeIn'},
          {'scale': 400, 'opacity': 0}, slots=('in',)),
    inout('vedit-spread-out', 'Spread away', 'basic', 'Spread', 313,
          {'unit': 'character', 'stagger': 0.02, 'duration': 0.6, 'ease': 'easeIn', 'order': 'centerOut'},
          {'tracking': 1.2, 'trackingUnit': 'em', 'blur': 10, 'opacity': 0}, slots=('out',)),
    # VEDIT loop round 2026-10-20 (조박사님: CapCut text animations)
    inout('vedit-blur-char', 'Blur by letter', 'character', 'Soft letters', 314,
          {'unit': 'character', 'stagger': 0.05, 'duration': 0.5, 'ease': 'easeOut'},
          {'blur': 16, 'opacity': 0}),
    inout('vedit-slide-char', 'Slide by letter', 'character', 'Slide in', 315,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.45, 'ease': 'easeOut'},
          {'position': {'x': 1.2, 'y': 0, 'unit': 'em'}, 'opacity': 0}),
    inout('vedit-stamp-word', 'Stamp by word', 'word', 'Stamp it', 316,
          {'unit': 'word', 'stagger': 0.15, 'duration': 0.3, 'ease': 'easeIn'},
          {'scale': 260, 'opacity': 0}),
    inout('vedit-shake-in', 'Shake in', 'character', 'Shake', 317,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.55, 'ease': 'bounce'},
          {'rotation': 25, 'position': {'x': -0.3, 'y': 0, 'unit': 'em'}, 'opacity': 0}),
    loop('vedit-wave-loop', 'Wave by letter', 'character', 'Wave wave', 324,
         [{'selectors': wiggle('character', 1.0, 40), 'props': {
             'position': {'x': 0, 'y': -0.25, 'unit': 'em'}}}]),
    # VEDIT overhaul stage 4 (조박사님 2026-10-21: text animations too plain) — the engine's wipe,
    # caret, skew and blur, which the presets had left unused.
    inout('vedit-pen-write', 'Pen writing', 'character', 'Hand written', 330,
          {'unit': 'character', 'stagger': 0.07, 'duration': 0.28, 'ease': 'linear'},
          {'wipe': {'enabled': True, 'angleDeg': 0, 'softness': 0.2}}, slots=('in',)),
    inout('vedit-pen-write-caret', 'Writing with cursor', 'character', 'Writing…', 331,
          {'unit': 'character', 'stagger': 0.07, 'duration': 0.22, 'ease': 'linear'},
          {'wipe': {'enabled': True, 'angleDeg': 0, 'softness': 0.1}}, slots=('in',),
          extra={'caret': {'enabled': True, 'leadUs': 60000, 'blinkOnUs': 250000, 'blinkOffUs': 250000,
                           'holdAfterUs': 600000}}),
    inout('vedit-rise-wipe-word', 'Rise from a line', 'word', 'Rise up', 332,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.5, 'ease': 'easeOut'},
          {'wipe': {'enabled': True, 'angleDeg': -90, 'softness': 0.15},
           'position': {'x': 0, 'y': 0.5, 'unit': 'em'}}),
    inout('vedit-speed-char', 'Speed in', 'character', 'Speed!', 333,
          {'unit': 'character', 'stagger': 0.035, 'duration': 0.35, 'ease': 'easeOut'},
          {'position': {'x': 1.6, 'y': 0, 'unit': 'em'}, 'skew': -24, 'blur': 10, 'opacity': 0}),
    inout('vedit-flash-char', 'Flash by letter', 'light', 'Flash', 334,
          {'unit': 'character', 'stagger': 0.05, 'duration': 0.4, 'ease': 'easeOut'},
          {'fillColor': '#ffffffff', 'scale': 150, 'blur': 6, 'opacity': 0}),
    # VEDIT overhaul stage 7: more kinds.
    inout('vedit-zoom-blur-word', 'Zoom blur by word', 'word', 'Zoom in', 335,
          {'unit': 'word', 'stagger': 0.12, 'duration': 0.45, 'ease': 'easeOut'},
          {'scale': 300, 'blur': 14, 'opacity': 0}),
    inout('vedit-cascade', 'Cascade', 'character', 'Cascade', 336,
          {'unit': 'character', 'stagger': 0.04, 'duration': 0.5, 'ease': 'back'},
          {'position': {'x': 0, 'y': -1.0, 'unit': 'em'}, 'rotation': -30, 'opacity': 0}),
    loop('vedit-hop', 'Hop', 'kinetic', 'Hop hop', 320,
         [{'selectors': wiggle('word', 1.6, 90, lo=0, hi=100), 'props': {
             'position': {'x': 0, 'y': -0.12, 'unit': 'box', 'maxPx': 30}}}]),
    loop('vedit-rock-char', 'Rock by letter', 'character', 'Rock', 321,
         [{'selectors': wiggle('character', 1.2, 60), 'props': {'rotation': 10}}]),
    loop('vedit-blink', 'Blink', 'light', 'Blink', 322,
         [{'selectors': wiggle('block', 1.5, 0, lo=0, hi=100, wave='triangle'), 'props': {'opacity': 15}}]),
    loop('vedit-beat-word', 'Beat by word', 'word', 'Beat word', 323,
         [{'selectors': wiggle('word', 1.2, 120, lo=0, hi=100), 'props': {'scale': 118}}]),
]

if __name__ == '__main__':
    for p in PRESETS:
        with open(os.path.join(OUT, p['id'] + '.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(p, f, ensure_ascii=False, indent=2)
            f.write('\n')
        print('wrote', p['id'])
