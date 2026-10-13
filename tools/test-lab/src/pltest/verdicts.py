# ps5-native-app-boilerplate - canonical terminal classifications.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later


def classify(report, execution_state=None):
    """Keep execution, streaming health and evidence validity independent."""
    analysis = report.get('analysis') or {}
    prior = report.get('verdict', 'INCONCLUSIVE')
    functional = analysis.get('functional_verdict', report.get('functional_verdict',
                              prior if prior in ('PASS', 'FAIL') else 'INCONCLUSIVE'))
    validity = analysis.get('measurement_validity', report.get('measurement_validity', 'INCONCLUSIVE'))
    delta = analysis.get('performance_delta', report.get('performance_delta', 'NOT_COMPARABLE'))
    simulated = report.get('execution_mode') != 'real'
    if execution_state is None:
        execution_state = report.get('execution_state')
    if execution_state is None:
        execution_state = ('CANCELLED' if prior == 'CANCELLED' else
                           'FAILED' if prior == 'FAIL' or functional == 'FAIL' else
                           'INCONCLUSIVE' if report.get('reason_code') else 'COMPLETED')
    if execution_state == 'CANCELLED':
        overall = 'CANCELLED'
    elif execution_state == 'INCONCLUSIVE' or report.get('cleanup_errors'):
        overall = 'INCONCLUSIVE'
    elif functional == 'FAIL' or delta == 'REGRESSED':
        overall = 'FAIL'
    elif simulated:
        overall = 'SIMULATED' if functional == 'PASS' else 'INCONCLUSIVE'
    elif functional == 'PASS' and validity == 'VALID':
        overall = 'PASS'
    else:
        overall = 'INCONCLUSIVE'
    return dict(execution_state=execution_state, functional_verdict=functional,
                measurement_validity=validity, performance_delta=delta,
                overall_verdict=overall)


def apply_classification(report, execution_state=None):
    values = classify(report, execution_state)
    report.update(values)
    # Compatibility names now describe the same conclusion.
    report['verdict'] = report['summary_verdict'] = values['overall_verdict']
    return values
