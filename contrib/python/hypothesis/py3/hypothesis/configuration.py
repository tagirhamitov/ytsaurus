# This file is part of Hypothesis, which may be found at
# https://github.com/HypothesisWorks/hypothesis/
#
# Copyright the Hypothesis Authors.
# Individual contributors are listed in AUTHORS.rst and the git log.
#
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at https://mozilla.org/MPL/2.0/.

import os
import warnings
from pathlib import Path

import _hypothesis_globals

from hypothesis.errors import HypothesisSideeffectWarning

__hypothesis_home_directory_default = Path.cwd() / ".hypothesis"

__hypothesis_home_directory = None


def set_hypothesis_home_dir(directory):
    global __hypothesis_home_directory
    __hypothesis_home_directory = None if directory is None else Path(directory)


def storage_directory(*names, intent_to_write=True):
    if intent_to_write:
        check_sideeffect_during_initialization(
            "accessing storage for {}", "/".join(names)
        )

    global __hypothesis_home_directory
    if not __hypothesis_home_directory:
        if where := os.getenv("HYPOTHESIS_STORAGE_DIRECTORY"):
            __hypothesis_home_directory = Path(where)
    if not __hypothesis_home_directory:
        __hypothesis_home_directory = __hypothesis_home_directory_default
    return __hypothesis_home_directory.joinpath(*names)


_first_postinit_what = None


def check_sideeffect_during_initialization(
    what: str, *fmt_args: object, extra: str = ""
) -> None:
    """Called from locations that should not be executed during initialization, for example
    touching disk or materializing lazy/deferred strategies from plugins. If initialization
    is in progress, a warning is emitted.

    Note that computing the repr can take nontrivial time or memory, so we avoid doing so
    unless (and until) we're actually emitting the warning.
    """
    global _first_postinit_what
    # This is not a particularly hot path, but neither is it doing productive work, so we want to
    # minimize the cost by returning immediately. The drawback is that we require
    # notice_initialization_restarted() to be called if in_initialization changes away from zero.
    if _first_postinit_what is not None:
        return
    elif _hypothesis_globals.in_initialization:
        # Note: -Werror is insufficient under pytest, as doesn't take effect until
        # test session start.
        msg = what.format(*fmt_args)
        warnings.warn(
            f"Slow code in plugin: avoid {msg} at import time!  Set PYTHONWARNINGS=error "
            "to get a traceback and show which plugin is responsible." + extra,
            HypothesisSideeffectWarning,
            stacklevel=3,
        )
    else:
        _first_postinit_what = (what, fmt_args)


def notice_initialization_restarted(*, warn: bool = True) -> None:
    """Reset _first_postinit_what, so that we don't think we're in post-init. Additionally, if it
    was set that means that there has been a sideeffect that we haven't warned about, so do that
    now (the warning text will be correct, and we also hint that the stacktrace can be improved).
    """
    global _first_postinit_what
    if _first_postinit_what is not None:
        what, *fmt_args = _first_postinit_what
        _first_postinit_what = None
        if warn:
            check_sideeffect_during_initialization(
                what,
                *fmt_args,
                extra=" Additionally, set HYPOTHESIS_EXTEND_INITIALIZATION=1 to pinpoint the exact location.",
            )
