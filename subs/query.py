import typing


class Query(object):
    @classmethod
    def make_args(cls, n: int): return ', '.join(('?',) * n)

    def __init__(self, table: str):
        self.table = table
        self.fields: typing.List[str] = []
        self.joins: typing.List[str] = []
        self.wheres: typing.List[str] = []
        self.group_bys: typing.List[str] = []
        self.order_bys: typing.List[str] = []

    def add_fields(self, *name: str):
        self.fields.extend(name)

    def add_joins(self, *joins: str):
        self.joins.extend(joins)

    def add_filter(self, *exprs: str):
        self.wheres.extend(exprs)

    def add_group(self, *exprs: str):
        self.group_bys.extend(exprs)

    def add_order(self, *exprs: str):
        self.order_bys.extend(exprs)

    def _query(self, l):
        if self.wheres:
            l.append('where')
            l.append(' and '.join(self.wheres))
        if self.group_bys:
            l.append('group by')
            l.append(', '.join(self.group_bys))
        if self.order_bys:
            l.append('order by')
            l.append(', '.join(self.order_bys))
        return ' '.join(l)

    def query(self, distinct=False):
        ret = ['select']
        if distinct:
            ret.append('distinct')
        ret.extend((', '.join(self.fields), 'from', self.table))
        if self.joins:
            ret.append(' '.join(self.joins))
        return self._query(ret)

    def update(self, q):
        return self._query(['update', self.table, 'set', q])
