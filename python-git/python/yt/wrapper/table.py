from common import flatten, require, YtError
from path_tools import escape_path, split_table_ranges

class Table(object):
    """ Columns should be list of string (column) or string pairs (column range) """
    def __init__(self, name, append=False, columns=None,
                 lower_key=None, upper_key=None,
                 start_index=None, end_index=None):
        self.name = name
        self.append = append
        self.columns = columns
        self.lower_key = lower_key
        self.upper_key = upper_key
        self.start_index = start_index
        self.end_index = end_index

        self.has_index = start_index is not None or end_index is not None
        self.has_key = lower_key is not None or upper_key is not None
        require(not (self.has_index and self.has_key),
                YtError("You could not specify key bound and index bound simultaneously"))

    def escaped_name(self):
        return escape_path(self.name)

    def yson_name(self):
        def column_to_str(column):
            column = flatten(column)
            require(len(column) <= 2,
                    YtError("Incorrect column " + str(column)))
            if len(column) == 1:
                return column[0]
            else:
                return ":".join(column)

        def key_to_str(key):
            if key is None:
                return ""
            return '("%s")' % ",".join(flatten(key))

        def index_to_str(index):
            if index is None:
                return ""
            return '#%d' % index

        name = self.escaped_name()
        if self.columns is not None:
            name = "%s{%s}" % \
                (name, ",".join(map(column_to_str, self.columns)))
        if self.has_key:
            name = "%s[%s]" % \
                (name, ":".join(map(key_to_str, [self.lower_key, self.upper_key])))

        if self.has_index:
            name = "%s[%s]" % \
                (name, ":".join(map(index_to_str, [self.start_index, self.end_index])))

        return name

    def has_delimiters(self):
        return \
            self.columns is not None or \
            self.lower_key is not None or \
            self.upper_key is not None or \
            self.append

    def __eq__(self, other):
        return self.name == other.name

    def __hash__(self):
        return hash(self.name)

def get_yson_name(table):
    return table.yson_name()

def to_table(object):
    if isinstance(object, Table):
        return object
    else:
        return Table(object)

def to_name(object):
    if isinstance(object, Table):
        name = object.name
    else:
        name = object
    return split_table_ranges(name)[0]
