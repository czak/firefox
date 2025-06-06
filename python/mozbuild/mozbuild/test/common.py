# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil

import mozpack.path as mozpath
from buildconfig import topsrcdir
from mach.logging import LoggingManager

from mozbuild.util import ReadOnlyDict

# By including this module, tests get structured logging.
log_manager = LoggingManager()
log_manager.add_terminal_logging()


def prepare_tmp_topsrcdir(path):
    for p in (
        "build/autoconf/config.guess",
        "build/autoconf/config.sub",
        "build/moz.configure/checks.configure",
        "build/moz.configure/init.configure",
        "build/moz.configure/util.configure",
    ):
        file_path = os.path.join(path, p)
        os.makedirs(os.path.dirname(file_path), exist_ok=True)
        shutil.copy(os.path.join(topsrcdir, p), file_path)


# mozconfig is not a reusable type (it's actually a module) so, we
# have to mock it.
class MockConfig:
    def __init__(
        self,
        topsrcdir="/path/to/topsrcdir",
        extra_substs={},
        error_is_fatal=True,
    ):
        self.topsrcdir = mozpath.abspath(topsrcdir)
        self.topobjdir = mozpath.abspath("/path/to/topobjdir")

        self.substs = ReadOnlyDict(
            {
                "MOZ_FOO": "foo",
                "MOZ_BAR": "bar",
                "MOZ_TRUE": "1",
                "MOZ_FALSE": "",
                "DLL_PREFIX": "lib",
                "DLL_SUFFIX": ".so",
            },
            **extra_substs
        )

        self.defines = self.substs

        self.lib_prefix = "lib"
        self.lib_suffix = ".a"
        self.import_prefix = "lib"
        self.import_suffix = ".so"
        self.dll_prefix = "lib"
        self.dll_suffix = ".so"
        self.error_is_fatal = error_is_fatal
