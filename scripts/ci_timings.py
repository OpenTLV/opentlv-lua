# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

"""Record hosted job durations; compare only matching event and change cohorts."""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import time

from ci_support import api, require, write as write_json

METADATA_ATTEMPTS = 18
METADATA_RETRY_SECONDS = 10


def seconds(start, end):
    return (datetime.fromisoformat(end.replace('Z', '+00:00')) -
            datetime.fromisoformat(start.replace('Z', '+00:00'))).total_seconds()


def summarize(jobs):
    completed = [job for job in jobs if job.get('started_at') and job.get('completed_at')
                 and job.get('conclusion') != 'skipped']
    require(completed, 'No completed jobs to measure')
    durations = [{key: job[key] for key in ('name', 'conclusion', 'started_at', 'completed_at')}
                 | {'seconds': seconds(job['started_at'], job['completed_at'])} for job in completed]
    for job, duration in zip(completed, durations):
        duration.update({key: job[key] for key in
                         ('timing_source', 'api_status', 'api_completed_at') if key in job})
    return {'job_count': len(durations), 'job_seconds': sum(job['seconds'] for job in durations),
            'execution_span_seconds': seconds(min(j['started_at'] for j in completed),
                                              max(j['completed_at'] for j in completed)),
            'jobs': durations}


def fetch_jobs(repo, run, attempt):
    jobs = []
    page = 1
    while True:
        data = api(f'repos/{repo}/actions/runs/{run}/attempts/{attempt}/jobs?per_page=100&page={page}')
        jobs.extend(data['jobs'])
        if len(data['jobs']) < 100:
            return jobs
        page += 1


def step_timing(job, run, attempt):
    """Recover telemetry from a terminal runner step, never a job conclusion."""
    if (job.get('status') != 'in_progress' or job.get('conclusion') is not None or
            job.get('completed_at') is not None or not job.get('started_at')):
        return job
    steps = job.get('steps', [])
    terminal = [step for step in steps if step.get('name') == 'Complete job']
    if len(terminal) != 1 or terminal[0].get('conclusion') != 'success':
        return job
    numbers = [step.get('number') for step in steps]
    if (not all(type(number) is int and number > 0 for number in numbers) or
            len(set(numbers)) != len(numbers) or terminal[0]['number'] != max(numbers)):
        return job
    end = terminal[0].get('completed_at')
    try:
        if not end or seconds(job['started_at'], end) < 0:
            return job
        for step in steps:
            if step.get('status') != 'completed' or not step.get('conclusion'):
                return job
            if step['conclusion'] == 'skipped':
                continue
            if (seconds(step['started_at'], step['completed_at']) < 0 or
                    seconds(step['completed_at'], end) < 0):
                return job
    except (KeyError, TypeError, ValueError, AttributeError):
        return job
    require(type(job.get('id')) is int and job['id'] > 0 and
            str(job.get('run_id')) == str(run) and str(job.get('run_attempt')) == str(attempt),
            'Terminal-step timing requires the same run and attempt')
    # Preserve the API's null conclusion; a successful cleanup step does not
    # prove that the job succeeded. This record is consumed only by timings.
    return job | {'completed_at': end, 'timing_source': 'completed_steps',
                  'api_status': job['status'], 'api_completed_at': job.get('completed_at')}


def measure(repo, run, attempt, scope):
    # A dependent job can start before the Jobs API exposes completion metadata.
    # Refresh the same attempt instead of accepting an empty or partial Lua report.
    for retry in range(METADATA_ATTEMPTS):
        selected = fetch_jobs(repo, run, attempt)
        available_names = [job['name'] for job in selected]
        if scope == 'lua':
            gates = [job for job in selected if job['name'].endswith(' / Lua quality gate')]
            require(len(gates) <= 1, 'Expected one Lua gate in this run')
            if gates:
                prefix = gates[0]['name'].removesuffix(' / Lua quality gate') + ' / '
                selected = [job for job in selected if job['name'].startswith(prefix)]
            else:
                selected = []
        else:
            # Main/Release collection must also wait for every prerequisite's
            # metadata, excluding the collectors themselves (including nested Full).
            selected = [job for job in selected if not job['name'].endswith(' timings')]
        selected = [step_timing(job, run, attempt) for job in selected]
        incomplete = [job for job in selected if not (
            job.get('conclusion') == 'skipped' or
            job.get('timing_source') == 'completed_steps' or
            (job.get('status') == 'completed' and job.get('conclusion') and
             job.get('started_at') and job.get('completed_at')))]
        ready = bool(selected) and not incomplete
        measurable = any(job.get('started_at') and job.get('completed_at') and
                         job.get('conclusion') != 'skipped' for job in selected)
        if ready and measurable:
            return summarize(selected)
        if not selected:
            pending = 'No jobs selected; available job names: ' + json.dumps(available_names)
        elif incomplete:
            pending = 'Incomplete jobs: ' + json.dumps([
                {key: job.get(key) for key in
                 ('id', 'name', 'status', 'conclusion', 'started_at', 'completed_at')}
                for job in incomplete])
        else:
            pending = 'No non-skipped jobs with both timestamps'
        context = f'{repo} run {run} attempt {attempt} scope {scope}'
        print(f'Job timing metadata is not ready ({retry + 1}/{METADATA_ATTEMPTS}; {context}). '
              + pending, flush=True)
        if retry < METADATA_ATTEMPTS - 1:
            time.sleep(METADATA_RETRY_SECONDS)
    raise ValueError(f'Job timing metadata remained incomplete after {METADATA_ATTEMPTS} attempts '
                     f'({context}): {pending}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--scope', choices=('lua', 'run'), default='run')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        report = {'schema': 1, 'note': baseline['note'],
                  'runs': [{key: value for key, value in run.items() if key != 'jobs'}
                           | summarize(run['jobs']) for run in baseline['runs']]}
    else:
        repo, run, attempt = (os.environ[key] for key in
                              ('GITHUB_REPOSITORY', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT'))
        report = {'schema': 1, 'repository': repo, 'commit': os.environ['GITHUB_SHA'],
                  'event': os.environ['GITHUB_EVENT_NAME'], 'run_id': run, 'run_attempt': attempt,
                  'scope': args.scope,
                  'note': 'Execution span includes scheduling gaps between first start and last finish; excludes initial queue time.',
                  **measure(repo, run, attempt, args.scope)}
    write_json(args.output, report)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
