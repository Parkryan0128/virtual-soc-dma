"""One validated manifest drives both firmware compilation and execution."""
import json
from pathlib import Path
import re

STAGES = ['boot', 'detect', 'polling', 'irq', 'all']
MANIFEST = Path(__file__).resolve().parents[1] / 'tests/scenarios/firmware.json'


def load_scenarios(path=MANIFEST):
    scenarios = json.loads(Path(path).read_text())
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError('firmware manifest must be a nonempty list')
    names = set()
    for scenario in scenarios:
        if not isinstance(scenario, dict):
            raise ValueError('scenario must be an object')
        name = scenario.get('name', '')
        if not isinstance(name, str) or not re.fullmatch(r'[a-z][a-z0-9_]*', name) or name in names:
            raise ValueError('invalid or duplicate scenario name')
        names.add(name)
        if scenario.get('stage') not in STAGES:
            raise ValueError(f'{name}: unknown stage')
        for field in ('expected', 'options', 'defines'):
            value = scenario.get(field, [])
            if not isinstance(value, list) or any(not isinstance(v, str) or not v.strip() for v in value):
                raise ValueError(f'{name}: {field} must contain nonempty strings')
        if not scenario.get('expected'):
            raise ValueError(f'{name}: expected evidence is required')
        for field in ('demo', 'trace'):
            if field in scenario and not isinstance(scenario[field], bool):
                raise ValueError(f'{name}: {field} must be a boolean')
        source = scenario.setdefault('source', name + '.c')
        if not isinstance(source, str) or not re.fullmatch(r'[a-z][a-z0-9_]*\.c', source):
            raise ValueError(f'{name}: invalid firmware source')
        if any(not re.fullmatch(r'[A-Z][A-Z0-9_]*(=[0-9]+)?', d) for d in scenario.get('defines', [])):
            raise ValueError(f'{name}: invalid compiler definition')
        if 'machine' in scenario and (not isinstance(scenario['machine'], str) or not scenario['machine']):
            raise ValueError(f'{name}: invalid machine')
    return scenarios


def select_scenarios(scenarios, stage='all', name=None, demo=False):
    if name:
        selected = [s for s in scenarios if s['name'] == name]
    elif demo:
        selected = [s for s in scenarios if s.get('demo')]
    else:
        selected = [s for s in scenarios if STAGES.index(s['stage']) <= STAGES.index(stage)]
    if not selected:
        raise ValueError('selection contains no firmware scenarios')
    return selected
