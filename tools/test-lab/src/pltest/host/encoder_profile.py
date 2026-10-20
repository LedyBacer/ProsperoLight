# ps5-native-app-boilerplate - isolated, verified software-encoder app recipe.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import copy
import uuid

NAME = 'ProsperoLight x264 Lab'
IDENTITY = str(uuid.uuid5(uuid.NAMESPACE_URL, 'prosperolight-test-lab/x264-compatibility'))
OVERRIDES = {'encoder': 'software', 'sw_preset': 'veryfast',
             'sw_tune': 'zerolatency', 'min_threads': '4'}


def inventory(value):
    apps = value.get('apps')
    if (not isinstance(apps, list) or len(apps) > 1024 or
            any(not isinstance(a, dict) or not isinstance(a.get('uuid'), str) for a in apps) or
            len({a['uuid'] for a in apps}) != len(apps)):
        raise ValueError('HOST_APP_INVENTORY_UNSUPPORTED')
    return apps


async def software_profile(api, apply=False):
    """Never edit a global encoder setting or silently replace an existing app."""
    original = inventory(await api.request('GET', '/api/apps'))
    existing = [a for a in original if a.get('name') == NAME or a['uuid'] == IDENTITY]
    if existing:
        if len(existing) != 1 or existing[0].get('name') != NAME or existing[0]['uuid'] != IDENTITY or existing[0].get('config-overrides') != OVERRIDES:
            raise ValueError('HOST_LAB_PROFILE_CONFLICT')
        return {'state': 'PRESENT', 'name': NAME, 'overrides_verified': True,
                'global_configuration_changed': False}
    bases = [a for a in original if a.get('name') == 'Desktop']
    if len(bases) != 1:
        raise ValueError('HOST_DESKTOP_APP_AMBIGUOUS')
    app = copy.deepcopy(bases[0])
    app.pop('id', None); app.pop('index', None)
    app.update(name=NAME, uuid=IDENTITY, index=-1)
    app['config-overrides'] = dict(OVERRIDES)
    if not apply:
        return {'state': 'PLANNED', 'name': NAME, 'overrides': dict(OVERRIDES),
                'global_configuration_changed': False}
    await api.ensure_idle(asyncio.Event())
    response = await api.request('POST', '/api/apps', app)
    if response.get('status') is not True:
        raise RuntimeError('HOST_APP_CREATE_REJECTED')
    # This host sorts its app list on save. Restore all previous positions and
    # append only our app; read back complete original objects, not just names.
    response = await api.request('POST', '/api/apps/reorder',
                                 {'order': [a['uuid'] for a in original] + [IDENTITY]})
    if response.get('status') is not True:
        raise RuntimeError('HOST_APP_ORDER_RESTORE_UNCONFIRMED')
    current = {a['uuid']: a for a in inventory(await api.request('GET', '/api/apps'))}
    if any(current.get(a['uuid']) != a for a in original):
        raise RuntimeError('HOST_EXISTING_APP_CHANGED')
    created = current.get(IDENTITY) or {}
    if created.get('name') != NAME or created.get('config-overrides') != OVERRIDES:
        raise RuntimeError('HOST_LAB_PROFILE_READBACK_MISMATCH')
    return {'state': 'CREATED', 'name': NAME, 'overrides_verified': True,
            'existing_apps_unchanged': True, 'global_configuration_changed': False}
