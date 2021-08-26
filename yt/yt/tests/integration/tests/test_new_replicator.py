from yt_env_setup import YTEnvSetup

from yt_commands import authors, set


##################################################################


class TestNewReplicator(YTEnvSetup):
    DELTA_MASTER_CONFIG = {
        "use_new_replicator": True,
    }

    @authors("gritukan")
    def test_simple(self):
        pass

    @authors("gritukan")
    def test_dynamic_config_change(self):
        set("//sys/@config/chunk_manager/max_heavy_columns", 10)


##################################################################


class TestNewReplicatorMulticell(TestNewReplicator):
    NUM_SECONDARY_MASTER_CELLS = 2
