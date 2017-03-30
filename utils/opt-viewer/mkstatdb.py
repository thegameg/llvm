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

def find_files(dir_or_file):
        if os.path.isfile(dir_or_file):
            return [dir_or_file]

        all = []
        for dir, subdirs, files in os.walk(dir_or_file):
            for file in files:
                if fnmatch.fnmatch(file, "*.opt.yaml"):
                    all.append( os.path.join(dir, file))
        return all

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument('yaml_dir')
    parser.add_argument(
        '--output',
        '-o',
        default='db.sqlite',
        type=str,
        help='Output file')
    parser.add_argument(
        '--jobs',
        '-j',
        default=cpu_count(),
        type=int,
        help='Max job count (defaults to current CPU count)')

    args = parser.parse_args()

    yaml_files = find_files(args.yaml_dir)
    if len(yaml_files) == 0 or not args.output:
        parser.print_help()
        sys.exit(1)

    if args.jobs == 1:
        pmap = map
    else:
        pool = Pool(processes=args.jobs)
        pmap = pool.map

    all_remarks, file_remarks, _ = optrecord.gather_results(pmap, yaml_files)
    byname = defaultdict(list)
    byfunction = defaultdict(functools.partial(defaultdict, list))
    for l in all_remarks.itervalues():
        for r in l:
            for i in range(0, len(r.Args)):
                for key, value in r.Args[i].iteritems():
                    if 'Cost' in key or 'Num' in key:
                        pass_name = r.Pass + r.Name + key
                        byname[pass_name].append((r, i, key))
                        byfunction[r.Function][pass_name].append((r, i, key))

    db = sqlite3.connect(args.output)
    c = db.cursor();
    create_query = '''CREATE TABLE statistics
                      (function TEXT'''
    for key in byname:
        create_query += ', ' + str(key).replace('-','').replace('/','') + ' number(8)'
    create_query += ', PRIMARY KEY (function))'
    print(create_query)
    c.execute(create_query)

    for function in byfunction:
        insert_query = "INSERT INTO statistics VALUES ('" + function + "'"
        for metric in byname:
            insert_query += ', ' + str(sum(int(remark.Args[index][key]) for remark, index, key in byfunction[function][metric]))

        insert_query += ')'
        print(insert_query)
        c.execute(insert_query)

    db.commit()
    db.close()
