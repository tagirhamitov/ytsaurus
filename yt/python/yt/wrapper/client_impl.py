# This file is auto-generate by yt/python/yt/wrapper/bin/generate_client_impl, please do not edit it manually!

from .cypress_commands import _KwargSentinelClass, _MapOrderSorted
from .client_helpers import initialize_client
from .client_state import ClientState
from . import client_api


class YtClient(ClientState):
    """Implements YT client."""

    def __init__(self, proxy=None, token=None, config=None):
        super(YtClient, self).__init__()
        initialize_client(self, proxy, token, config)

    def LocalFile(
            self,
            path,
            file_name=None, attributes=None):
        """
        Represents a local path of a file and its path in job's sandbox
        """
        return client_api.LocalFile(
            path,
            client=self,
            file_name=file_name, attributes=attributes)

    def PingTransaction(
            self,
            transaction, ping_period, ping_timeout,
            interrupt_on_failed=True):
        """
        Pinger for transaction.

        Pings transaction in background thread.

        """
        return client_api.PingTransaction(
            transaction, ping_period, ping_timeout,
            client=self,
            interrupt_on_failed=interrupt_on_failed)

    def TablePath(
            self,
            name,
            append=None, sorted_by=None, columns=None, exact_key=None, lower_key=None, upper_key=None,
            exact_index=None, start_index=None, end_index=None, ranges=None, schema=None, optimize_for=None,
            compression_codec=None, erasure_codec=None, foreign=None, rename_columns=None, simplify=None,
            attributes=None):
        """
        YPath descendant to be used in table commands.

        Supported attributes:

        * append -- append to table or overwrite.
        * columns -- list of string (column).
        * exact_key, lower_key, upper_key -- tuple of strings to identify range of rows.
        * exact_index, start_index, end_index -- tuple of indexes to identify range of rows.
        * ranges -- list of dicts, allows to specify arbitrary ranges on the table, see more details in the docs.
        * schema -- TableSchema (or list with column schemas -- deprecated), see     `static schema doc <https://yt.yandex-team.ru/docs/description/storage/static_schema.html#create>`_

        .. seealso:: `YPath in the docs <https://yt.yandex-team.ru/docs/description/common/ypath.html>`_

        """
        return client_api.TablePath(
            name,
            client=self,
            append=append, sorted_by=sorted_by, columns=columns, exact_key=exact_key, lower_key=lower_key,
            upper_key=upper_key, exact_index=exact_index, start_index=start_index, end_index=end_index,
            ranges=ranges, schema=schema, optimize_for=optimize_for, compression_codec=compression_codec,
            erasure_codec=erasure_codec, foreign=foreign, rename_columns=rename_columns, simplify=simplify,
            attributes=attributes)

    def TempTable(
            self,
            *args,
            **kwds):
        """
        Creates temporary table in given path with given prefix on scope enter and        removes it on scope exit.

        .. seealso:: :func:`create_temp_table <yt.wrapper.table_commands.create_temp_table>`

        """
        return client_api.TempTable(
            *args,
            client=self,
            **kwds)

    def Transaction(
            self,
            timeout=None, deadline=None, attributes=None, ping=None, interrupt_on_failed=True,
            transaction_id=None, ping_ancestor_transactions=None, type='master', acquire=None, ping_period=None,
            ping_timeout=None):
        """

        It is designed to be used by with_statement::

        with Transaction():
        ...
        lock("//home/my_node")
        ...
        with Transaction():
        ...
        yt.run_map(...)

        Caution: if you use this class then do not use directly methods \\*_transaction.

        :param bool acquire: commit/abort transaction in exit from with. By default False if new transaction is not started else True and false values are not allowed.
        :param bool ping: ping transaction in separate thread. By default True if acquire is also True else False.

        .. seealso:: `transactions in the docs <https://yt.yandex-team.ru/docs/description/storage/transactions.html>`_

        """
        return client_api.Transaction(
            client=self,
            timeout=timeout, deadline=deadline, attributes=attributes, ping=ping, interrupt_on_failed=interrupt_on_failed,
            transaction_id=transaction_id, ping_ancestor_transactions=ping_ancestor_transactions, type=type,
            acquire=acquire, ping_period=ping_period, ping_timeout=ping_timeout)

    def abort_job(
            self,
            job_id,
            interrupt_timeout=None):
        """
        Interrupts running job with preserved result.

        :param str job_id: job id.
        :param int interrupt_timeout: wait for interrupt before abort (in ms).

        """
        return client_api.abort_job(
            job_id,
            client=self,
            interrupt_timeout=interrupt_timeout)

    def abort_operation(
            self,
            operation,
            reason=None):
        """
        Aborts operation.

        Do nothing if operation is in final state.

        :param str operation: operation id.

        """
        return client_api.abort_operation(
            operation,
            client=self,
            reason=reason)

    def abort_query(
            self,
            query_id,
            message=None, stage=None):
        """
        Abort query.

        :param query_id: id of a query to abort
        :type query_id: str
        :param message: optional message to be shown in query abort error
        :type message: str or None
        :param stage: query tracker stage, defaults to "production"
        :type stage: str

        """
        return client_api.abort_query(
            query_id,
            client=self,
            message=message, stage=stage)

    def abort_transaction(
            self,
            transaction):
        """
        Aborts transaction. All changes will be lost.

        :param str transaction: transaction id.

        .. seealso:: `abort_tx in the docs <https://yt.yandex-team.ru/docs/api/commands.html#aborttx>`_

        """
        return client_api.abort_transaction(
            transaction,
            client=self)

    def add_maintenance(
            self,
            node_address, maintenance_type, comment):
        """
        Adds maintenance request for given node

        :param node_address: node address.
        :param maintenance_type: maintenance type. There are 5 maintenance types: ban, decommission, disable_scheduler_jobs,
        disable_write_sessions, disable_tablet_cells.
        :param comment: any string with length not larger than 512 characters.
        :return: unique (per node) maintenance id.

        """
        return client_api.add_maintenance(
            node_address, maintenance_type, comment,
            client=self)

    def add_member(
            self,
            member, group):
        """
        Adds member to Cypress node group.

        :param str member: member to add.
        :param str group: group to add member to.

        .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_

        """
        return client_api.add_member(
            member, group,
            client=self)

    def alter_table(
            self,
            path,
            schema=None, dynamic=None, upstream_replica_id=None):
        """
        Performs schema and other table meta information modifications.
        Applicable to static and dynamic tables.

        :param path: path to table
        :type path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param schema: new schema to set on table
        :param bool dynamic: dynamic
        :param str upstream_replica_id: upstream_replica_id

        """
        return client_api.alter_table(
            path,
            client=self,
            schema=schema, dynamic=dynamic, upstream_replica_id=upstream_replica_id)

    def alter_table_replica(
            self,
            replica_id,
            enabled=None, mode=None):
        """
        TODO
        """
        return client_api.alter_table_replica(
            replica_id,
            client=self,
            enabled=enabled, mode=mode)

    def balance_tablet_cells(
            self,
            bundle,
            tables=None, sync=False):
        """
        Reassign tablets evenly among tablet cells.

        :param str bundle: tablet cell bundle name.
        :param list tables: if None, all tablets of bundle will be moved. If specified,
        only tablets of `tables` will be moved.
        :param bool sync: wait for the command to finish.

        """
        return client_api.balance_tablet_cells(
            bundle,
            client=self,
            tables=tables, sync=sync)

    def batch_apply(
            self,
            function, data):
        """
        Applies function to each element from data in a batch mode and returns result.
        """
        return client_api.batch_apply(
            function, data,
            client=self)

    def check_permission(
            self,
            user, permission, path,
            format=None, read_from=None, cache_sticky_group_size=None, columns=None):
        """
        Checks permission for Cypress node.

        :param str user: user login.
        :param str permission: one of ["read", "write", "administer", "create", "use"].
        :return: permission in specified format (YSON by default).

        .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_

        """
        return client_api.check_permission(
            user, permission, path,
            client=self,
            format=format, read_from=read_from, cache_sticky_group_size=cache_sticky_group_size,
            columns=columns)

    def commit_transaction(
            self,
            transaction):
        """
        Saves all transaction changes.

        :param str transaction: transaction id.

        .. seealso:: `commit_tx in the docs <https://yt.yandex-team.ru/docs/api/commands.html#committx>`_

        """
        return client_api.commit_transaction(
            transaction,
            client=self)

    def complete_operation(
            self,
            operation):
        """
        Completes operation.

        Aborts all running and pending jobs.
        Preserves results of finished jobs.
        Does nothing if operation is in final state.

        :param str operation: operation id.

        """
        return client_api.complete_operation(
            operation,
            client=self)

    def concatenate(
            self,
            source_paths, destination_path):
        """
        Concatenates cypress nodes. This command applicable only to files and tables.

        :param source_paths: source paths.
        :type source_paths: list[str or :class:`YPath <yt.wrapper.ypath.YPath>`]
        :param destination_path: destination path.
        :type destination_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`

        """
        return client_api.concatenate(
            source_paths, destination_path,
            client=self)

    def copy(
            self,
            source_path, destination_path,
            recursive=None, force=None, ignore_existing=None, lock_existing=None, preserve_account=None,
            preserve_owner=None, preserve_acl=None, preserve_expiration_time=None, preserve_expiration_timeout=None,
            preserve_creation_time=None, preserve_modification_time=None, pessimistic_quota_check=None):
        """
        Copies Cypress node.

        :param source_path: source path.
        :type source_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param destination_path: destination path.
        :type destination_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool recursive: ``yt.wrapper.config["yamr_mode"]["create_recursive"]`` by default.
        :param bool ignore_existing: ignore existing.
        :param bool lock_existing: lock existing node.
        :param bool preserve_account: preserve account.
        :param bool preserve_owner: preserve owner.
        :param bool preserve_acl: preserve acl.
        :param bool preserve_expiration_time: preserve expiration time.
        :param bool preserve_expiration_timeout: preserve expiration timeout.
        :param bool preserve_creation_time: preserve creation time.
        :param bool preserve_modification_time: preserve modification time.
        :param bool force: force.
        :param bool pessimistic_quota_check: pessimistic quota check.

        .. seealso:: `copy in the docs <https://yt.yandex-team.ru/docs/api/commands.html#copy>`_

        """
        return client_api.copy(
            source_path, destination_path,
            client=self,
            recursive=recursive, force=force, ignore_existing=ignore_existing, lock_existing=lock_existing,
            preserve_account=preserve_account, preserve_owner=preserve_owner, preserve_acl=preserve_acl,
            preserve_expiration_time=preserve_expiration_time, preserve_expiration_timeout=preserve_expiration_timeout,
            preserve_creation_time=preserve_creation_time, preserve_modification_time=preserve_modification_time,
            pessimistic_quota_check=pessimistic_quota_check)

    def create(
            self,
            type,
            path=None, recursive=False, ignore_existing=False, lock_existing=None, force=None,
            attributes=None):
        """
        Creates Cypress node.

        :param str type: one of ["table", "file", "map_node", "list_node", ...].
        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool lock_existing: lock existing node.
        :param bool recursive: ``yt.wrapper.config["yamr_mode"]["create_recursive"]`` by default.
        :param dict attributes: attributes.

        .. seealso:: `create in the docs <https://yt.yandex-team.ru/docs/api/commands.html#create>`_

        """
        return client_api.create(
            type,
            client=self,
            path=path, recursive=recursive, ignore_existing=ignore_existing, lock_existing=lock_existing,
            force=force, attributes=attributes)

    def create_batch_client(
            self,
            raise_errors=False, max_batch_size=None, concurrency=None):
        """
        Creates client which supports batch executions.
        """
        return client_api.create_batch_client(
            client=self,
            raise_errors=raise_errors, max_batch_size=max_batch_size, concurrency=concurrency)

    def create_revision_parameter(
            self,
            path,
            transaction_id=None, revision=None):
        """
        Creates revision parameter of the path.

        :param str path: path.
        :rtype: dict

        """
        return client_api.create_revision_parameter(
            path,
            client=self,
            transaction_id=transaction_id, revision=revision)

    def create_table(
            self,
            *args,
            **kwargs):
        """
        Creates empty table. Shortcut for `create("table", ...)`.

        :param path: path to table.
        :type path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param bool recursive: create the path automatically,     ``yt.wrapper.config["yamr_mode"]["create_recursive"]`` by default.
        :param bool ignore_existing: do nothing if path already exists otherwise and option specified,
        otherwise if path exists and option is not specified
        then :class:`YtResponseError <yt.wrapper.errors.YtResponseError>` will be raised.
        :param dict attributes: attributes.

        """
        return client_api.create_table(
            *args,
            client=self,
            **kwargs)

    def create_table_backup(
            self,
            manifest,
            force=None, checkpoint_timestamp_delay=None):
        """
        Creates a consistent backup copy of a collection of tables.

        :param manifest: description of tables to be backed up.
        :type manifest: dict or :class:`BackupManifest`
        :param bool force: overwrite destination tables.

        """
        return client_api.create_table_backup(
            manifest,
            client=self,
            force=force, checkpoint_timestamp_delay=checkpoint_timestamp_delay)

    def create_temp_table(
            self,
            path=None, prefix=None, attributes=None, expiration_timeout=None):
        """
        Creates temporary table by given path with given prefix and return name.

        :param path: existing path, by default ``yt.wrapper.config["remote_temp_tables_directory"]``.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param str prefix: prefix of table name.
        :param int expiration_timeout: expiration timeout for newly created table, in ms.
        :return: name of result table.
        :rtype: str

        """
        return client_api.create_temp_table(
            client=self,
            path=path, prefix=prefix, attributes=attributes, expiration_timeout=expiration_timeout)

    def delete_rows(
            self,
            table, input_stream,
            atomicity=None, durability=None, format=None, raw=None, require_sync_replica=None):
        """
        Deletes rows with keys from input_stream from dynamic table.

        :param table: table to remove rows from.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param input_stream: python file-like object, string, list of strings.
        :param format: format of input data, ``yt.wrapper.config["tabular_data_format"]`` by default.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: if `raw` is specified stream with unparsed records (strings)
        in specified `format` is expected. Otherwise dicts or :class:`Record <yt.wrapper.yamr_record.Record>`
        are expected.
        :param bool require_sync_replica: require sync replica write.

        """
        return client_api.delete_rows(
            table, input_stream,
            client=self,
            atomicity=atomicity, durability=durability, format=format, raw=raw, require_sync_replica=require_sync_replica)

    def destroy_chunk_locations(
            self,
            node_address, location_uuids):
        """
        Mark locations for destroing. Disks of these locations can be recovered.

        :param node_address: node address.
        :param location_uuids: location uuids.

        """
        return client_api.destroy_chunk_locations(
            node_address, location_uuids,
            client=self)

    def disable_chunk_locations(
            self,
            node_address, location_uuids):
        """
        Disable locations by uuids.

        :param node_address: node address.
        :param location_uuids: location uuids.

        """
        return client_api.disable_chunk_locations(
            node_address, location_uuids,
            client=self)

    def download_core_dump(
            self,
            output_directory,
            job_id=None, operation_id=None, core_table_path=None, core_indices=None):
        """
        Downloads core dump for a given operation_id and job_id from a given core_table_path.
        If core_table_path is not specified, operation_id is used to generate core_table_path.
        If job_id is not specified, first dumped job is used.

        :param output_directory:
        :param job_id:
        :param operation_id:
        :param core_table_path:
        :param core_indices:
        :param client:
        :return: None

        """
        return client_api.download_core_dump(
            output_directory,
            client=self,
            job_id=job_id, operation_id=operation_id, core_table_path=core_table_path, core_indices=core_indices)

    def dump_job_context(
            self,
            job_id, path):
        """
        Dumps job input context to specified path.
        """
        return client_api.dump_job_context(
            job_id, path,
            client=self)

    def execute_batch(
            self,
            requests,
            concurrency=None):
        """
        Executes `requests` in parallel as one batch request.
        """
        return client_api.execute_batch(
            requests,
            client=self,
            concurrency=concurrency)

    def exists(
            self,
            path,
            read_from=None, cache_sticky_group_size=None, suppress_transaction_coordinator_sync=None):
        """
        Checks if Cypress node exists.

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`

        .. seealso:: `exists in the docs <https://yt.yandex-team.ru/docs/api/commands.html#exists>`_

        """
        return client_api.exists(
            path,
            client=self,
            read_from=read_from, cache_sticky_group_size=cache_sticky_group_size, suppress_transaction_coordinator_sync=suppress_transaction_coordinator_sync)

    def explain_query(
            self,
            query,
            timestamp=None, input_row_limit=None, output_row_limit=None, range_expansion_limit=None,
            max_subqueries=None, workload_descriptor=None, allow_full_scan=None, allow_join_without_index=None,
            format=None, raw=None, execution_pool=None, retention_timestamp=None):
        """
        Explains a SQL-like query on dynamic table.

        .. seealso:: `supported features <https://yt.yandex-team.ru/docs/description/dynamic_tables/dyn_query_language>`_

        :param str query: for example "<columns> [as <alias>], ... from \\[<table>\\]                   [where <predicate> [group by <columns> [as <alias>], ...]]".
        :param int timestamp: timestamp.
        :param format: output format.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: don't parse response to rows.

        """
        return client_api.explain_query(
            query,
            client=self,
            timestamp=timestamp, input_row_limit=input_row_limit, output_row_limit=output_row_limit,
            range_expansion_limit=range_expansion_limit, max_subqueries=max_subqueries, workload_descriptor=workload_descriptor,
            allow_full_scan=allow_full_scan, allow_join_without_index=allow_join_without_index, format=format,
            raw=raw, execution_pool=execution_pool, retention_timestamp=retention_timestamp)

    def externalize(
            self,
            path, cell_tag):
        """
        Externalize cypress node

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param int: cell_tag.

        """
        return client_api.externalize(
            path, cell_tag,
            client=self)

    def find_free_subpath(
            self,
            path):
        """
        Generates some free random subpath.

        :param str path: path.
        :rtype: str

        """
        return client_api.find_free_subpath(
            path,
            client=self)

    def find_spark_cluster(
            self,
            discovery_path=None):
        """
        Print Spark urls
        :param discovery_path: Cypress path for discovery files and logs
        :param client: YtClient
        :return: None

        """
        return client_api.find_spark_cluster(
            client=self,
            discovery_path=discovery_path)

    def freeze_table(
            self,
            path,
            first_tablet_index=None, last_tablet_index=None, sync=False):
        """
        Freezes table.

        TODO

        """
        return client_api.freeze_table(
            path,
            client=self,
            first_tablet_index=first_tablet_index, last_tablet_index=last_tablet_index, sync=sync)

    def generate_timestamp(self):
        """
        Generates timestamp.
        """
        return client_api.generate_timestamp(client=self)

    def get(
            self,
            path,
            max_size=None, attributes=None, format=None, read_from=None, cache_sticky_group_size=None,
            suppress_transaction_coordinator_sync=None):
        """
        Gets Cypress node content (attribute tree).

        :param path: path to tree, it must exist!
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param list attributes: desired node attributes in the response.
        :param format: output format (by default python dict automatically parsed from YSON).
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :return: node tree content in `format`

        Be careful: attributes have specific representation in JSON format.

        .. seealso:: `get in the docs <https://yt.yandex-team.ru/docs/api/commands.html#get>`_

        """
        return client_api.get(
            path,
            client=self,
            max_size=max_size, attributes=attributes, format=format, read_from=read_from, cache_sticky_group_size=cache_sticky_group_size,
            suppress_transaction_coordinator_sync=suppress_transaction_coordinator_sync)

    def get_attribute(
            self,
            path, attribute,
            default=_KwargSentinelClass()):
        """
        Gets attribute of Cypress node.

        :param str path: path.
        :param str attribute: attribute.
        :param default: if node hasn't attribute `attribute` this value will be returned.

        """
        return client_api.get_attribute(
            path, attribute,
            client=self,
            default=default)

    def get_current_transaction_id(self):
        """

        Returns current transaction id of client.

        """
        return client_api.get_current_transaction_id(client=self)

    def get_file_from_cache(
            self,
            md5,
            cache_path=None):
        """
        Gets file path in cache

        :param str md5: MD5 hash of file
        :param str cache_path: Path to file cache
        :return: path to file in Cypress if it was found in cache and YsonEntity otherwise

        """
        return client_api.get_file_from_cache(
            md5,
            client=self,
            cache_path=cache_path)

    def get_job(
            self,
            operation_id, job_id,
            format=None):
        """
        Get job of operation.

        :param str operation_id: operation id.
        :param str job_id: job id.

        """
        return client_api.get_job(
            operation_id, job_id,
            client=self,
            format=format)

    def get_job_input(
            self,
            job_id):
        """
        Get full input of the specified job.

        :param str job_id: job id.

        """
        return client_api.get_job_input(
            job_id,
            client=self)

    def get_job_input_paths(
            self,
            job_id):
        """
        Get input paths of the specified job.

        :param str job_if: job id.
        :return: list of YPaths.

        """
        return client_api.get_job_input_paths(
            job_id,
            client=self)

    def get_job_spec(
            self,
            job_id,
            omit_node_directory=None, omit_input_table_specs=None, omit_output_table_specs=None):
        """
        Get spec of the specified job.

        :param str job_id: job id.
        :param bool omit_node_directory: whether node directory should be removed from job spec.
        :param bool omit_input_table_specs: whether input table specs should be removed from job spec.
        :param bool omit_output_table_specc: whether output table specs should be removed from job spec.

        """
        return client_api.get_job_spec(
            job_id,
            client=self,
            omit_node_directory=omit_node_directory, omit_input_table_specs=omit_input_table_specs,
            omit_output_table_specs=omit_output_table_specs)

    def get_job_stderr(
            self,
            operation_id, job_id):
        """
        Gets stderr of the specified job.

        :param str operation_id: operation id.
        :param str job_id: job id.

        """
        return client_api.get_job_stderr(
            operation_id, job_id,
            client=self)

    def get_operation(
            self,
            operation_id=None, operation_alias=None, attributes=None, include_scheduler=None, format=None):
        """
        Get operation attributes through API.

        """
        return client_api.get_operation(
            client=self,
            operation_id=operation_id, operation_alias=operation_alias, attributes=attributes, include_scheduler=include_scheduler,
            format=format)

    def get_operation_attributes(
            self,
            operation,
            fields=None):
        """
        Returns dict with operation attributes.

        :param str operation: operation id.
        :return: operation description.
        :rtype: dict

        """
        return client_api.get_operation_attributes(
            operation,
            client=self,
            fields=fields)

    def get_operation_state(
            self,
            operation):
        """
        Returns current state of operation.

        :param str operation: operation id.

        Raises :class:`YtError <yt.common.YtError>` if operation does not exists.

        """
        return client_api.get_operation_state(
            operation,
            client=self)

    def get_query(
            self,
            query_id,
            attributes=None, stage=None, format=None):
        """
        Get query.

        :param query_id: id of a query to get
        :type query_id: str
        :param attributes: optional attribute filter
        :type attributes: list or None
        :param stage: query tracker stage, defaults to "production"
        :type stage: str

        """
        return client_api.get_query(
            query_id,
            client=self,
            attributes=attributes, stage=stage, format=format)

    def get_supported_features(
            self,
            format=None):
        """
        Retrieves supported cluster features (data types, codecs etc.).
        """
        return client_api.get_supported_features(
            client=self,
            format=format)

    def get_table_columnar_statistics(
            self,
            paths):
        """
        Gets columnar statistics of tables listed in paths
        :param paths: paths to tables
        :type paths: list of (str or :class:`TablePath <yt.wrapper.ypath.TablePath>`)

        """
        return client_api.get_table_columnar_statistics(
            paths,
            client=self)

    def get_table_schema(
            self,
            table_path):
        """
        Gets schema of table.

        :param table_path: path to table.
        :type table_path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`

        """
        return client_api.get_table_schema(
            table_path,
            client=self)

    def get_tablet_errors(
            self,
            path,
            limit=None, format=None):
        """
        Gets dynamic table tablet and replication errors.
        :param str path: path to table
        :param int limit: maximum number of returned errors of any kind

        """
        return client_api.get_tablet_errors(
            path,
            client=self,
            limit=limit, format=format)

    def get_tablet_infos(
            self,
            path, tablet_indexes,
            format=None):
        """
        TODO
        """
        return client_api.get_tablet_infos(
            path, tablet_indexes,
            client=self,
            format=format)

    def get_type(
            self,
            *args,
            **kwargs):
        """
        Gets Cypress node attribute type.

        :param str path: path.

        """
        return client_api.get_type(
            *args,
            client=self,
            **kwargs)

    def get_user_name(
            self,
            token=None, headers=None):
        """
        Requests auth method at proxy to receive user name by token or by cookies in header.
        """
        return client_api.get_user_name(
            client=self,
            token=token, headers=headers)

    def has_attribute(
            self,
            path, attribute):
        """
        Checks if Cypress node has attribute.

        :param str path: path.
        :param str attribute: attribute.

        """
        return client_api.has_attribute(
            path, attribute,
            client=self)

    def insert_rows(
            self,
            table, input_stream,
            update=None, aggregate=None, atomicity=None, durability=None, require_sync_replica=None,
            format=None, raw=None):
        """
        Inserts rows from input_stream to dynamic table.

        :param table: output table path.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param input_stream: python file-like object, string, list of strings.
        :param format: format of input data, ``yt.wrapper.config["tabular_data_format"]`` by default.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: if `raw` is specified stream with unparsed records (strings)
        in specified `format` is expected. Otherwise dicts or :class:`Record <yt.wrapper.yamr_record.Record>`
        are expected.
        :param bool require_sync_replica: require sync replica write.

        """
        return client_api.insert_rows(
            table, input_stream,
            client=self,
            update=update, aggregate=aggregate, atomicity=atomicity, durability=durability, require_sync_replica=require_sync_replica,
            format=format, raw=raw)

    def internalize(
            self,
            path):
        """
        Internalize cypress node

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`

        """
        return client_api.internalize(
            path,
            client=self)

    def is_empty(
            self,
            table):
        """
        Is table empty?

        :param table: table.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :rtype: bool

        """
        return client_api.is_empty(
            table,
            client=self)

    def is_sorted(
            self,
            table):
        """
        Is table sorted?

        :param table: table.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :rtype: bool

        """
        return client_api.is_sorted(
            table,
            client=self)

    def iterate_operations(
            self,
            user=None, state=None, type=None, filter=None, pool_tree=None, pool=None, with_failed_jobs=None,
            from_time=None, to_time=None, cursor_direction='past', limit_per_request=100, include_archive=None,
            attributes=None, format=None):
        """
        Yield operations that satisfy given options.

        """
        return client_api.iterate_operations(
            client=self,
            user=user, state=state, type=type, filter=filter, pool_tree=pool_tree, pool=pool,
            with_failed_jobs=with_failed_jobs, from_time=from_time, to_time=to_time, cursor_direction=cursor_direction,
            limit_per_request=limit_per_request, include_archive=include_archive, attributes=attributes,
            format=format)

    def link(
            self,
            target_path, link_path,
            recursive=False, ignore_existing=False, lock_existing=None, force=False, attributes=None):
        """
        Makes link to Cypress node.

        :param target_path: target path.
        :type target_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param link_path: link path.
        :type link_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool recursive: recursive.

        :param bool ignore_existing: ignore existing.
        :param bool lock_existing: lock existing node.

        .. seealso:: `link in the docs <https://yt.yandex-team.ru/docs/api/commands.html#link>`_

        """
        return client_api.link(
            target_path, link_path,
            client=self,
            recursive=recursive, ignore_existing=ignore_existing, lock_existing=lock_existing, force=force,
            attributes=attributes)

    def list(
            self,
            path,
            max_size=None, format=None, absolute=None, attributes=None, sort=True, read_from=None,
            cache_sticky_group_size=None, suppress_transaction_coordinator_sync=None):
        """
        Lists directory (map_node) content. Node type must be "map_node".

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param int max_size: max output size.
        :param list attributes: desired node attributes in the response.
        :param format: command response format, by default - `None`.
        :type format: descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool absolute: convert relative paths to absolute. Works only if format isn't specified.
        :param bool sort: if set to `True` output will be sorted.

        .. note:: Output is never sorted if format is specified or result is incomplete,     i.e. path children count exceeds max_size.

        :return: raw YSON (string) by default, parsed YSON or JSON if format is not specified (= `None`).

        .. seealso:: `list in the docs <https://yt.yandex-team.ru/docs/api/commands.html#list>`_

        """
        return client_api.list(
            path,
            client=self,
            max_size=max_size, format=format, absolute=absolute, attributes=attributes, sort=sort,
            read_from=read_from, cache_sticky_group_size=cache_sticky_group_size, suppress_transaction_coordinator_sync=suppress_transaction_coordinator_sync)

    def list_jobs(
            self,
            operation_id,
            job_type=None, job_state=None, address=None, job_competition_id=None, with_competitors=None,
            sort_field=None, sort_order=None, limit=None, offset=None, with_stderr=None, with_spec=None,
            with_fail_context=None, include_cypress=None, include_runtime=None, include_archive=None,
            data_source=None, format=None):
        """
        List jobs of operation.
        """
        return client_api.list_jobs(
            operation_id,
            client=self,
            job_type=job_type, job_state=job_state, address=address, job_competition_id=job_competition_id,
            with_competitors=with_competitors, sort_field=sort_field, sort_order=sort_order, limit=limit,
            offset=offset, with_stderr=with_stderr, with_spec=with_spec, with_fail_context=with_fail_context,
            include_cypress=include_cypress, include_runtime=include_runtime, include_archive=include_archive,
            data_source=data_source, format=format)

    def list_operations(
            self,
            user=None, state=None, type=None, filter=None, pool_tree=None, pool=None, with_failed_jobs=None,
            from_time=None, to_time=None, cursor_time=None, cursor_direction=None, include_archive=None,
            include_counters=None, limit=None, enable_ui_mode=False, attributes=None, format=None):
        """
        List operations that satisfy given options.

        """
        return client_api.list_operations(
            client=self,
            user=user, state=state, type=type, filter=filter, pool_tree=pool_tree, pool=pool,
            with_failed_jobs=with_failed_jobs, from_time=from_time, to_time=to_time, cursor_time=cursor_time,
            cursor_direction=cursor_direction, include_archive=include_archive, include_counters=include_counters,
            limit=limit, enable_ui_mode=enable_ui_mode, attributes=attributes, format=format)

    def list_queries(
            self,
            user=None, engine=None, state=None, filter=None, from_time=None, to_time=None, cursor_time=None,
            cursor_direction=None, limit=None, attributes=None, stage=None, format=None):
        """
        List operations that satisfy given options.

        """
        return client_api.list_queries(
            client=self,
            user=user, engine=engine, state=state, filter=filter, from_time=from_time, to_time=to_time,
            cursor_time=cursor_time, cursor_direction=cursor_direction, limit=limit, attributes=attributes,
            stage=stage, format=format)

    def lock(
            self,
            path,
            mode=None, waitable=False, wait_for=None, child_key=None, attribute_key=None):
        """
        Tries to lock the path.

        :param str mode: blocking type, one of ["snapshot", "shared" or "exclusive"], "exclusive" by default.
        :param bool waitable: wait for lock if node is under blocking.
        :param int wait_for: wait interval in milliseconds. If timeout occurred,     :class:`YtError <yt.common.YtError>` is raised.
        :return: map with lock information (as dict) or throws     :class:`YtResponseError <yt.wrapper.errors.YtResponseError>` with 40* code if lock conflict detected.

        .. seealso:: `lock in the docs <https://yt.yandex-team.ru/docs/description/storage/transactions#locks>`_

        """
        return client_api.lock(
            path,
            client=self,
            mode=mode, waitable=waitable, wait_for=wait_for, child_key=child_key, attribute_key=attribute_key)

    def lock_rows(
            self,
            table, input_stream,
            locks=[], lock_type=None, durability=None, format=None, raw=None):
        """
        Lock rows with keys from input_stream from dynamic table.

        :param table: table to remove rows from.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param input_stream: python file-like object, string, list of strings.
        :param format: format of input data, ``yt.wrapper.config["tabular_data_format"]`` by default.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: if `raw` is specified stream with unparsed records (strings)
        in specified `format` is expected. Otherwise dicts or :class:`Record <yt.wrapper.yamr_record.Record>`
        are expected.

        """
        return client_api.lock_rows(
            table, input_stream,
            client=self,
            locks=locks, lock_type=lock_type, durability=durability, format=format, raw=raw)

    def lookup_rows(
            self,
            table, input_stream,
            timestamp=None, column_names=None, keep_missing_rows=None, enable_partial_result=None,
            use_lookup_cache=None, format=None, raw=None, versioned=None, retention_timestamp=None):
        """
        Lookups rows in dynamic table.

        .. seealso:: `supported features <https://yt.yandex-team.ru/docs/description/dynamic_tables/dyn_query_language>`_

        :param format: output format.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: don't parse response to rows.
        :param bool versioned: return all versions of the requested rows.

        """
        return client_api.lookup_rows(
            table, input_stream,
            client=self,
            timestamp=timestamp, column_names=column_names, keep_missing_rows=keep_missing_rows,
            enable_partial_result=enable_partial_result, use_lookup_cache=use_lookup_cache, format=format,
            raw=raw, versioned=versioned, retention_timestamp=retention_timestamp)

    def make_idm_client(
            self,
            address=None):
        """
        Creates IdmClient from YtClient.

        """
        return client_api.make_idm_client(
            client=self,
            address=address)

    def mkdir(
            self,
            path,
            recursive=None):
        """
        Makes directory (Cypress node of map_node type).

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool recursive: ``yt.wrapper.config["yamr_mode"]["create_recursive"]`` by default.

        """
        return client_api.mkdir(
            path,
            client=self,
            recursive=recursive)

    def mount_table(
            self,
            path,
            first_tablet_index=None, last_tablet_index=None, cell_id=None, freeze=False, sync=False,
            target_cell_ids=None):
        """
        Mounts table.

        TODO

        """
        return client_api.mount_table(
            path,
            client=self,
            first_tablet_index=first_tablet_index, last_tablet_index=last_tablet_index, cell_id=cell_id,
            freeze=freeze, sync=sync, target_cell_ids=target_cell_ids)

    def move(
            self,
            source_path, destination_path,
            recursive=None, force=None, preserve_account=None, preserve_owner=None, preserve_expiration_time=None,
            preserve_expiration_timeout=None, preserve_creation_time=None, preserve_modification_time=None,
            pessimistic_quota_check=None):
        """
        Moves (renames) Cypress node.

        :param source_path: source path.
        :type source_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param destination_path: destination path.
        :type destination_path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool recursive: ``yt.wrapper.config["yamr_mode"]["create_recursive"]`` by default.
        :param bool preserve_account: preserve account.
        :param bool preserve_owner: preserve owner.
        :param bool preserve_expiration_time: preserve expiration time.
        :param bool preserve_expiration_timeout: preserve expiration timeout.
        :param bool preserve_creation_time: preserve creation time.
        :param bool preserve_modification_time: preserve modification time.
        :param bool force: force.
        :param bool pessimistic_quota_check: pessimistic quota check.

        .. seealso:: `move in the docs <https://yt.yandex-team.ru/docs/api/commands.html#move>`_

        """
        return client_api.move(
            source_path, destination_path,
            client=self,
            recursive=recursive, force=force, preserve_account=preserve_account, preserve_owner=preserve_owner,
            preserve_expiration_time=preserve_expiration_time, preserve_expiration_timeout=preserve_expiration_timeout,
            preserve_creation_time=preserve_creation_time, preserve_modification_time=preserve_modification_time,
            pessimistic_quota_check=pessimistic_quota_check)

    def ping_transaction(
            self,
            transaction,
            timeout=None, retry_config=None):
        """
        Prolongs transaction lifetime.

        :param str transaction: transaction id.

        .. seealso:: `ping_tx in the docs <https://yt.yandex-team.ru/docs/api/commands.html#pingtx>`_

        """
        return client_api.ping_transaction(
            transaction,
            client=self,
            timeout=timeout, retry_config=retry_config)

    def put_file_to_cache(
            self,
            path, md5,
            cache_path=None):
        """
        Puts file to cache

        :param str path: path to file in Cypress
        :param str md5: Expected MD5 hash of file
        :param str cache_path: Path to file cache
        :return: path to file in cache

        """
        return client_api.put_file_to_cache(
            path, md5,
            client=self,
            cache_path=cache_path)

    def read_blob_table(
            self,
            table,
            part_index_column_name=None, data_column_name=None, part_size=None, table_reader=None):
        """
        Reads file from blob table.

        :param table: table to read.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param string part_index_column_name: name of column with part indexes.
        :param string data_column_name: name of column with data.
        :param int part_size: size of each blob.
        :param dict table_reader: spec of "read" operation.
        :rtype: :class:`ResponseStream <yt.wrapper.response_stream.ResponseStream>`.


        """
        return client_api.read_blob_table(
            table,
            client=self,
            part_index_column_name=part_index_column_name, data_column_name=data_column_name, part_size=part_size,
            table_reader=table_reader)

    def read_file(
            self,
            path,
            file_reader=None, offset=None, length=None, enable_read_parallel=None):
        """
        Downloads file from path in Cypress to local machine.

        :param path: path to file in Cypress.
        :type path: str or :class:`FilePath <yt.wrapper.ypath.FilePath>`
        :param dict file_reader: spec of download command.
        :param int offset: offset in input file in bytes, 0 by default.
        :param int length: length in bytes of desired part of input file, all file without offset by default.
        :return: some stream over downloaded file, string generator by default.

        """
        return client_api.read_file(
            path,
            client=self,
            file_reader=file_reader, offset=offset, length=length, enable_read_parallel=enable_read_parallel)

    def read_query_result(
            self,
            query_id,
            result_index=None, stage=None, format=None, raw=None):
        """
        Read query result.

        :param query_id: id of a query to read result
        :type query_id: str
        :param result_index: index of a result to read, defaults to 0
        :type result_index: int
        :param stage: query tracker stage, defaults to "production"
        :type stage: str

        """
        return client_api.read_query_result(
            query_id,
            client=self,
            result_index=result_index, stage=stage, format=format, raw=raw)

    def read_table(
            self,
            table,
            format=None, table_reader=None, control_attributes=None, unordered=None, raw=None,
            response_parameters=None, enable_read_parallel=None):
        """
        Reads rows from table and parse (optionally).

        :param table: table to read.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param dict table_reader: spec of "read" operation.
        :param bool raw: don't parse response to rows.
        :rtype: if `raw` is specified -- :class:`ResponseStream <yt.wrapper.response_stream.ResponseStream>`,     rows iterator over dicts or :class:`Record <yt.wrapper.yamr_record.Record>` otherwise.

        If ``yt.wrapper.config["read_retries"]["enable"]`` is specified,
        command is executed under self-pinged transaction with retries and snapshot lock on the table.
        This transaction is alive until your finish reading your table, or call `close` method of ResponseStream.

        """
        return client_api.read_table(
            table,
            client=self,
            format=format, table_reader=table_reader, control_attributes=control_attributes, unordered=unordered,
            raw=raw, response_parameters=response_parameters, enable_read_parallel=enable_read_parallel)

    def read_table_structured(
            self,
            table, row_type,
            table_reader=None, unordered=None, response_parameters=None, enable_read_parallel=None):
        """
        Reads rows from table in structured format. Cf. docstring for read_table
        """
        return client_api.read_table_structured(
            table, row_type,
            client=self,
            table_reader=table_reader, unordered=unordered, response_parameters=response_parameters,
            enable_read_parallel=enable_read_parallel)

    def register_queue_consumer(
            self,
            queue_path, consumer_path, vital):
        """
        Register queue consumer.

        :param queue_path: path to queue table.
        :type queue_path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param consumer_path: path to consumer table.
        :type consumer_path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param bool vital: vital.

        """
        return client_api.register_queue_consumer(
            queue_path, consumer_path, vital,
            client=self)

    def remount_table(
            self,
            path,
            first_tablet_index=None, last_tablet_index=None):
        """
        Remounts table.

        TODO

        """
        return client_api.remount_table(
            path,
            client=self,
            first_tablet_index=first_tablet_index, last_tablet_index=last_tablet_index)

    def remove(
            self,
            path,
            recursive=False, force=False):
        """
        Removes Cypress node.

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param bool recursive: recursive.
        :param bool force: force.

        .. seealso:: `remove in the docs <https://yt.yandex-team.ru/docs/api/commands.html#remove>`_

        """
        return client_api.remove(
            path,
            client=self,
            recursive=recursive, force=force)

    def remove_attribute(
            self,
            path, attribute):
        """
        Removes Cypress node `attribute`

        :param str path: path.
        :param str attribute: attribute.

        """
        return client_api.remove_attribute(
            path, attribute,
            client=self)

    def remove_maintenance(
            self,
            node_address, maintenance_id):
        """
        Removes maintenance request from given node

        :param node_address: node address.
        :param maintenance_id: maintenance id.

        """
        return client_api.remove_maintenance(
            node_address, maintenance_id,
            client=self)

    def remove_member(
            self,
            member, group):
        """
        Removes member from Cypress node group.

        :param str member: member to remove.
        :param str group: group to remove member from.

        .. seealso:: `permissions in the docs <https://yt.yandex-team.ru/docs/description/common/access_control>`_

        """
        return client_api.remove_member(
            member, group,
            client=self)

    def reshard_table(
            self,
            path,
            pivot_keys=None, tablet_count=None, first_tablet_index=None, last_tablet_index=None,
            uniform=None, enable_slicing=None, slicing_accuracy=None, sync=False):
        """
        Changes pivot keys separating tablets of a given table.

        TODO

        """
        return client_api.reshard_table(
            path,
            client=self,
            pivot_keys=pivot_keys, tablet_count=tablet_count, first_tablet_index=first_tablet_index,
            last_tablet_index=last_tablet_index, uniform=uniform, enable_slicing=enable_slicing, slicing_accuracy=slicing_accuracy,
            sync=sync)

    def reshard_table_automatic(
            self,
            path,
            sync=False):
        """
        Automatically balance tablets of a mounted table according to tablet balancer config.

        Only mounted tablets will be resharded.

        :param path: path to table.
        :type path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param bool sync: wait for the command to finish.

        """
        return client_api.reshard_table_automatic(
            path,
            client=self,
            sync=sync)

    def restore_table_backup(
            self,
            manifest,
            force=None, mount=None, enable_replicas=None):
        """
        Restores a collection of tables from its backup copy.

        :param manifest: description of tables to be restored.
        :type manifest: dict or :class:`BackupManifest`
        :param bool force: overwrite destination tables.
        :param bool mount: mount restored tables which were mounted before backup.
        :param bool enable_replicas: enable restored table replicas which were enabled before backup.

        """
        return client_api.restore_table_backup(
            manifest,
            client=self,
            force=force, mount=mount, enable_replicas=enable_replicas)

    def resume_operation(
            self,
            operation):
        """
        Continues operation after suspending.

        :param str operation: operation id.

        """
        return client_api.resume_operation(
            operation,
            client=self)

    def resurrect_chunk_locations(
            self,
            node_address, location_uuids):
        """
        Try resurrect disabled locations.

        :param node_address: node address.
        :param location_uuids: location uuids.

        """
        return client_api.resurrect_chunk_locations(
            node_address, location_uuids,
            client=self)

    def row_count(
            self,
            table):
        """
        Returns number of rows in the table.

        :param table: table.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :rtype: int

        """
        return client_api.row_count(
            table,
            client=self)

    def run_command_with_lock(
            self,
            path, command,
            popen_kwargs=None, lock_conflict_callback=None, ping_failed_callback=None, set_address=True,
            address_path=None, create_lock_options=None, poll_period=None, forward_signals=None):
        """

        Run given command under lock.

        """
        return client_api.run_command_with_lock(
            path, command,
            client=self,
            popen_kwargs=popen_kwargs, lock_conflict_callback=lock_conflict_callback, ping_failed_callback=ping_failed_callback,
            set_address=set_address, address_path=address_path, create_lock_options=create_lock_options,
            poll_period=poll_period, forward_signals=forward_signals)

    def run_erase(
            self,
            table,
            spec=None, sync=True):
        """
        Erases table or part of it.

        Erase differs from remove command.
        It only removes content of table (range of records or all table) and doesn't remove Cypress node.

        :param table: table.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param dict spec: operation spec.

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_erase(
            table,
            client=self,
            spec=spec, sync=sync)

    def run_job_shell(
            self,
            job_id,
            shell_name=None, timeout=None, command=None):
        """
        Runs interactive shell in the job sandbox.

        :param str job_id: job id.
        :param str shell_name: shell name.

        """
        return client_api.run_job_shell(
            job_id,
            client=self,
            shell_name=shell_name, timeout=timeout, command=command)

    def run_join_reduce(
            self,
            binary, source_table, destination_table,
            local_files=None, yt_files=None, format=None, input_format=None, output_format=None,
            sync=True, job_io=None, table_writer=None, job_count=None, memory_limit=None, spec=None,
            sort_by=None, reduce_by=None, join_by=None, stderr_table=None):
        """
        Runs join-reduce operation.

        .. note:: You should specity at least two input table and all except one     should have set foreign attibute. You should also specify join_by columns.

        .. seealso::  :ref:`operation_parameters` and :func:`run_map_reduce <.run_map_reduce>`.

        """
        return client_api.run_join_reduce(
            binary, source_table, destination_table,
            client=self,
            local_files=local_files, yt_files=yt_files, format=format, input_format=input_format,
            output_format=output_format, sync=sync, job_io=job_io, table_writer=table_writer, job_count=job_count,
            memory_limit=memory_limit, spec=spec, sort_by=sort_by, reduce_by=reduce_by, join_by=join_by,
            stderr_table=stderr_table)

    def run_map(
            self,
            binary, source_table,
            destination_table=None, local_files=None, yt_files=None, format=None, input_format=None,
            output_format=None, sync=True, job_io=None, table_writer=None, job_count=None, memory_limit=None,
            spec=None, ordered=None, stderr_table=None):
        """
        Runs map operation.

        :param bool ordered: force ordered input for mapper.

        .. seealso::  :ref:`operation_parameters` and :func:`run_map_reduce <.run_map_reduce>`.

        """
        return client_api.run_map(
            binary, source_table,
            client=self,
            destination_table=destination_table, local_files=local_files, yt_files=yt_files, format=format,
            input_format=input_format, output_format=output_format, sync=sync, job_io=job_io, table_writer=table_writer,
            job_count=job_count, memory_limit=memory_limit, spec=spec, ordered=ordered, stderr_table=stderr_table)

    def run_map_reduce(
            self,
            mapper, reducer, source_table, destination_table,
            format=None, map_input_format=None, map_output_format=None, reduce_input_format=None,
            reduce_output_format=None, sync=True, job_io=None, table_writer=None, spec=None, map_local_files=None,
            map_yt_files=None, reduce_local_files=None, reduce_yt_files=None, mapper_memory_limit=None,
            reducer_memory_limit=None, sort_by=None, reduce_by=None, reduce_combiner=None, reduce_combiner_input_format=None,
            reduce_combiner_output_format=None, reduce_combiner_local_files=None, reduce_combiner_yt_files=None,
            reduce_combiner_memory_limit=None, stderr_table=None):
        """
        Runs map (optionally), sort, reduce and reduce-combine (optionally) operations.

        :param mapper: python generator, callable object-generator or string (with bash commands).
        :param reducer: python generator, callable object-generator or string (with bash commands).
        :param source_table: input tables or list of tables.
        :type source_table: list[str or :class:`TablePath <yt.wrapper.ypath.TablePath>`]
        :param destination_table: output table or list of tables.
        :type destination_table: list[str or :class:`TablePath <yt.wrapper.ypath.TablePath>`]
        :param stderr_table: stderrs table.
        :type stderr_table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param format: common format of input, intermediate and output data. More specific formats will override it.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param map_input_format: input format for map operation.
        :type map_input_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param map_output_format: output format for map operation.
        :type map_output_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param reduce_input_format: input format for reduce operation.
        :type reduce_input_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param reduce_output_format: output format for reduce operation.
        :type reduce_output_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param dict job_io: job io specification.
        :param dict table_writer: standard operation parameter.
        :param dict spec: standard operation parameter.
        :param map_local_files: paths to map scripts on local machine.
        :type map_local_files: str or list[str]
        :param map_yt_files: paths to map scripts in Cypress.
        :type map_yt_files: str or list[str]
        :param reduce_local_files: paths to reduce scripts on local machine.
        :type reduce_combiner_local_files: str or list[str]
        :param reduce_yt_files: paths to reduce scripts in Cypress.
        :type reduce_yt_files: str or list[str]
        :param int mapper_memory_limit: in bytes, map **job** memory limit.
        :param int reducer_memory_limit: in bytes, reduce **job** memory limit.
        :param sort_by: list of columns for sorting by, equals to `reduce_by` by default.
        :type sort_by: str or list[str]
        :param reduce_by: list of columns for grouping by.
        :type reduce_by: str or list[str]
        :param reduce_combiner: python generator, callable object-generator or string (with bash commands).
        :param reduce_combiner_input_format: input format for reduce combiner.
        :type reduce_combiner_input_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param reduce_combiner_output_format: output format for reduce combiner.
        :type reduce_combiner_output_format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param reduce_combiner_local_files: paths to reduce combiner scripts on local machine.
        :type reduce_combiner_local_files: str or list[str]
        :param reduce_combiner_yt_files: paths to reduce combiner scripts in Cypress.
        :type reduce_combiner_yt_files: str or list[str]
        :param int reduce_combiner_memory_limit: memory limit in bytes.

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_map_reduce(
            mapper, reducer, source_table, destination_table,
            client=self,
            format=format, map_input_format=map_input_format, map_output_format=map_output_format,
            reduce_input_format=reduce_input_format, reduce_output_format=reduce_output_format, sync=sync,
            job_io=job_io, table_writer=table_writer, spec=spec, map_local_files=map_local_files,
            map_yt_files=map_yt_files, reduce_local_files=reduce_local_files, reduce_yt_files=reduce_yt_files,
            mapper_memory_limit=mapper_memory_limit, reducer_memory_limit=reducer_memory_limit, sort_by=sort_by,
            reduce_by=reduce_by, reduce_combiner=reduce_combiner, reduce_combiner_input_format=reduce_combiner_input_format,
            reduce_combiner_output_format=reduce_combiner_output_format, reduce_combiner_local_files=reduce_combiner_local_files,
            reduce_combiner_yt_files=reduce_combiner_yt_files, reduce_combiner_memory_limit=reduce_combiner_memory_limit,
            stderr_table=stderr_table)

    def run_merge(
            self,
            source_table, destination_table,
            mode=None, sync=True, job_io=None, table_writer=None, job_count=None, spec=None):
        """
        Merges source tables to destination table.

        :param source_table: tables to merge.
        :type source_table: list[str or :class:`TablePath <yt.wrapper.ypath.TablePath>`]
        :param destination_table: path to result table.
        :type destination_table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param str mode: one of ["auto", "unordered", "ordered", "sorted"]. "auto" by default.
        Mode `sorted` keeps sortedness of output tables.
        Mode `ordered` is about chunk magic, not for ordinary users.
        In `auto` mode system chooses proper mode depending on the table sortedness.
        :param int job_count: recommendation how many jobs should run.
        :param dict job_io: job io specification.
        :param dict table_writer: standard operation parameter.
        :param dict spec: standard operation parameter.

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_merge(
            source_table, destination_table,
            client=self,
            mode=mode, sync=sync, job_io=job_io, table_writer=table_writer, job_count=job_count,
            spec=spec)

    def run_operation(
            self,
            spec_builder,
            sync=True, run_operation_mutation_id=None, enable_optimizations=False):
        """
        Runs operation.

        :param spec_builder: spec builder with parameters of the operation.
        :type spec_builder: :class:`SpecBuilder <yt.wrapper.spec_builders.SpecBuilder>`

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_operation(
            spec_builder,
            client=self,
            sync=sync, run_operation_mutation_id=run_operation_mutation_id, enable_optimizations=enable_optimizations)

    def run_reduce(
            self,
            binary, source_table,
            destination_table=None, local_files=None, yt_files=None, format=None, input_format=None,
            output_format=None, sync=True, job_io=None, table_writer=None, job_count=None, memory_limit=None,
            spec=None, sort_by=None, reduce_by=None, join_by=None, stderr_table=None, enable_key_guarantee=None):
        """
        Runs reduce operation.

        .. seealso::  :ref:`operation_parameters` and :func:`run_map_reduce <.run_map_reduce>`.

        """
        return client_api.run_reduce(
            binary, source_table,
            client=self,
            destination_table=destination_table, local_files=local_files, yt_files=yt_files, format=format,
            input_format=input_format, output_format=output_format, sync=sync, job_io=job_io, table_writer=table_writer,
            job_count=job_count, memory_limit=memory_limit, spec=spec, sort_by=sort_by, reduce_by=reduce_by,
            join_by=join_by, stderr_table=stderr_table, enable_key_guarantee=enable_key_guarantee)

    def run_remote_copy(
            self,
            source_table, destination_table,
            cluster_name=None, network_name=None, cluster_connection=None, copy_attributes=None,
            spec=None, sync=True):
        """
        Copies source table from remote cluster to destination table on current cluster.

        :param source_table: source table to copy (or list of tables).
        :type source_table: list[str or :class:`TablePath <yt.wrapper.ypath.TablePath>`]
        :param destination_table: destination table.
        :type destination_table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param str cluster_name: cluster name.
        :param str network_name: network name.
        :param dict spec: operation spec.
        :param bool copy_attributes: copy attributes source_table to destination_table.

        .. note:: For atomicity you should specify just one item in `source_table`     in case attributes copying.

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_remote_copy(
            source_table, destination_table,
            client=self,
            cluster_name=cluster_name, network_name=network_name, cluster_connection=cluster_connection,
            copy_attributes=copy_attributes, spec=spec, sync=sync)

    def run_sort(
            self,
            source_table,
            destination_table=None, sort_by=None, sync=True, job_io=None, table_writer=None, spec=None):
        """
        Sorts source tables to destination table.

        If destination table is not specified, than it equals to source table.

        .. seealso::  :ref:`operation_parameters`.

        """
        return client_api.run_sort(
            source_table,
            client=self,
            destination_table=destination_table, sort_by=sort_by, sync=sync, job_io=job_io, table_writer=table_writer,
            spec=spec)

    def sample_rows_from_table(
            self,
            table, output_table, row_count):
        """
        Samples random rows from table.

        :param table: path to input table
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param output_table: path to output table
        :type output_table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param int row_count: the number of rows to be sampled


        """
        return client_api.sample_rows_from_table(
            table, output_table, row_count,
            client=self)

    def search(
            self,
            root='', node_type=None, path_filter=None, object_filter=None, subtree_filter=None,
            map_node_order=_MapOrderSorted(), list_node_order=None, attributes=None, exclude=None,
            depth_bound=None, follow_links=False, read_from=None, cache_sticky_group_size=None, enable_batch_mode=None):
        """
        Searches for some nodes in Cypress subtree.

        :param root: path to search.
        :type root: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param node_type: node types.
        :type node_type: list[str]
        :param object_filter: filtering predicate.
        :param map_node_order: function that specifies order of traversing map_node children;
        that function should take two arguments (path, object)
        and should return iterable over object children;
        default map_node_order sorts children lexicographically;
        set it to None in order to switch off sorting.
        :param attributes: these attributes will be added to result objects.
        :type attributes: list[str]
        :param exclude: excluded paths.
        :type exclude: list[str]
        :param int depth_bound: recursion depth.
        :param bool follow_links: follow links.
        :param lambda action: apply given method to each found path.
        :return: result paths as iterable over :class:`YsonString <yt.yson.yson_types.YsonString>`.

        """
        return client_api.search(
            client=self,
            root=root, node_type=node_type, path_filter=path_filter, object_filter=object_filter,
            subtree_filter=subtree_filter, map_node_order=map_node_order, list_node_order=list_node_order,
            attributes=attributes, exclude=exclude, depth_bound=depth_bound, follow_links=follow_links,
            read_from=read_from, cache_sticky_group_size=cache_sticky_group_size, enable_batch_mode=enable_batch_mode)

    def select_rows(
            self,
            query,
            timestamp=None, input_row_limit=None, output_row_limit=None, range_expansion_limit=None,
            fail_on_incomplete_result=None, verbose_logging=None, enable_code_cache=None, max_subqueries=None,
            workload_descriptor=None, allow_full_scan=None, allow_join_without_index=None, format=None,
            raw=None, execution_pool=None, response_parameters=None, retention_timestamp=None, placeholder_values=None):
        """
        Executes a SQL-like query on dynamic table.

        .. seealso:: `supported features <https://yt.yandex-team.ru/docs/description/dynamic_tables/dyn_query_language>`_

        :param str query: for example "<columns> [as <alias>], ... from \\[<table>\\]                   [where <predicate> [group by <columns> [as <alias>], ...]]".
        :param int timestamp: timestamp.
        :param format: output format.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param bool raw: don't parse response to rows.

        """
        return client_api.select_rows(
            query,
            client=self,
            timestamp=timestamp, input_row_limit=input_row_limit, output_row_limit=output_row_limit,
            range_expansion_limit=range_expansion_limit, fail_on_incomplete_result=fail_on_incomplete_result,
            verbose_logging=verbose_logging, enable_code_cache=enable_code_cache, max_subqueries=max_subqueries,
            workload_descriptor=workload_descriptor, allow_full_scan=allow_full_scan, allow_join_without_index=allow_join_without_index,
            format=format, raw=raw, execution_pool=execution_pool, response_parameters=response_parameters,
            retention_timestamp=retention_timestamp, placeholder_values=placeholder_values)

    def set(
            self,
            path, value,
            format=None, recursive=False, force=None, suppress_transaction_coordinator_sync=None):
        """
        Sets new value to Cypress node.

        :param path: path.
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param value: json-able object.
        :param format: format of the value. If format is None than value should be     object that can be dumped to JSON of YSON. Otherwise it should be string.
        :param bool recursive: recursive.

        .. seealso:: `set in the docs <https://yt.yandex-team.ru/docs/api/commands.html#set>`_

        """
        return client_api.set(
            path, value,
            client=self,
            format=format, recursive=recursive, force=force, suppress_transaction_coordinator_sync=suppress_transaction_coordinator_sync)

    def set_attribute(
            self,
            path, attribute, value):
        """
        Sets Cypress node `attribute` to `value`.

        :param str path: path.
        :param str attribute: attribute.
        :param value: value.

        """
        return client_api.set_attribute(
            path, attribute, value,
            client=self)

    def shuffle_table(
            self,
            table,
            sync=True, temp_column_name='__random_number', spec=None):
        """
        Shuffles table randomly.

        :param table: table.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param temp_column_name: temporary column name which will be used in reduce_by.
        :type temp_column_name: str.

        . seealso::  :ref:`operation_parameters`.

        """
        return client_api.shuffle_table(
            table,
            client=self,
            sync=sync, temp_column_name=temp_column_name, spec=spec)

    def sky_share(
            self,
            path,
            cluster=None, key_columns=[], enable_fastbone=False):
        """
        Shares table on cluster via skynet
        :param path: path to table
        :type path: str or :class:`YPath <yt.wrapper.ypath.YPath>`
        :param cluster: cluster name [by default it is derived from proxy url]
        :type path: str

        .. seealso:: `blob tables in the docs <https://docs.yandex-team.ru/docs/yt/description/storage/blobtables#skynet>`_

        """
        return client_api.sky_share(
            path,
            client=self,
            cluster=cluster, key_columns=key_columns, enable_fastbone=enable_fastbone)

    def smart_upload_file(
            self,
            filename,
            destination=None, yt_filename=None, placement_strategy=None, ignore_set_attributes_error=True,
            hash=None):
        """
        Uploads file to destination path with custom placement strategy.

        :param str filename: path to file on local machine.
        :param str destination: desired file path in Cypress.
        :param str yt_filename: "file_name" attribute of file in Cypress (visible in operation name of file),
        by default basename of `destination` (or `filename` if `destination` is not set)
        :param str placement_strategy: one of ["replace", "ignore", "random", "hash"], "hash" by default.
        :param bool ignore_set_attributes_error: ignore :class:`YtResponseError <yt.wrapper.errors.YtResponseError>`
        during attributes setting.
        :return: YSON structure with result destination path

        `placement_strategy` can be set to:

        * "replace" or "ignore" -> destination path will be `destination`
        or ``yt.wrapper.config["remote_temp_files_directory"]/<basename>`` if destination is not specified.

        * "random" (only if `destination` parameter is `None`) -> destination path will be
        ``yt.wrapper.config["remote_temp_files_directory"]/<basename><random_suffix>``.

        * "hash" (only if `destination` parameter is `None`) -> destination path will be
        ``yt.wrapper.config["remote_temp_files_directory"]/hash/<md5sum_of_file>`` or this path will be link
        to some random Cypress path.

        """
        return client_api.smart_upload_file(
            filename,
            client=self,
            destination=destination, yt_filename=yt_filename, placement_strategy=placement_strategy,
            ignore_set_attributes_error=ignore_set_attributes_error, hash=hash)

    def start_query(
            self,
            engine, query,
            settings=None, stage=None):
        """
        Start query.

        :param engine: one of "ql", "yql".
        :type engine: str
        :param query: text of a query
        :type query: str
        :param settings: a ditionary of settings
        :type settings: dict or None
        :param stage: query tracker stage, defaults to "production"
        :type stage: str

        """
        return client_api.start_query(
            engine, query,
            client=self,
            settings=settings, stage=stage)

    def start_spark_cluster(
            self,
            spark_worker_core_count, spark_worker_memory_limit, spark_worker_count,
            spark_worker_timeout='5m', operation_alias=None, discovery_path=None, pool=None, spark_worker_tmpfs_limit='150G',
            spark_master_memory_limit='2G', spark_history_server_memory_limit='8G', dynamic_config_path='//sys/spark/bin/releases/spark-launch-conf',
            operation_spec=None):
        """
        Start Spark Standalone cluster in YT Vanilla Operation. See https://wiki.yandex-team.ru/spyt/
        :param spark_worker_core_count: Number of cores that will be available on Spark worker
        :param spark_worker_memory_limit: Amount of memory that will be available on Spark worker
        :param spark_worker_count: Number of Spark workers
        :param spark_worker_timeout: Worker timeout to wait master start
        :param operation_alias: Alias for the underlying YT operation
        :param discovery_path: Cypress path for discovery files and logs, the same path must be used in find_spark_cluster
        :param pool: Pool for the underlying YT operation
        :param spark_worker_tmpfs_limit: Limit of tmpfs usage per Spark worker
        :param spark_master_memory_limit: Memory limit on Spark master
        :param spark_history_server_memory_limit: Memory limit on Spark History Server
        :param dynamic_config_path: YT path of dynamic config
        :param operation_spec: YT Vanilla Operation spec
        :param client: YtClient
        :return: running YT Vanilla Operation with Spark Standalone

        """
        return client_api.start_spark_cluster(
            spark_worker_core_count, spark_worker_memory_limit, spark_worker_count,
            client=self,
            spark_worker_timeout=spark_worker_timeout, operation_alias=operation_alias, discovery_path=discovery_path,
            pool=pool, spark_worker_tmpfs_limit=spark_worker_tmpfs_limit, spark_master_memory_limit=spark_master_memory_limit,
            spark_history_server_memory_limit=spark_history_server_memory_limit, dynamic_config_path=dynamic_config_path,
            operation_spec=operation_spec)

    def start_transaction(
            self,
            parent_transaction=None, timeout=None, deadline=None, attributes=None, type='master',
            sticky=False, prerequisite_transaction_ids=None):
        """
        Starts transaction.

        :param str parent_transaction: parent transaction id.
        :param int timeout: transaction lifetime singe last ping in milliseconds.
        :param str type: could be either "master" or "tablet"
        :param bool sticky: EXPERIMENTAL, do not use it, unless you have been told to do so.
        :param dict attributes: attributes
        :return: new transaction id.
        :rtype: str

        .. seealso:: `start_tx in the docs <https://yt.yandex-team.ru/docs/api/commands.html#starttx>`_

        """
        return client_api.start_transaction(
            client=self,
            parent_transaction=parent_transaction, timeout=timeout, deadline=deadline, attributes=attributes,
            type=type, sticky=sticky, prerequisite_transaction_ids=prerequisite_transaction_ids)

    def suspend_operation(
            self,
            operation,
            abort_running_jobs=False):
        """
        Suspends operation.

        :param str operation: operation id.

        """
        return client_api.suspend_operation(
            operation,
            client=self,
            abort_running_jobs=abort_running_jobs)

    def transfer_account_resources(
            self,
            source_account, destination_account, resource_delta):
        """
        Transfers resources between accounts.

        On the path from `source_account` to `destination_account` in the account tree, `resource_delta`
        is subtracted from `source_account` and its ancestors and added to `destination_account` and
        its ancestors. Limits of the lowest common ancestor remain unchanged.

        :param str source_account: account to transfer resources from.
        :param str destination_account: account to transfer resources to.
        :param resource_delta: the amount of transferred resources as a dict.

        """
        return client_api.transfer_account_resources(
            source_account, destination_account, resource_delta,
            client=self)

    def transfer_pool_resources(
            self,
            source_pool, destination_pool, pool_tree, resource_delta):
        """
        Transfers resources between pools.

        On the path from `source_pool` to `destination_pool` in the specified `pool_tree`, `resource_delta`
        is subtracted from `source_pool` and its ancestors and added to `destination_pool` and
        its ancestors. Limits of the lowest common ancestor remain unchanged.

        :param str source_pool: pool to transfer resources from.
        :param str destination_pool: pool to transfer resources to.
        :param resource_delta: the amount of transferred resources as a dict.

        """
        return client_api.transfer_pool_resources(
            source_pool, destination_pool, pool_tree, resource_delta,
            client=self)

    def transform(
            self,
            source_table,
            destination_table=None, erasure_codec=None, compression_codec=None, desired_chunk_size=None,
            spec=None, check_codecs=False, optimize_for=None):
        """
        Transforms source table to destination table writing data with given compression and erasure codecs.

        Automatically calculates desired chunk size and data size per job. Also can be used to convert chunks in
        table between old and new formats (optimize_for parameter).

        """
        return client_api.transform(
            source_table,
            client=self,
            destination_table=destination_table, erasure_codec=erasure_codec, compression_codec=compression_codec,
            desired_chunk_size=desired_chunk_size, spec=spec, check_codecs=check_codecs, optimize_for=optimize_for)

    def trim_rows(
            self,
            path, tablet_index, trimmed_row_count):
        """
        Trim rows of the dynamic table.

        :param path: path to table.
        :type path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param int tablet_index: tablet index.
        :param int trimmed_row_count: trimmed row count.

        """
        return client_api.trim_rows(
            path, tablet_index, trimmed_row_count,
            client=self)

    def unfreeze_table(
            self,
            path,
            first_tablet_index=None, last_tablet_index=None, sync=False):
        """
        Unfreezes table.

        TODO

        """
        return client_api.unfreeze_table(
            path,
            client=self,
            first_tablet_index=first_tablet_index, last_tablet_index=last_tablet_index, sync=sync)

    def unlock(
            self,
            path):
        """
        Tries to unlock the path.

        Both acquired and pending locks are unlocked. Only explicit locks are unlockable.

        If the node is not locked, succeeds silently. If the locked version of the node
        contains changes compared to its original version, :class:`YtError <yt.common.YtError>` is raised.

        .. seealso:: `unlock in the docs <https://yt.yandex-team.ru/docs/description/storage/transactions#lock_operations>`_

        """
        return client_api.unlock(
            path,
            client=self)

    def unmount_table(
            self,
            path,
            first_tablet_index=None, last_tablet_index=None, force=None, sync=False):
        """
        Unmounts table.

        TODO

        """
        return client_api.unmount_table(
            path,
            client=self,
            first_tablet_index=first_tablet_index, last_tablet_index=last_tablet_index, force=force,
            sync=sync)

    def unregister_queue_consumer(
            self,
            queue_path, consumer_path):
        """
        Unregister queue consumer.

        :param queue_path: path to queue table.
        :type queue_path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param consumer_path: path to consumer table.
        :type consumer_path: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`

        """
        return client_api.unregister_queue_consumer(
            queue_path, consumer_path,
            client=self)

    def update_operation_parameters(
            self,
            operation_id, parameters):
        """
        Updates operation runtime parameters.
        """
        return client_api.update_operation_parameters(
            operation_id, parameters,
            client=self)

    def write_file(
            self,
            destination, stream,
            file_writer=None, is_stream_compressed=False, force_create=None, compute_md5=False,
            size_hint=None, filename_hint=None, progress_monitor=None):
        """
        Uploads file to destination path from stream on local machine.

        :param destination: destination path in Cypress.
        :type destination: str or :class:`FilePath <yt.wrapper.ypath.FilePath>`
        :param stream: stream or bytes generator.
        :param dict file_writer: spec of upload operation.
        :param bool is_stream_compressed: expect stream to contain compressed data.
        This data can be passed directly to proxy without recompression. Be careful! this option
        disables write retries.
        :param bool force_create: unconditionally create file and ignores exsting file.
        :param bool compute_md5: compute md5 of file content.

        """
        return client_api.write_file(
            destination, stream,
            client=self,
            file_writer=file_writer, is_stream_compressed=is_stream_compressed, force_create=force_create,
            compute_md5=compute_md5, size_hint=size_hint, filename_hint=filename_hint, progress_monitor=progress_monitor)

    def write_table(
            self,
            table, input_stream,
            format=None, table_writer=None, max_row_buffer_size=None, is_stream_compressed=False,
            force_create=None, raw=None):
        """
        Writes rows from input_stream to table.

        :param table: output table. Specify `TablePath` attributes for append mode or something like this.
        Table can not exist.
        :type table: str or :class:`TablePath <yt.wrapper.ypath.TablePath>`
        :param input_stream: python file-like object, string, list of strings.
        :param format: format of input data, ``yt.wrapper.config["tabular_data_format"]`` by default.
        :type format: str or descendant of :class:`Format <yt.wrapper.format.Format>`
        :param dict table_writer: spec of "write" operation.
        :param int max_row_buffer_size: option for testing purposes only, consult yt@ if you want to use it.
        :param bool is_stream_compressed: expect stream to contain compressed table data.
        This data can be passed directly to proxy without recompression. Be careful! This option
        disables write retries.
        :param bool force_create: try to create table regardless of its existence
        (if not specified the pure write_table call will create table if it is doesn't exist).
        Use this option only if you know what you do.

        The function tries to split input stream to portions of fixed size and write its with retries.
        If splitting fails, stream is written as is through HTTP.
        Set ``yt.wrapper.config["write_retries"]["enable"]`` to False for writing     without splitting and retries.

        Writing is executed under self-pinged transaction.

        """
        return client_api.write_table(
            table, input_stream,
            client=self,
            format=format, table_writer=table_writer, max_row_buffer_size=max_row_buffer_size, is_stream_compressed=is_stream_compressed,
            force_create=force_create, raw=raw)

    def write_table_structured(
            self,
            table, row_type, input_stream,
            table_writer=None, max_row_buffer_size=None, force_create=None):
        """
        Writes rows from input_stream to table in structured format. Cf. docstring for write_table
        """
        return client_api.write_table_structured(
            table, row_type, input_stream,
            client=self,
            table_writer=table_writer, max_row_buffer_size=max_row_buffer_size, force_create=force_create)
