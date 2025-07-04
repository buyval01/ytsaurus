"""
Template for intervaltree

WARNING: DO NOT edit .pxi FILE directly, .pxi is generated from .pxi.in
"""

from pandas._libs.algos import is_monotonic

ctypedef fused int_scalar_t:
    int64_t
    float64_t

ctypedef fused uint_scalar_t:
    uint64_t
    float64_t

ctypedef fused scalar_t:
    int_scalar_t
    uint_scalar_t

# ----------------------------------------------------------------------
# IntervalTree
# ----------------------------------------------------------------------

cdef class IntervalTree(IntervalMixin):
    """A centered interval tree

    Based off the algorithm described on Wikipedia:
    https://en.wikipedia.org/wiki/Interval_tree

    we are emulating the IndexEngine interface
    """
    cdef readonly:
        ndarray left, right
        IntervalNode root
        object dtype
        str closed
        object _is_overlapping, _left_sorter, _right_sorter
        Py_ssize_t _na_count

    def __init__(self, left, right, closed='right', leaf_size=100):
        """
        Parameters
        ----------
        left, right : np.ndarray[ndim=1]
            Left and right bounds for each interval. Assumed to contain no
            NaNs.
        closed : {'left', 'right', 'both', 'neither'}, optional
            Whether the intervals are closed on the left-side, right-side, both
            or neither. Defaults to 'right'.
        leaf_size : int, optional
            Parameter that controls when the tree switches from creating nodes
            to brute-force search. Tune this parameter to optimize query
            performance.
        """
        if closed not in ['left', 'right', 'both', 'neither']:
            raise ValueError("invalid option for 'closed': %s" % closed)

        left = np.asarray(left)
        right = np.asarray(right)
        self.dtype = np.result_type(left, right)
        self.left = np.asarray(left, dtype=self.dtype)
        self.right = np.asarray(right, dtype=self.dtype)

        indices = np.arange(len(left), dtype='int64')

        self.closed = closed

        # GH 23352: ensure no nan in nodes
        mask = ~np.isnan(self.left)
        self._na_count = len(mask) - mask.sum()
        self.left = self.left[mask]
        self.right = self.right[mask]
        indices = indices[mask]

        node_cls = NODE_CLASSES[str(self.dtype), closed]
        self.root = node_cls(self.left, self.right, indices, leaf_size)

    @property
    def left_sorter(self) -> np.ndarray:
        """How to sort the left labels; this is used for binary search
        """
        if self._left_sorter is None:
            values = [self.right, self.left]
            self._left_sorter = np.lexsort(values)
        return self._left_sorter

    @property
    def right_sorter(self) -> np.ndarray:
        """How to sort the right labels
        """
        if self._right_sorter is None:
            self._right_sorter = np.argsort(self.right)
        return self._right_sorter

    @property
    def is_overlapping(self) -> bool:
        """
        Determine if the IntervalTree contains overlapping intervals.
        Cached as self._is_overlapping.
        """
        if self._is_overlapping is not None:
            return self._is_overlapping

        # <= when both sides closed since endpoints can overlap
        op = le if self.closed == 'both' else lt

        # overlap if start of current interval < end of previous interval
        # (current and previous in terms of sorted order by left/start side)
        current = self.left[self.left_sorter[1:]]
        previous = self.right[self.left_sorter[:-1]]
        self._is_overlapping = bool(op(current, previous).any())

        return self._is_overlapping

    @property
    def is_monotonic_increasing(self) -> bool:
        """
        Return True if the IntervalTree is monotonic increasing (only equal or
        increasing values), else False
        """
        if self._na_count > 0:
            return False

        sort_order = self.left_sorter
        return is_monotonic(sort_order, False)[0]

    def get_indexer(self, ndarray[scalar_t, ndim=1] target) -> np.ndarray:
        """Return the positions corresponding to unique intervals that overlap
        with the given array of scalar targets.
        """

        # TODO: write get_indexer_intervals
        cdef:
            Py_ssize_t old_len
            Py_ssize_t i
            Int64Vector result

        result = Int64Vector()
        old_len = 0
        for i in range(len(target)):
            try:
                self.root.query(result, target[i])
            except OverflowError:
                # overflow -> no match, which is already handled below
                pass

            if result.data.n == old_len:
                result.append(-1)
            elif result.data.n > old_len + 1:
                raise KeyError(
                    'indexer does not intersect a unique set of intervals')
            old_len = result.data.n
        return result.to_array().astype('intp')

    def get_indexer_non_unique(self, ndarray[scalar_t, ndim=1] target):
        """Return the positions corresponding to intervals that overlap with
        the given array of scalar targets. Non-unique positions are repeated.
        """
        cdef:
            Py_ssize_t old_len
            Py_ssize_t i
            Int64Vector result, missing

        result = Int64Vector()
        missing = Int64Vector()
        old_len = 0
        for i in range(len(target)):
            try:
                self.root.query(result, target[i])
            except OverflowError:
                # overflow -> no match, which is already handled below
                pass

            if result.data.n == old_len:
                result.append(-1)
                missing.append(i)
            old_len = result.data.n
        return (result.to_array().astype('intp'),
                missing.to_array().astype('intp'))

    def __repr__(self) -> str:
        return ('<IntervalTree[{dtype},{closed}]: '
                '{n_elements} elements>'.format(
                    dtype=self.dtype, closed=self.closed,
                    n_elements=self.root.n_elements))

    # compat with IndexEngine interface
    def clear_mapping(self) -> None:
        pass


cdef take(ndarray source, ndarray indices):
    """Take the given positions from a 1D ndarray
    """
    return PyArray_Take(source, indices, 0)


cdef sort_values_and_indices(all_values, all_indices, subset):
    indices = take(all_indices, subset)
    values = take(all_values, subset)
    sorter = PyArray_ArgSort(values, 0, NPY_QUICKSORT)
    sorted_values = take(values, sorter)
    sorted_indices = take(indices, sorter)
    return sorted_values, sorted_indices


# ----------------------------------------------------------------------
# Nodes
# ----------------------------------------------------------------------

@cython.internal
cdef class IntervalNode:
    cdef readonly:
        int64_t n_elements, n_center, leaf_size
        bint is_leaf_node

    def __repr__(self) -> str:
        if self.is_leaf_node:
            return (
                f"<{type(self).__name__}: {self.n_elements} elements (terminal)>"
            )
        else:
            n_left = self.left_node.n_elements
            n_right = self.right_node.n_elements
            n_center = self.n_elements - n_left - n_right
            return (
                f"<{type(self).__name__}: "
                f"pivot {self.pivot}, {self.n_elements} elements "
                f"({n_left} left, {n_right} right, {n_center} overlapping)>"
            )

    def counts(self):
        """
        Inspect counts on this node
        useful for debugging purposes
        """
        if self.is_leaf_node:
            return self.n_elements
        else:
            m = len(self.center_left_values)
            l = self.left_node.counts()
            r = self.right_node.counts()
            return (m, (l, r))


# we need specialized nodes and leaves to optimize for different dtype and
# closed values

NODE_CLASSES = {}


@cython.internal
cdef class Float64ClosedLeftIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Float64ClosedLeftIntervalNode left_node, right_node
        float64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        float64_t min_left, max_right
        float64_t pivot

    def __init__(self,
                 ndarray[float64_t, ndim=1] left,
                 ndarray[float64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(float64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, float64_t[:] left, float64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[float64_t, ndim=1] left,
                        ndarray[float64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Float64ClosedLeftIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            float64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['float64',
             'left'] = Float64ClosedLeftIntervalNode


@cython.internal
cdef class Float64ClosedRightIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Float64ClosedRightIntervalNode left_node, right_node
        float64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        float64_t min_left, max_right
        float64_t pivot

    def __init__(self,
                 ndarray[float64_t, ndim=1] left,
                 ndarray[float64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(float64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, float64_t[:] left, float64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[float64_t, ndim=1] left,
                        ndarray[float64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Float64ClosedRightIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            float64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['float64',
             'right'] = Float64ClosedRightIntervalNode


@cython.internal
cdef class Float64ClosedBothIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Float64ClosedBothIntervalNode left_node, right_node
        float64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        float64_t min_left, max_right
        float64_t pivot

    def __init__(self,
                 ndarray[float64_t, ndim=1] left,
                 ndarray[float64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(float64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, float64_t[:] left, float64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[float64_t, ndim=1] left,
                        ndarray[float64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Float64ClosedBothIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            float64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['float64',
             'both'] = Float64ClosedBothIntervalNode


@cython.internal
cdef class Float64ClosedNeitherIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Float64ClosedNeitherIntervalNode left_node, right_node
        float64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        float64_t min_left, max_right
        float64_t pivot

    def __init__(self,
                 ndarray[float64_t, ndim=1] left,
                 ndarray[float64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(float64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, float64_t[:] left, float64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[float64_t, ndim=1] left,
                        ndarray[float64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Float64ClosedNeitherIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            float64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['float64',
             'neither'] = Float64ClosedNeitherIntervalNode


@cython.internal
cdef class Int64ClosedLeftIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Int64ClosedLeftIntervalNode left_node, right_node
        int64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        int64_t min_left, max_right
        int64_t pivot

    def __init__(self,
                 ndarray[int64_t, ndim=1] left,
                 ndarray[int64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(int64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, int64_t[:] left, int64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[int64_t, ndim=1] left,
                        ndarray[int64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Int64ClosedLeftIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, int_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            int64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['int64',
             'left'] = Int64ClosedLeftIntervalNode


@cython.internal
cdef class Int64ClosedRightIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Int64ClosedRightIntervalNode left_node, right_node
        int64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        int64_t min_left, max_right
        int64_t pivot

    def __init__(self,
                 ndarray[int64_t, ndim=1] left,
                 ndarray[int64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(int64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, int64_t[:] left, int64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[int64_t, ndim=1] left,
                        ndarray[int64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Int64ClosedRightIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, int_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            int64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['int64',
             'right'] = Int64ClosedRightIntervalNode


@cython.internal
cdef class Int64ClosedBothIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Int64ClosedBothIntervalNode left_node, right_node
        int64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        int64_t min_left, max_right
        int64_t pivot

    def __init__(self,
                 ndarray[int64_t, ndim=1] left,
                 ndarray[int64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(int64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, int64_t[:] left, int64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[int64_t, ndim=1] left,
                        ndarray[int64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Int64ClosedBothIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, int_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            int64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['int64',
             'both'] = Int64ClosedBothIntervalNode


@cython.internal
cdef class Int64ClosedNeitherIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Int64ClosedNeitherIntervalNode left_node, right_node
        int64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        int64_t min_left, max_right
        int64_t pivot

    def __init__(self,
                 ndarray[int64_t, ndim=1] left,
                 ndarray[int64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(int64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, int64_t[:] left, int64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[int64_t, ndim=1] left,
                        ndarray[int64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Int64ClosedNeitherIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, int_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            int64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['int64',
             'neither'] = Int64ClosedNeitherIntervalNode


@cython.internal
cdef class Uint64ClosedLeftIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Uint64ClosedLeftIntervalNode left_node, right_node
        uint64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        uint64_t min_left, max_right
        uint64_t pivot

    def __init__(self,
                 ndarray[uint64_t, ndim=1] left,
                 ndarray[uint64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(uint64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, uint64_t[:] left, uint64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[uint64_t, ndim=1] left,
                        ndarray[uint64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Uint64ClosedLeftIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, uint_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            uint64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['uint64',
             'left'] = Uint64ClosedLeftIntervalNode


@cython.internal
cdef class Uint64ClosedRightIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Uint64ClosedRightIntervalNode left_node, right_node
        uint64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        uint64_t min_left, max_right
        uint64_t pivot

    def __init__(self,
                 ndarray[uint64_t, ndim=1] left,
                 ndarray[uint64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(uint64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, uint64_t[:] left, uint64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[uint64_t, ndim=1] left,
                        ndarray[uint64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Uint64ClosedRightIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, uint_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            uint64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['uint64',
             'right'] = Uint64ClosedRightIntervalNode


@cython.internal
cdef class Uint64ClosedBothIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Uint64ClosedBothIntervalNode left_node, right_node
        uint64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        uint64_t min_left, max_right
        uint64_t pivot

    def __init__(self,
                 ndarray[uint64_t, ndim=1] left,
                 ndarray[uint64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(uint64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, uint64_t[:] left, uint64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] < self.pivot:
                left_ind.append(i)
            elif self.pivot < left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[uint64_t, ndim=1] left,
                        ndarray[uint64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Uint64ClosedBothIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, uint_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            uint64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] <= point <= self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] <= point:
                        break
                    result.append(indices[i])
                if point <= self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point <= values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left <= point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['uint64',
             'both'] = Uint64ClosedBothIntervalNode


@cython.internal
cdef class Uint64ClosedNeitherIntervalNode(IntervalNode):
    """Non-terminal node for an IntervalTree

    Categorizes intervals by those that fall to the left, those that fall to
    the right, and those that overlap with the pivot.
    """
    cdef readonly:
        Uint64ClosedNeitherIntervalNode left_node, right_node
        uint64_t[:] center_left_values, center_right_values, left, right
        int64_t[:] center_left_indices, center_right_indices, indices
        uint64_t min_left, max_right
        uint64_t pivot

    def __init__(self,
                 ndarray[uint64_t, ndim=1] left,
                 ndarray[uint64_t, ndim=1] right,
                 ndarray[int64_t, ndim=1] indices,
                 int64_t leaf_size):

        self.n_elements = len(left)
        self.leaf_size = leaf_size

        # min_left and min_right are used to speed-up query by skipping
        # query on sub-nodes. If this node has size 0, query is cheap,
        # so these values don't matter.
        if left.size > 0:
            self.min_left = left.min()
            self.max_right = right.max()
        else:
            self.min_left = 0
            self.max_right = 0

        if self.n_elements <= leaf_size:
            # make this a terminal (leaf) node
            self.is_leaf_node = True
            self.left = left
            self.right = right
            self.indices = indices
            self.n_center = 0
        else:
            # calculate a pivot so we can create child nodes
            self.is_leaf_node = False
            self.pivot = np.median(left / 2 + right / 2)
            if np.isinf(self.pivot):
                self.pivot = cython.cast(uint64_t, 0)
                if self.pivot > np.max(right):
                    self.pivot = np.max(left)
                if self.pivot < np.min(left):
                    self.pivot = np.min(right)

            left_set, right_set, center_set = self.classify_intervals(
                left, right)

            self.left_node = self.new_child_node(left, right,
                                                 indices, left_set)
            self.right_node = self.new_child_node(left, right,
                                                  indices, right_set)

            self.center_left_values, self.center_left_indices = \
                sort_values_and_indices(left, indices, center_set)
            self.center_right_values, self.center_right_indices = \
                sort_values_and_indices(right, indices, center_set)
            self.n_center = len(self.center_left_indices)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    cdef classify_intervals(self, uint64_t[:] left, uint64_t[:] right):
        """Classify the given intervals based upon whether they fall to the
        left, right, or overlap with this node's pivot.
        """
        cdef:
            Int64Vector left_ind, right_ind, overlapping_ind
            Py_ssize_t i

        left_ind = Int64Vector()
        right_ind = Int64Vector()
        overlapping_ind = Int64Vector()

        for i in range(self.n_elements):
            if right[i] <= self.pivot:
                left_ind.append(i)
            elif self.pivot <= left[i]:
                right_ind.append(i)
            else:
                overlapping_ind.append(i)

        return (left_ind.to_array(),
                right_ind.to_array(),
                overlapping_ind.to_array())

    cdef new_child_node(self,
                        ndarray[uint64_t, ndim=1] left,
                        ndarray[uint64_t, ndim=1] right,
                        ndarray[int64_t, ndim=1] indices,
                        ndarray[int64_t, ndim=1] subset):
        """Create a new child node.
        """
        left = take(left, subset)
        right = take(right, subset)
        indices = take(indices, subset)
        return Uint64ClosedNeitherIntervalNode(
            left, right, indices, self.leaf_size)

    @cython.wraparound(False)
    @cython.boundscheck(False)
    @cython.initializedcheck(False)
    cpdef query(self, Int64Vector result, uint_scalar_t point):
        """Recursively query this node and its sub-nodes for intervals that
        overlap with the query point.
        """
        cdef:
            int64_t[:] indices
            uint64_t[:] values
            Py_ssize_t i

        if self.is_leaf_node:
            # Once we get down to a certain size, it doesn't make sense to
            # continue the binary tree structure. Instead, we use linear
            # search.
            for i in range(self.n_elements):
                if self.left[i] < point < self.right[i]:
                    result.append(self.indices[i])
        else:
            # There are child nodes. Based on comparing our query to the pivot,
            # look at the center values, then go to the relevant child.
            if point < self.pivot:
                values = self.center_left_values
                indices = self.center_left_indices
                for i in range(self.n_center):
                    if not values[i] < point:
                        break
                    result.append(indices[i])
                if point < self.left_node.max_right:
                    self.left_node.query(result, point)
            elif point > self.pivot:
                values = self.center_right_values
                indices = self.center_right_indices
                for i in range(self.n_center - 1, -1, -1):
                    if not point < values[i]:
                        break
                    result.append(indices[i])
                if self.right_node.min_left < point:
                    self.right_node.query(result, point)
            else:
                result.extend(self.center_left_indices)


NODE_CLASSES['uint64',
             'neither'] = Uint64ClosedNeitherIntervalNode
