import pytest

from yt_env_setup import YTEnvSetup
from yt_commands import *

##################################################################

class TestSchedulerReduceCommands(YTEnvSetup):
    NUM_MASTERS = 3
    NUM_HOLDERS = 5
    NUM_SCHEDULERS = 1

    def test_cat(self):
        create('table', '//tmp/in1')
        write(
            '//tmp/in1',
            [
                {'key': 0, 'value': 1},
                {'key': 1, 'value': 2},
                {'key': 2, 'value': 3},
                {'key': 3, 'value': 4}
            ],
            sorted_by = 'key')

        create('table', '//tmp/in2')
        write(
            '//tmp/in1',
            [
                {'key': -1,'value': 5},
                {'key': 1, 'value': 6},
                {'key': 2, 'value': 7},
                {'key': 4, 'value': 8}
            ],
            sorted_by = 'key')

        create('table', '//tmp/out')

        reduce(
            in_ = ['//tmp/in1', '//tmp/in2'],
            out = ['//tmp/out'],
            reducer = 'cat')
               
        assert read('//tmp/out') ==
            [
                {'key': -1,'value': 5},
                {'key': 0, 'value': 1},
                {'key': 1, 'value': 2},
                {'key': 1, 'value': 6},
                {'key': 2, 'value': 3},
                {'key': 2, 'value': 7},
                {'key': 3, 'value': 4},
                {'key': 4, 'value': 8}
            ]
        
