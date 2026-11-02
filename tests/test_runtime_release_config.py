"""The delivered INI has the settings approved by real RE4 audio/guard QA."""
import configparser
from pathlib import Path


def test_delivered_health_alert_is_enabled_with_verified_settings():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert runtime.getint('LowHealth') == 1
    assert runtime.getint('LowHealthThreshold') == 360
    assert runtime.getint('LowHealthIntervalMs') == 4000
    assert runtime.getint('LowHealthVolumePercent') == 35
    assert runtime.getint('TraceKnife') == 0
    assert runtime.getint('SwordSheath') == 1


def test_knife_damage_multiplier_and_heart_containers_are_removed():
    # 20-oct cleanup: SwordDamage (rejected by design) and the heart-container pilot are gone.
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    for key in ('SwordDamage', 'SwordDamageMilli', 'HeartContainers', 'TraceHearts', 'HeartTraceMaxEvents'):
        assert key not in runtime, key


def test_chapter_progression_remains_off_until_native_qa():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert runtime.getint('ChapterProgression') == 0
    assert runtime.getint('TraceChapter') == 0
    assert 1 <= runtime.getint('ChapterTraceMaxEvents') <= 1000


def test_parry_observation_remains_explicitly_opt_in():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['LinkRuntime'].getint('ParryObserve') == 0


def test_mortal_draw_f8_pilot_is_removed():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert 'MortalDrawTrial' not in config['LinkRuntime']


def test_ocarina_stays_off_by_default():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    ocarina = config['Ocarina']
    assert ocarina.getint('Enabled') == 0
    for piece in ('Item', 'Freeze', 'Mount', 'Pose', 'Notes', 'Pad'):
        assert ocarina.getint(piece) == 0


def test_combat_runtime_stays_off_by_default():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    for key in ('HitPartObserve', 'MortalDrawObserve', 'MortalDrawContextual', 'MortalDrawDamage',
                'ParryReaction', 'ParryRiposte', 'MortalDrawLineWaiver'):
        assert runtime.getint(key) == 0, key
    assert runtime.getint('ParryRiposteMilli') == 1000
    assert runtime.getint('MortalDrawMaxRange') == 1200
    assert runtime.getint('MortalDrawStrikeTick') == 16


def test_parry_general_block_stays_off_by_default():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert runtime.getint('ParryGeneralBlock') == 0
    assert runtime.getint('ParryGeneralBosses') == 1
    assert runtime.getint('ParryGeneralThrown') == 1


def test_fps_probe_is_read_only_and_on():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['LinkRuntime'].getint('FpsProbe') == 1


def test_fairy_revive_is_on_with_its_sound():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    fairy = config['FairyRevive']
    assert fairy.getint('Enabled') == 1
    assert fairy.getint('Sound') == 1
    assert 0 <= fairy.getint('SoundVolumePercent') <= 100


def test_rupee_colours_wait_for_their_room_data():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['Rupees'].getint('Enabled') == 0


def test_case_heart_items_wait_for_their_case_data():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['LinkRuntime'].getint('CaseHeartItems') == 0


def test_mortal_draw_prompt_defaults_to_the_action_button():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert runtime.getint('MortalDrawPrompt') == 1
    assert runtime.get('MortalDrawKey') == 'E'
    assert runtime.get('MortalDrawPadButton') == 'None'
    assert runtime.getint('MortalDrawMaskCheck') == 1


def test_parry_grace_window_is_200_ms():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['LinkRuntime'].getint('ParryGraceMs') == 200


def test_parry_window_is_750_ms():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    assert config['LinkRuntime'].getint('ParryWindowMs') == 750


def test_event_clock_removed_from_runtime_config():
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert 'EventClock' not in runtime
    assert 'EventClockName' not in runtime


def test_sword_trail_is_on_with_its_defaults():
    # Task 4a (20-oct): TP's sword trail in the spin and the Great Spin, on in the delivered INI.
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'dll/link_runtime.ini')
    runtime = config['LinkRuntime']
    assert runtime.getint('SwordTrail') == 1
    assert runtime.get('SwordTrailMoves') == 'spin,great_spin,back_slice,ending,slash'
    assert runtime.getint('SwordTrailLengthMs') == 200
    assert runtime.getfloat('SwordTrailInner') == 0.25
    assert runtime.get('SwordTrailColor') == '9FD0FF' and runtime.getint('SwordTrailAlpha') == 150
    assert runtime.get('SwordTrailGreatColor') == 'FF3A28' and runtime.getint('SwordTrailGreatAlpha') == 170
    # trail-moves-01 (21-oct): Back Slice, Ending Blow (and its leap) and the native knife slashes.
    assert runtime.get('SwordTrailBackColor') == 'FFD24A' and runtime.getint('SwordTrailBackAlpha') == 160
    assert runtime.getint('SwordTrailBackLengthMs') == 250
    assert runtime.get('SwordTrailEndingColor') == 'FFFFFF' and runtime.getint('SwordTrailEndingAlpha') == 190
    assert runtime.getint('SwordTrailEndingLengthMs') == 300
    assert runtime.get('SwordTrailSlashColor') == 'E8F0FF' and runtime.getint('SwordTrailSlashAlpha') == 90
    assert runtime.getint('SwordTrailSlashLengthMs') == 150
