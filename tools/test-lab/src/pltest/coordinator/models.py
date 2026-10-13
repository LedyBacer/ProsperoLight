# ps5-native-app-boilerplate - versioned Test Lab coordinator API models.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from typing import Any, Literal
import json
from pydantic import BaseModel, ConfigDict, Field, model_validator


class RunCreate(BaseModel):
    model_config = ConfigDict(extra='forbid')
    schema_version: Literal['prosperolight-test-lab-api/1'] = 'prosperolight-test-lab-api/1'
    suite: str = Field(pattern=r'^[a-z0-9][a-z0-9-]{0,63}$')
    profile: dict[str, Any]
    mode: str = 'fake'

    @model_validator(mode='after')
    def bounded_profile(self):
        if self.mode not in {'fake', 'real'}:
            raise ValueError('mode must be fake or real')
        try: json.dumps(self.profile, allow_nan=False)
        except (TypeError, ValueError) as exc: raise ValueError('profile must contain finite JSON values') from exc
        forbidden = ('password', 'passwd', 'token', 'secret', 'authorization', 'private_key', 'rikey', 'certificate')
        nodes = 0
        def check(value, depth=0):
            nonlocal nodes
            nodes += 1
            if depth > 8 or nodes > 512:
                raise ValueError('profile exceeds structure limits')
            if isinstance(value, dict):
                if any(any(term in str(key).casefold() for term in forbidden) for key in value):
                    raise ValueError('credentials are not allowed in profiles')
                for item in value.values(): check(item, depth + 1)
            elif isinstance(value, list):
                for item in value: check(item, depth + 1)
        check(self.profile)
        if len(self.profile_json().encode('utf-8')) > 16 * 1024:
            raise ValueError('profile exceeds 16 KiB')
        if self.profile.get('schema') != 'prosperolight-test/1':
            raise ValueError('unsupported profile schema')
        timing = self.profile.get('timing', {})
        if not isinstance(timing, dict) or type(timing.get('hard_seconds', 120)) is not int \
                or not 1 <= timing.get('hard_seconds', 120) <= 1800:
            raise ValueError('invalid profile hard deadline')
        stage_timeout = timing.get('stage_timeout_seconds', timing.get('hard_seconds', 120))
        if type(stage_timeout) is not int or not 1 <= stage_timeout <= 1800:
            raise ValueError('invalid per-stage timeout')
        return self

    def profile_json(self):
        return self.model_dump_json()


class RunView(BaseModel):
    model_config = ConfigDict(extra='forbid')
    schema_version: Literal['prosperolight-test-lab-api/1'] = 'prosperolight-test-lab-api/1'
    run_id: str
    state: str
    stage: str
    verdict: str
    execution_state: str | None = None
    functional_verdict: str | None = None
    measurement_validity: str | None = None
    performance_delta: str | None = None
    overall_verdict: str | None = None
    reason_code: str | None = None
    report: dict[str, Any] | None = None


class CompareRequest(BaseModel):
    model_config = ConfigDict(extra='forbid')
    schema_version: Literal['prosperolight-test-lab-api/1'] = 'prosperolight-test-lab-api/1'
    baseline: str | None = Field(default=None, pattern=r'^[0-9a-fA-F-]{36}$')
    candidate: str | None = Field(default=None, pattern=r'^[0-9a-fA-F-]{36}$')
    pairs: list[tuple[str, str]] = Field(default_factory=list, max_length=20)
    calibration: bool = False
    calibration_pairs: list[tuple[str, str]] | None = Field(default=None, max_length=20)
    screening: dict[str, float | int | dict[str, dict[str, float | int]]] | None = None

    @model_validator(mode='after')
    def identities(self):
        import uuid
        if bool(self.pairs) == bool(self.baseline or self.candidate):
            raise ValueError('provide either baseline/candidate or pairs')
        if self.calibration and self.calibration_pairs is not None:
            raise ValueError('A/A cannot reference another calibration')
        values = (self.pairs or [(self.baseline, self.candidate)]) + (self.calibration_pairs or [])
        for a, b in values:
            for value in (a, b):
                if not isinstance(value, str) or str(uuid.UUID(value)) != value:
                    raise ValueError('expected canonical run UUID')
        return self
