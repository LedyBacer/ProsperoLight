# ps5-native-app-boilerplate - durable SQLite run journal and artifact index.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sqlite3
import time
import uuid
from contextlib import contextmanager
from pathlib import Path


TERMINAL = {'COMPLETED', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'}
NEXT = {
    'CREATED': {'PREFLIGHT', 'CANCELLED', 'FAILED'},
    'PREFLIGHT': {'PREPARE_STIMULUS', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'PREPARE_STIMULUS': {'START_PS5', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'START_PS5': {'WAIT_STREAM_READY', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'WAIT_STREAM_READY': {'WARMUP', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'WARMUP': {'MEASURE', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'MEASURE': {'COOLDOWN', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'COOLDOWN': {'STOP_STREAM', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'STOP_STREAM': {'COLLECT', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'COLLECT': {'ANALYZE', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
    'ANALYZE': {'COMPLETED', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'},
}


class RunStore:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.artifacts = self.root / 'runs'
        self.artifacts.mkdir(mode=0o700, exist_ok=True)
        self.db = self.root / 'runs.sqlite3'
        if not self.db.exists(): self.db.touch(mode=0o600)
        try: os.chmod(self.db, 0o600)
        except OSError: pass
        with self.connect() as c:
            c.executescript('''
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS runs(
                  run_id TEXT PRIMARY KEY, created REAL NOT NULL, updated REAL NOT NULL,
                  state TEXT NOT NULL, stage TEXT NOT NULL, verdict TEXT NOT NULL,
                  profile TEXT NOT NULL, report TEXT, reason_code TEXT, reason TEXT);
                CREATE TABLE IF NOT EXISTS events(
                  seq INTEGER PRIMARY KEY AUTOINCREMENT, run_id TEXT NOT NULL,
                  at REAL NOT NULL, state TEXT NOT NULL, stage TEXT NOT NULL,
                  kind TEXT NOT NULL, data TEXT NOT NULL,
                  FOREIGN KEY(run_id) REFERENCES runs(run_id));
                CREATE INDEX IF NOT EXISTS events_run_seq ON events(run_id,seq);
                CREATE TABLE IF NOT EXISTS cancellations(run_id TEXT PRIMARY KEY);
                CREATE TABLE IF NOT EXISTS leases(
                  name TEXT PRIMARY KEY, run_id TEXT NOT NULL, acquired REAL NOT NULL,
                  heartbeat REAL NOT NULL);
            ''')
            columns = {r['name'] for r in c.execute('PRAGMA table_info(leases)')}
            if 'heartbeat' not in columns:
                c.execute('ALTER TABLE leases ADD COLUMN heartbeat REAL NOT NULL DEFAULT 0')

    def restore_reports(self):
        """Repair a crash between journal commit and report-file publication."""
        repaired = []
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            for row in c.execute('SELECT run_id,state,report FROM runs WHERE report IS NOT NULL'):
                if row['state'] in TERMINAL:
                    path = self.artifacts / row['run_id'] / 'report.json'
                    value = json.loads(row['report'])
                    try:
                        matches = json.loads(path.read_text()) == value
                    except (OSError, ValueError):
                        matches = False
                    if not matches:
                        self._atomic_write(path, json.dumps(value, indent=2) + '\n')
                        repaired.append((path.parent, row['run_id']))
            c.commit()
        from ..report import finalize_manifest
        for directory, run_id in repaired:
            finalize_manifest(directory, run_id)

    def recover_expired(self, ttl=10):
        cutoff = time.time() - ttl; recovered = []
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            rows = c.execute('SELECT name,run_id FROM leases WHERE heartbeat<?', (cutoff,)).fetchall()
            for row in rows:
                deleted = c.execute('DELETE FROM leases WHERE name=? AND run_id=? AND heartbeat<?',
                                    (row['name'], row['run_id'], cutoff)).rowcount
                if deleted and self._exists(c, row['run_id']):
                    state = c.execute('SELECT state FROM runs WHERE run_id=?', (row['run_id'],)).fetchone()[0]
                    if state not in TERMINAL:
                        try:
                            execution_mode = json.loads(c.execute('SELECT profile FROM runs WHERE run_id=?',
                                                                  (row['run_id'],)).fetchone()['profile']).get('mode', 'fake')
                        except (TypeError, ValueError, AttributeError):
                            execution_mode = 'fake'
                        report = {'schema_version': 'prosperolight-test-lab-report/1', 'execution_mode': execution_mode,
                                  'run_id': row['run_id'], 'verdict': 'INCONCLUSIVE',
                                  'stage': 'RECOVERY', 'reason_code': 'COORDINATOR_RESTART',
                                  'reason': 'Unsafe work is not replayed after lease expiry',
                                  'artifacts': ['profile.json', 'report.json'], 'stage_results': {}}
                        pending = self.report_artifact(row['run_id'], report)
                        c.execute('UPDATE runs SET state="INCONCLUSIVE",stage="RECOVERY",verdict="INCONCLUSIVE",'
                                  'reason_code="COORDINATOR_RESTART",reason="Unsafe work is not replayed after lease expiry",'
                                  'report=?,updated=? '
                                  'WHERE run_id=?', (json.dumps(report, indent=2), time.time(), row['run_id']))
                        self._event(c, row['run_id'], 'INCONCLUSIVE', 'RECOVERY', 'coordinator_restarted',
                                    {'reason_code': 'COORDINATOR_RESTART'})
                        recovered.append((row['run_id'], pending))
            c.commit()
        for run_id, pending in recovered: self.publish_report(run_id, pending)
        return [run_id for run_id, _ in recovered]

    @contextmanager
    def connect(self):
        c = sqlite3.connect(self.db, timeout=5, isolation_level=None)
        c.row_factory = sqlite3.Row
        c.execute('PRAGMA foreign_keys=ON')
        c.execute('PRAGMA busy_timeout=5000')
        try:
            yield c
        finally:
            c.close()

    @staticmethod
    def _exists(c, run_id):
        return c.execute('SELECT 1 FROM runs WHERE run_id=?', (run_id,)).fetchone() is not None

    def _event(self, c, run_id, state, stage, kind, data):
        c.execute('INSERT INTO events(run_id,at,state,stage,kind,data) VALUES(?,?,?,?,?,?)',
                  (run_id, time.time(), state, stage, kind, json.dumps(data, separators=(',', ':'))))

    def create(self, suite, profile, mode='fake'):
        run_id = str(uuid.uuid4()); now = time.time()
        directory = self.artifacts / run_id
        directory.mkdir(mode=0o700, exist_ok=False)
        self._atomic_write(directory / 'profile.json', json.dumps(profile, indent=2) + '\n')
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            c.execute('INSERT INTO runs VALUES(?,?,?,?,?,?,?,?,?,?)',
                      (run_id, now, now, 'CREATED', 'CREATED', 'INCONCLUSIVE',
                       json.dumps({'suite': suite, 'profile': profile, 'mode': mode}), None, None, None))
            self._event(c, run_id, 'CREATED', 'CREATED', 'created', {'suite': suite})
            c.commit()
        return self.get(run_id)

    def transition(self, run_id, state, stage, kind, data=None, verdict=None, reason_code=None, reason=None, report=None):
        data = data or {}; now = time.time()
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            row = c.execute('SELECT state FROM runs WHERE run_id=?', (run_id,)).fetchone()
            if not row: c.rollback(); return None
            if row['state'] in TERMINAL:
                c.rollback(); raise RuntimeError('terminal run state is immutable')
            if state in TERMINAL:
                c.rollback(); raise RuntimeError('terminal transitions must atomically finish and release the lease')
            if state != row['state'] and state not in NEXT.get(row['state'], set()):
                c.rollback(); raise RuntimeError('invalid run transition: ' + row['state'] + ' -> ' + state)
            c.execute('UPDATE runs SET updated=?,state=?,stage=?,verdict=COALESCE(?,verdict),'
                      'reason_code=?,reason=?,report=COALESCE(?,report) WHERE run_id=?',
                      (now, state, stage, verdict, reason_code, reason,
                       json.dumps(report, indent=2) if report is not None else None, run_id))
            self._event(c, run_id, state, stage, kind, data)
            c.commit()
        return self.get(run_id)

    def finish(self, run_id, state, stage, kind, verdict, reason_code=None, reason=None, report=None, expected_state=None):
        if state not in TERMINAL: raise ValueError('finish requires a terminal state')
        if report is not None:
            from ..verdicts import apply_classification
            verdict = apply_classification(report, state)['overall_verdict']
        now = time.time()
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            row = c.execute('SELECT state FROM runs WHERE run_id=?', (run_id,)).fetchone()
            if not row: c.rollback(); return None
            if row['state'] in TERMINAL or (expected_state and row['state'] != expected_state):
                c.commit(); return self.get(run_id)
            if state == 'COMPLETED' and row['state'] != 'ANALYZE':
                c.rollback(); raise RuntimeError('a run can complete only after analysis ACK')
            c.execute('UPDATE runs SET updated=?,state=?,stage=?,verdict=?,reason_code=?,reason=?,report=? WHERE run_id=?',
                      (now, state, stage, verdict, reason_code, reason,
                       json.dumps(report, indent=2) if report is not None else None, run_id))
            self._event(c, run_id, state, stage, kind,
                        {'reason_code': reason_code, 'reason': reason} if reason_code or reason else {})
            c.execute('DELETE FROM leases WHERE run_id=?', (run_id,))
            c.commit()
        return self.get(run_id)

    def claim(self, run_id):
        """Claim CREATED and the hardware lease in the same transaction."""
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            row = c.execute('SELECT state FROM runs WHERE run_id=?', (run_id,)).fetchone()
            if not row or row['state'] != 'CREATED':
                c.rollback(); return 'active'
            owner = c.execute('SELECT run_id FROM leases WHERE name="ps5"').fetchone()
            if owner:
                c.rollback(); return 'active' if owner['run_id'] == run_id else 'busy'
            now = time.time()
            c.execute('INSERT INTO leases VALUES(?,?,?,?)', ('ps5', run_id, now, now))
            c.execute('UPDATE runs SET state="PREFLIGHT",stage="PREFLIGHT",updated=? WHERE run_id=?', (now, run_id))
            self._event(c, run_id, 'PREFLIGHT', 'PREFLIGHT', 'claimed', {})
            c.commit(); return 'claimed'

    def request_stop(self, run_id):
        with self.connect() as c:
            c.execute('INSERT OR IGNORE INTO cancellations VALUES(?)', (run_id,))

    def stop_requested(self, run_id):
        with self.connect() as c:
            return c.execute('SELECT 1 FROM cancellations WHERE run_id=?', (run_id,)).fetchone() is not None

    def acquire(self, run_id, name='ps5'):
        with self.connect() as c:
            try:
                now = time.time()
                c.execute('INSERT INTO leases(name,run_id,acquired,heartbeat) VALUES(?,?,?,?)',
                          (name, run_id, now, now))
                return True
            except sqlite3.IntegrityError:
                return False

    def heartbeat(self, run_id, name='ps5'):
        with self.connect() as c:
            c.execute('UPDATE leases SET heartbeat=? WHERE name=? AND run_id=?', (time.time(), name, run_id))
            return c.total_changes == 1

    def release(self, run_id, name='ps5'):
        with self.connect() as c:
            c.execute('DELETE FROM leases WHERE name=? AND run_id=?', (name, run_id))

    def get(self, run_id):
        with self.connect() as c:
            row = c.execute('SELECT * FROM runs WHERE run_id=?', (run_id,)).fetchone()
        if not row: return None
        result = dict(row)
        result['profile'] = json.loads(result['profile'])
        result['report'] = json.loads(result['report']) if result['report'] else None
        if result['report']:
            from ..verdicts import classify
            result.update(classify(result['report'], result['state']))
        return result

    def list_events(self, run_id, after=0):
        with self.connect() as c:
            if not self._exists(c, run_id): return None
            rows = c.execute('SELECT seq,at,state,stage,kind,data FROM events WHERE run_id=? AND seq>? ORDER BY seq',
                             (run_id, after)).fetchall()
        return [{**dict(r), 'data': json.loads(r['data'])} for r in rows]

    def files(self, run_id):
        run = self.get(run_id)
        if not run: return None
        root = (self.artifacts / run_id).resolve()
        if root.parent != self.artifacts: raise RuntimeError('unsafe run artifact path')
        return [{'name': p.relative_to(root).as_posix(), 'bytes': p.stat().st_size}
                for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()
                and not p.name.startswith('.') and not p.name.endswith(('.tmp', '.pending'))]

    @staticmethod
    def _atomic_write(path, value):
        path = Path(path); temporary = path.with_name(path.name + '.tmp')
        temporary.write_text(value, encoding='utf-8')
        os.replace(temporary, path)

    def report_artifact(self, run_id, report):
        from ..verdicts import apply_classification
        apply_classification(report)
        path = self.artifacts / run_id / 'report.json'
        pending = path.with_name(path.name + '.' + str(uuid.uuid4()) + '.pending')
        self._atomic_write(pending, json.dumps(report, indent=2) + '\n')
        return pending

    def publish_report(self, run_id, pending):
        path = self.artifacts / run_id / 'report.json'
        if Path(pending).parent != path.parent or not Path(pending).name.startswith('report.json.') or not Path(pending).name.endswith('.pending'):
            raise ValueError('unsafe pending report path')
        with self.connect() as c:
            c.execute('BEGIN IMMEDIATE')
            row = c.execute('SELECT report FROM runs WHERE run_id=?', (run_id,)).fetchone()
            if not row or not row['report'] or json.loads(row['report']) != json.loads(Path(pending).read_text()):
                Path(pending).unlink(missing_ok=True); c.rollback(); return
            os.replace(pending, path)
            c.commit()
        if (path.parent / 'analysis.json').is_file():
            self._atomic_write(path.parent / 'events.jsonl', ''.join(
                json.dumps(event) + '\n' for event in self.list_events(run_id) or []))
            from ..report import finalize_manifest
            finalize_manifest(path.parent, run_id)

    def artifact_path(self, run_id, name):
        if not self.get(run_id) or any(part.startswith('.') or part.endswith(('.tmp', '.pending')) for part in Path(name).parts):
            return None
        root = (self.artifacts / run_id).resolve()
        raw = root / name
        if any(p.is_symlink() for p in (raw, *raw.parents) if p != self.root):
            return None
        path = raw.resolve()
        if root.parent != self.artifacts or root not in path.parents or not path.is_file() or path.is_symlink():
            return None
        return path
