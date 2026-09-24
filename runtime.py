import sys
import math
import datetime

class PliArray:
    def __init__(self, bounds, default=0):
        # bounds is a list of (low, high)
        self.bounds = bounds
        self.default = default
        self.dims = [high - low + 1 for low, high in bounds]
        total_size = 1
        for d in self.dims:
            total_size *= d
        self.data = [default] * total_size

    def _get_flat_index(self, indices):
        if len(indices) != len(self.bounds):
            raise IndexError(f"Expected {len(self.bounds)} dimensions, got {len(indices)}")
        flat = 0
        for i, (idx, (low, high)) in enumerate(zip(indices, self.bounds)):
            if idx < low or idx > high:
                raise IndexError(f"Subscript {idx} out of range [{low}:{high}] in dimension {i+1}")
            offset = idx - low
            flat = flat * self.dims[i] + offset
        return flat

    def __getitem__(self, indices):
        if not isinstance(indices, tuple):
            indices = (indices,)
        return self.data[self._get_flat_index(indices)]

    def __setitem__(self, indices, value):
        if not isinstance(indices, tuple):
            indices = (indices,)
        self.data[self._get_flat_index(indices)] = value

    def __repr__(self):
        return f"PliArray(bounds={self.bounds}, data={self.data})"

def _pli_call_or_index(target, *args):
    if isinstance(target, PliArray):
        return target[args]
    elif callable(target):
        return target(*args)
    else:
        raise TypeError(f"Target '{target}' is not callable or an array")

def _pli_concat(a, b):
    return str(a) + str(b)

def _pli_substr(s, start, length=None):
    s = str(s)
    start_idx = int(start) - 1
    if start_idx < 0:
        start_idx = 0
    if length is None:
        return s[start_idx:]
    else:
        l = int(length)
        if l < 0:
            return ""
        return s[start_idx:start_idx + l]

def _pli_set_substr(s, start, length, replacement):
    s = str(s)
    start_idx = int(start) - 1
    rep = str(replacement)
    if length is None:
        len_rep = len(rep)
    else:
        len_rep = int(length)
        if len(rep) > len_rep:
            rep = rep[:len_rep]
        elif len(rep) < len_rep:
            rep = rep.ljust(len_rep)

    if start_idx < 0:
        start_idx = 0
    if start_idx > len(s):
        s = s.ljust(start_idx)

    end_idx = start_idx + len_rep
    return s[:start_idx] + rep + s[end_idx:]

def _pli_index(s, sub):
    pos = str(s).find(str(sub))
    return (pos + 1) if pos != -1 else 0

def _pli_length(s):
    return len(str(s))

def _pli_verify(s, chars):
    s_str = str(s)
    c_str = str(chars)
    for i, ch in enumerate(s_str):
        if ch not in c_str:
            return i + 1
    return 0

def _pli_translate(s, to_chars, from_chars):
    s_str = str(s)
    table = str.maketrans(str(from_chars), str(to_chars))
    return s_str.translate(table)

def _pli_trim(s):
    return str(s).strip()

def _pli_mod(x, y):
    return x % y

def _pli_abs(x):
    return abs(x)

def _pli_max(*args):
    return max(args)

def _pli_min(*args):
    return min(args)

def _pli_sqrt(x):
    return math.sqrt(x)

def _pli_round(x, n=0):
    return round(x, int(n))

def _pli_ceil(x):
    return math.ceil(x)

def _pli_floor(x):
    return math.floor(x)

def _pli_sign(x):
    if x > 0: return 1
    elif x < 0: return -1
    return 0

def _pli_date():
    return datetime.datetime.now().strftime("%y%m%d")

def _pli_time():
    now = datetime.datetime.now()
    return now.strftime("%H%M%S") + f"{now.microsecond // 1000:03d}"

_input_buffer = []

def _pli_get_list(*targets):
    global _input_buffer
    results = []
    for _ in targets:
        while not _input_buffer:
            line = sys.stdin.readline()
            if not line:
                return results
            tokens = [t.strip() for t in line.replace(',', ' ').split() if t.strip()]
            _input_buffer.extend(tokens)
        val_str = _input_buffer.pop(0)
        try:
            val = int(val_str)
        except ValueError:
            try:
                val = float(val_str)
            except ValueError:
                val = val_str
        results.append(val)
    return results

def _pli_put_list(items, skip=False, skip_count=1):
    if skip:
        sys.stdout.write('\n' * max(1, int(skip_count)))
    formatted = []
    for item in items:
        if isinstance(item, bool):
            formatted.append("'1'B" if item else "'0'B")
        else:
            formatted.append(str(item))
    sys.stdout.write(" ".join(formatted) + "\n")
    sys.stdout.flush()

def _pli_put_edit(items, formats, skip=False, skip_count=1):
    if skip:
        sys.stdout.write('\n' * max(1, int(skip_count)))
    res = []
    for item in items:
        res.append(str(item))
    sys.stdout.write(" ".join(res) + "\n")
    sys.stdout.flush()
