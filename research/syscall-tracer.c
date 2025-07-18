-*- mode:python -*-

# Copyright (c) 2004-2006 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt
#          Kevin Lim

import os, signal
import sys, time
import glob
from SCons.Script.SConscript import SConsEnvironment

Import('env')

env['DIFFOUT'] = File('diff-out')

# get the termcap from the environment
termcap = env['TERMCAP']

# Dict that accumulates lists of tests by category (quick, medium, long)
env.Tests = {}

def contents(node):
    return file(str(node)).read()

# functions to parse return value from scons Execute()... not the same
# as wait() etc., so python built-in os funcs don't work.
def signaled(status):
    return (status & 0x80) != 0;

def signum(status):
    return (status & 0x7f);

# List of signals that indicate that we should retry the test rather
# than consider it failed.
retry_signals = (signal.SIGTERM, signal.SIGKILL, signal.SIGINT,
                 signal.SIGQUIT, signal.SIGHUP)

# regular expressions of lines to ignore when diffing outputs
output_ignore_regexes = (
    '^command line:',		# for stdout file
    '^gem5 compiled ',		# for stderr file
    '^gem5 started ',		# for stderr file
    '^gem5 executing on ',	# for stderr file
    '^Simulation complete at',	# for stderr file
    '^Listening for',		# for stderr file
    'listening for remote gdb',	# for stderr file
    )

output_ignore_args = ' '.join(["-I '"+s+"'" for s in output_ignore_regexes])

output_ignore_args += ' --exclude=stats.txt --exclude=outdiff'

def run_test(target, source, env):
    """Check output from running test.

    Targets are as follows:
    target[0] : status

    Sources are:
    source[0] : gem5 binary
    source[1] : tests/run.py script
    source[2] : reference stats file

    """
    # make sure target files are all gone
    for t in target:
        if os.path.exists(t.abspath):
            env.Execute(Delete(t.abspath))

    tgt_dir = os.path.dirname(str(target[0]))

    # Base command for running test.  We mess around with indirectly
    # referring to files via SOURCES and TARGETS so that scons can mess
    # with paths all it wants to and we still get the right files.
    cmd = '${SOURCES[0]} -d %s -re ${SOURCES[1]} %s' % (tgt_dir, tgt_dir)

    # Prefix test run with batch job submission command if appropriate.
    # Batch command also supports timeout arg (in seconds, not minutes).
    timeout = 15 * 60 # used to be a param, probably should be again
    if env['BATCH']:
        cmd = '%s -t %d %s' % (env['BATCH_CMD'], timeout, cmd)

    # Create a default value for the status string, changed as needed
    # based on the status.
    status_str = "passed."

    pre_exec_time = time.time()
    status = env.Execute(env.subst(cmd, target=target, source=source))
    if status == 0:
        # gem5 terminated normally.
        # Run diff on output & ref directories to find differences.
        # Exclude the stats file since we will use diff-out on that.

        # NFS file systems can be annoying and not have updated yet
        # wait until we see the file modified
        statsdiff = os.path.join(tgt_dir, 'statsdiff')
        m_time = 0
        nap = 0
        while m_time < pre_exec_time and nap < 10:
            try:
                m_time = os.stat(statsdiff).st_mtime
            except OSError:
                pass
            time.sleep(1)
            nap += 1

        outdiff = os.path.join(tgt_dir, 'outdiff')
        # tack 'true' on the end so scons doesn't report diff's
        # non-zero exit code as a build error
        diffcmd = 'diff -ubrs %s ${SOURCES[2].dir} %s > %s; true' \
                  % (output_ignore_args, tgt_dir, outdiff)
        env.Execute(env.subst(diffcmd, target=target, source=source))
        print "===== Output differences ====="
        print contents(outdiff)
        # Run diff-out on stats.txt file
        diffcmd = '$DIFFOUT ${SOURCES[2]} %s > %s' \
                  % (os.path.join(tgt_dir, 'stats.txt'), statsdiff)
        diffcmd = env.subst(diffcmd, target=target, source=source)
        diff_status = env.Execute(diffcmd, strfunction=None)
        # If there is a difference, change the status string to say so
        if diff_status != 0:
            status_str = "CHANGED!"
        print "===== Statistics differences ====="
        print contents(statsdiff)

    else: # gem5 exit status != 0
        # Consider it a failed test unless the exit status is 2
        status_str = "FAILED!"
        # gem5 did not terminate properly, so no need to check the output
        if signaled(status):
            print 'gem5 terminated with signal', signum(status)
            if signum(status) in retry_signals:
                # Consider the test incomplete; don't create a 'status' output.
                # Hand the return status to scons and let scons decide what
                # to do about it (typically terminate unless run with -k).
                return status
        elif status == 2:
            # The test was skipped, change the status string to say so
            status_str = "skipped."
        else:
            print 'gem5 exited with non-zero status', status
        # complete but failed execution (call to exit() with non-zero
        # status, SIGABORT due to assertion failure, etc.)... fall through
        # and generate FAILED status as if output comparison had failed

    # Generate status file contents based on exit status of gem5 and diff-out
    f = file(str(target[0]), 'w')
    print >>f, tgt_dir, status_str
    f.close()
    # done
    return 0

def run_test_string(target, source, env):
    return env.subst("Running test in ${TARGETS[0].dir}.",
                     target=target, source=source)

testAction = env.Action(run_test, run_test_string)

def print_test(target, source, env):
    # print the status with colours to make it easier to see what
    # passed and what failed
    line = contents(source[0])

    # split the line to words and get the last one
    words = line.split()
    status = words[-1]

    # if the test failed make it red, if it passed make it green, and
    # skip the punctuation
    if status == "FAILED!":
        status = termcap.Red + status[:-1] + termcap.Normal + status[-1]
    elif status == "CHANGED!":
        status = termcap.Yellow + status[:-1] + termcap.Normal + status[-1]
    elif status == "passed.":
        status = termcap.Green + status[:-1] + termcap.Normal + status[-1]
    elif status == "skipped.":
        status = termcap.Cyan + status[:-1] + termcap.Normal + status[-1]

    # put it back in the list and join with space
    words[-1] = status
    line = " ".join(words)

    print '***** ' + line
    return 0

printAction = env.Action(print_test, strfunction = None)

# Static vars for update_test:
# - long-winded message about ignored sources
ignore_msg = '''
Note: The following file(s) will not be copied.  New non-standard
      output files must be copied manually once before --update-ref will
      recognize them as outputs.  Otherwise they are assumed to be
      inputs and are ignored.
'''
# - reference files always needed
needed_files = set(['simout', 'simerr', 'stats.txt', 'config.ini'])
# - source files we always want to ignore
known_ignores = set(['status', 'outdiff', 'statsdiff'])

def update_test(target, source, env):
    """Update reference test outputs.

    Target is phony.  First two sources are the ref & new stats.txt file
    files, respectively.  We actually copy everything in the
    respective directories except the status & diff output files.

    """
    dest_dir = str(source[0].get_dir())
    src_dir = str(source[1].get_dir())
    dest_files = set(os.listdir(dest_dir))
    src_files = set(os.listdir(src_dir))
    # Copy all of the required files plus any existing dest files.
    wanted_files = needed_files | dest_files
    missing_files = wanted_files - src_files
    if len(missing_files) > 0:
        print "  WARNING: the following file(s) are missing " \
              "and will not be updated:"
        print "    ", " ,".join(missing_files)
    copy_files = wanted_files - missing_files
    warn_ignored_files = (src_files - copy_files) - known_ignores
    if len(warn_ignored_files) > 0:
        print ignore_msg,
        print "       ", ", ".join(warn_ignored_files)
    for f in copy_files:
        if f in dest_files:
            print "  Replacing file", f
            dest_files.remove(f)
        else:
            print "  Creating new file", f
        copyAction = Copy(os.path.join(dest_dir, f), os.path.join(src_dir, f))
        copyAction.strfunction = None
        env.Execute(copyAction)
    return 0

def update_test_string(target, source, env):
    return env.subst("Updating ${SOURCES[0].dir} from ${SOURCES[1].dir}",
                     target=target, source=source)

updateAction = env.Action(update_test, update_test_string)

def test_builder(env, ref_dir):
    """Define a test."""

    path = list(ref_dir.split('/'))

    # target path (where test output goes) consists of category, mode,
    # name, isa, opsys, and config (skips the 'ref' component)
    assert(path.pop(-4) == 'ref')
    tgt_dir = os.path.join(*path[-6:])

    # local closure for prepending target path to filename
    def tgt(f):
        return os.path.join(tgt_dir, f)

    ref_stats = os.path.join(ref_dir, 'stats.txt')
    new_stats = tgt('stats.txt')
    status_file = tgt('status')

    env.Command([status_file, new_stats],
                [env.M5Binary, 'run.py', ref_stats],
                testAction)

    # phony target to echo status
    if GetOption('update_ref'):
        p = env.Command(tgt('_update'),
                        [ref_stats, new_stats, status_file],
                        updateAction)
    else:
        p = env.Command(tgt('_print'), [status_file], printAction)

    env.AlwaysBuild(p)


# Figure out applicable configs based on build type
configs = []
if env['TARGET_ISA'] == 'alpha':
    configs += ['tsunami-simple-atomic',
                'tsunami-simple-timing',
                'tsunami-simple-atomic-dual',
                'tsunami-simple-timing-dual',
                'twosys-tsunami-simple-atomic',
                'tsunami-o3', 'tsunami-o3-dual',
                'tsunami-inorder',
                'tsunami-switcheroo-full']
if env['TARGET_ISA'] == 'sparc':
    configs += ['t1000-simple-atomic',
                't1000-simple-timing']
if env['TARGET_ISA'] == 'arm':
    configs += ['simple-atomic-dummychecker',
                'o3-timing-checker',
                'realview-simple-atomic',
                'realview-simple-atomic-dual',
                'realview-simple-timing',
                'realview-simple-timing-dual',
                'realview-o3',
                'realview-o3-checker',
                'realview-o3-dual',
                'realview-switcheroo-atomic',
                'realview-switcheroo-timing',
                'realview-switcheroo-o3',
                'realview-switcheroo-full']
if env['TARGET_ISA'] == 'x86':
    configs += ['pc-simple-atomic',
                'pc-simple-timing',
                'pc-o3-timing',
                'pc-switcheroo-full']

configs += ['simple-atomic', 'simple-timing', 'o3-timing', 'memtest',
            'simple-atomic-mp', 'simple-timing-mp', 'o3-timing-mp',
            'inorder-timing', 'rubytest', 'tgen-simple-mem',
            'tgen-dram-ctrl']

if env['PROTOCOL'] != 'None':
    if env['PROTOCOL'] == 'MI_example':
        configs += [c + "-ruby" for c in configs]
    else:
        configs = [c + "-ruby-" + env['PROTOCOL'] for c in configs]

src = Dir('.').srcdir
for config in configs:
    dirs = src.glob('*/*/*/ref/%s/*/%s' % (env['TARGET_ISA'], config))
    for d in dirs:
        d = str(d)
        if not os.path.exists(os.path.join(d, 'skip')):
            test_builder(env, d)
