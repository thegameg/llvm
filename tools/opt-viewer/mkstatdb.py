#!/usr/bin/env python2.7

desc = '''Generate statistics about optimization records from the YAML files
generated with -fsave-optimization-record and -fdiagnostics-show-hotness.

The tools requires PyYAML and sqlite3 Python packages.'''

import os
import fnmatch
import optrecord
import argparse
import operator
import sqlite3
import functools
from collections import defaultdict
from multiprocessing import cpu_count, Pool
import time

# Define what metrics we are interested in as statistics.
def want_metric(metric_name):
    return 'Cost' in metric_name or 'Num' in metric_name

# SQLite doesn't seem to like '-' and '/'.
def normalize_metric(metric_name):
    return str(metric_name).replace('-','').replace('/','')

def create_table(db, table_name, metrics, is_verbose):
    c = db.cursor();
    create_query = '''CREATE TABLE {}
                      (file TEXT, function TEXT'''.format(table_name)
    for metric in metrics:
        create_query += ', {} number(8)'.format(normalize_metric(metric))
    create_query += ', PRIMARY KEY (file, function))'
    if is_verbose:
        print(create_query)
    c.execute(create_query)

def populate_table(db, table_name, functions, num_metrics, is_verbose):
    c = db.cursor();

    query_end = ''
    for i in range(0, num_metrics):
        query_end += ', ' + 'null'
    query_end += ')'

    for file, function in functions:
        insert_query = """INSERT INTO {}
                          VALUES ('{}', '{}'{}""" \
                       .format(table_name, file, function, query_end)
        if is_verbose:
            print(insert_query)
        c.execute(insert_query)

def insert_values(db, table_name, file_remarks, is_verbose):
    c = db.cursor();
    for file in file_remarks:
        line = file_remarks[file]
        for remark_list in line.itervalues():
            for remark in remark_list:
                for arg_tuple in remark.Args:
                    name, value = arg_tuple[0]
                    if want_metric(name):
                        metric_name = normalize_metric('{}{}{}'
                                .format(remark.Pass, remark.Name, name))
                        update_query = """UPDATE {} SET {} = {}
                                          WHERE file = '{}'
                                          AND function = '{}'""" \
                                       .format(table_name, metric_name, value,
                                               remark.File, remark.Function)
                        if is_verbose:
                            print(update_query)
                        c.execute(update_query)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument(
        'yaml_dirs_or_files',
        nargs='+',
        help='List of optimization record files or directories searched '
             'for optimization record files.')
    parser.add_argument(
        '--output',
        '-o',
        default='db.sqlite',
        type=str,
        help='Output file')
    parser.add_argument(
        '--table',
        default='statistics',
        type=str,
        help='Table name')
    parser.add_argument(
        '--jobs',
        '-j',
        default=cpu_count(),
        type=int,
        help='Max job count (defaults to current CPU count)')
    parser.add_argument(
        '--no-progress-indicator',
        '-n',
        action='store_true',
        default=False,
        help='Do not display any indicator of how many YAML files were read '
             'or imported in the SQLite DB.')
    parser.add_argument(
        '--verbose',
        '-v',
        action='store_true',
        default=False,
        help='Display SQL queries before they are executed')
    args = parser.parse_args()

    print_progress = not args.no_progress_indicator

    yaml_files = optrecord.find_opt_files(args.yaml_dirs_or_files)
    if not yaml_files:
        parser.error("No *.opt.yaml files found")
        sys.exit(1)

    all_remarks, file_remarks, _ = \
        optrecord.gather_results(yaml_files, args.jobs, print_progress)

    metrics = set()
    functions = set()
    for remark in all_remarks.itervalues():
        for tuple_arg in remark.Args:
            name, _ = tuple_arg[0]
            if want_metric(name):
                metric_name = '{}{}{}'.format(remark.Pass, remark.Name, name)
                metrics.add(metric_name)
                functions.add((remark.File, remark.Function))

    db = sqlite3.connect(args.output)

    # In order to avoid using more memory follow the following steps:
    # 1. Create an empty table:
    # | File | Function | Metric0 | Metric1 | Metric2 | etc.
    create_table(db, args.table, metrics, args.verbose)
    # 2. Populate the table with all the functions in all the files,
    #    with null metric values.
    # | File           | Function | Metric0 | Metric1 | Metric2 | etc.
    # | file0.opt.yaml | fun0     | null    | null    | null    | etc.
    # | file1.opt.yaml | fun8     | null    | null    | null    | etc.
    populate_table(db, args.table, functions, len(metrics), args.verbose)
    # 3. Insert the values of each metric by updating the rows.
    # | File           | Function | Metric0 | Metric1 | Metric2 | etc.
    # | file0.opt.yaml | fun0     | 34      | 4       | 5       | etc.
    # | file1.opt.yaml | fun8     | null    | 4       | 6       | etc.
    insert_values(db, args.table, file_remarks, args.verbose)

    db.commit()
    db.close()
