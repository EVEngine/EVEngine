// Node.js-style async runtime for EVEngine (main-thread callbacks only).
// Workers must not touch the Squirrel VM — use ThreadChannel / Thread.postMain,
// then resolve Promises from async_pump() on the game thread.
//
// Loaded via eve.asyncScript from load.nut before the frame loop.

if (!("_async" in getroottable())) {
    _async <- {
        nextTicks = []
        timers = []          // { id, due, fn, interval }
        nextId = 1
        draining = false
        maxDrain = 1000
        // Pending worker polls: { ch, resolve, reject, task }
        waits = []
        // Task-correlated completions: { task, value, resolve, reject }
        taskWaits = []
        eventWaits = []  // { name, resolve, reject }
    };
}
if (!("taskWaits" in _async))
    _async.taskWaits <- [];

function nextTick(fn) {
    if (typeof fn != "function")
        throw "nextTick: expected function";
    _async.nextTicks.append(fn);
}

function setImmediate(fn) {
    nextTick(fn);
    return 0;
}

function setTimeout(fn, ms) {
    if (typeof fn != "function")
        throw "setTimeout: expected function";
    if (ms == null) ms = 0;
    if (ms < 0) ms = 0;
    local id = _async.nextId;
    _async.nextId += 1;
    local due = timer.getTime() + (ms * 1.0 / 1000.0);
    _async.timers.append({ id = id, due = due, fn = fn, interval = 0.0 });
    return id;
}

function setInterval(fn, ms) {
    if (typeof fn != "function")
        throw "setInterval: expected function";
    if (ms == null) ms = 0;
    if (ms < 0) ms = 0;
    local id = _async.nextId;
    _async.nextId += 1;
    local period = ms * 1.0 / 1000.0;
    local due = timer.getTime() + period;
    _async.timers.append({ id = id, due = due, fn = fn, interval = period });
    return id;
}

function clearTimeout(id) {
    local out = [];
    foreach (t in _async.timers) {
        if (t.id != id)
            out.append(t);
    }
    _async.timers = out;
}

function clearInterval(id) {
    clearTimeout(id);
}

function _async_drain_microtasks() {
    local n = 0;
    while (_async.nextTicks.len() > 0 && n < _async.maxDrain) {
        local fn = _async.nextTicks[0];
        _async.nextTicks.remove(0);
        n += 1;
        try {
            fn();
        } catch (e) {
            print("nextTick error: " + e + "\n");
        }
    }
    if (_async.nextTicks.len() > 0)
        print("async: microtask queue truncated after " + _async.maxDrain + "\n");
}

function _async_fire_timers() {
    if (_async.timers.len() == 0)
        return;
    local now = timer.getTime();
    local remain = [];
    local dueFns = [];
    foreach (t in _async.timers) {
        if (t.due <= now) {
            dueFns.append(t);
            if (t.interval > 0.0) {
                t.due = now + t.interval;
                remain.append(t);
            }
        } else {
            remain.append(t);
        }
    }
    _async.timers = remain;
    foreach (t in dueFns) {
        try {
            t.fn();
        } catch (e) {
            print("timer error: " + e + "\n");
        }
    }
}

function _async_poll_waits() {
    if (_async.waits.len() == 0)
        return;
    local remain = [];
    foreach (w in _async.waits) {
        local done = false;
        try {
            if (w.task != null && w.task.hasFailed()) {
                w.reject(w.task.getError());
                done = true;
            } else if (w.ch != null && w.ch.hasData()) {
                w.resolve(w.ch.pop());
                done = true;
            }
        } catch (e) {
            try { w.reject(e); } catch (e2) {}
            done = true;
        }
        if (!done)
            remain.append(w);
    }
    _async.waits = remain;
}

function _async_poll_tasks() {
    if (_async.taskWaits.len() == 0)
        return;
    local remain = [];
    foreach (w in _async.taskWaits) {
        local done = false;
        try {
            if (w.task.hasFailed()) {
                w.reject(w.task.getError());
                done = true;
            } else if (w.task.isDone()) {
                w.resolve(w.value);
                done = true;
            }
        } catch (e) {
            try { w.reject(e); } catch (e2) {}
            done = true;
        }
        if (!done)
            remain.append(w);
    }
    _async.taskWaits = remain;
}

// Drain timers, worker waits, and microtasks. Call once per frame (load.nut).
function async_pump() {
    if (_async.draining)
        return;
    _async.draining = true;
    try {
        _async_fire_timers();
        _async_poll_waits();
        _async_poll_tasks();
        _async_drain_microtasks();
        // Timers / waits may have queued more microtasks.
        _async_drain_microtasks();
    } catch (e) {
        print("async_pump error: " + e + "\n");
    }
    _async.draining = false;
}

// ---- Promise (thenable, main-thread only) ---------------------------------

class Promise {
    _state = "pending"   // pending | fulfilled | rejected
    _value = null
    _handlers = null

    constructor(executor) {
        this._handlers = [];
        if (typeof executor != "function")
            throw "Promise: executor must be a function";
        local self = this;
        local resolve = function(value) { self._settle("fulfilled", value); };
        local reject = function(reason) { self._settle("rejected", reason); };
        try {
            executor(resolve, reject);
        } catch (e) {
            reject(e);
        }
    }

    function _settle(state, value) {
        if (this._state != "pending")
            return;
        // Promise resolution procedure (simplified): flatten thenables.
        if (state == "fulfilled" && value != null && typeof value == "instance" &&
            ("then" in value) && typeof value.then == "function") {
            local self = this;
            try {
                value.then(
                    function(v) { self._settle("fulfilled", v); },
                    function(e) { self._settle("rejected", e); }
                );
            } catch (e) {
                this._state = "rejected";
                this._value = e;
                this._flush();
            }
            return;
        }
        this._state = state;
        this._value = value;
        this._flush();
    }

    function _flush() {
        local handlers = this._handlers;
        this._handlers = [];
        local self = this;
        foreach (h in handlers) {
            nextTick(function() {
                try {
                    if (self._state == "fulfilled") {
                        if (typeof h.onFulfilled == "function") {
                            local v = h.onFulfilled(self._value);
                            h.resolve(v);
                        } else {
                            h.resolve(self._value);
                        }
                    } else {
                        if (typeof h.onRejected == "function") {
                            local v = h.onRejected(self._value);
                            h.resolve(v);
                        } else {
                            h.reject(self._value);
                        }
                    }
                } catch (e) {
                    h.reject(e);
                }
            });
        }
    }

    function then(onFulfilled, onRejected = null) {
        local self = this;
        return Promise(function(resolve, reject) {
            self._handlers.append({
                onFulfilled = onFulfilled
                onRejected = onRejected
                resolve = resolve
                reject = reject
            });
            if (self._state != "pending")
                self._flush();
        });
    }

    // Named fail — Squirrel reserves `catch` as a keyword.
    function fail(onRejected) {
        return this.then(null, onRejected);
    }

    function fin(onFinally) {
        return this.then(
            function(v) {
                if (typeof onFinally == "function") onFinally();
                return v;
            },
            function(e) {
                if (typeof onFinally == "function") onFinally();
                throw e;
            }
        );
    }
}

Promise.resolve <- function(value) {
    return Promise(function(resolve, reject) { resolve(value); });
};

Promise.reject <- function(reason) {
    return Promise(function(resolve, reject) { reject(reason); });
};

Promise.all <- function(arr) {
    return Promise(function(resolve, reject) {
        if (arr == null || arr.len() == 0) {
            resolve([]);
            return;
        }
        local results = [];
        results.resize(arr.len(), null);
        local left = arr.len();
        for (local i = 0; i < arr.len(); i++) {
            local idx = i;
            local p = arr[i];
            if (p == null || typeof p != "instance" || !("then" in p))
                p = Promise.resolve(p);
            p.then(
                function(v) {
                    results[idx] = v;
                    left -= 1;
                    if (left == 0)
                        resolve(results);
                },
                function(e) { reject(e); }
            );
        }
    });
};

Promise.race <- function(arr) {
    return Promise(function(resolve, reject) {
        if (arr == null || arr.len() == 0)
            return;
        foreach (p in arr) {
            if (p == null || typeof p != "instance" || !("then" in p))
                p = Promise.resolve(p);
            p.then(resolve, reject);
        }
    });
};

// ---- Helpers --------------------------------------------------------------

function asyncSleep(ms) {
    return Promise(function(resolve, reject) {
        setTimeout(function() { resolve(null); }, ms);
    });
}

// Run delay on a worker thread, then resolve with `value` on the main thread.
function asyncDelay(ms, value = "") {
    if (value == null) value = "";
    return Promise(function(resolve, reject) {
        local ch = thread.newChannel();
        local pool = thread.getPool();
        local task = pool.submitPush(ch, "" + value, ms);
        _async.waits.append({ ch = ch, resolve = resolve, reject = reject, task = task });
    });
}

// Wait for a named Event posted via thread.postMain / pool.submitPost / event.pushData.
function asyncOnEvent(name) {
    return Promise(function(resolve, reject) {
        if (name == null || name == "") {
            reject("asyncOnEvent: empty name");
            return;
        }
        _async.eventWaits.append({ name = name, resolve = resolve, reject = reject });
    });
}

// Called from load.nut for each polled event (before quit handling).
function async_dispatch_event(name, data) {
    if (_async.eventWaits.len() == 0)
        return false;
    local remain = [];
    local matched = false;
    foreach (w in _async.eventWaits) {
        if (!matched && w.name == name) {
            try {
                w.resolve(data);
            } catch (e) {
                try { w.reject(e); } catch (e2) {}
            }
            matched = true;
        } else {
            remain.append(w);
        }
    }
    _async.eventWaits = remain;
    return matched;
}

// Delay on a worker, then post Event(name, data). The Promise is correlated to
// its own native Task, so same-name events cannot resolve the wrong request.
function asyncPost(name, data, ms) {
    if (data == null) data = "";
    if (ms == null) ms = 0;
    return Promise(function(resolve, reject) {
        local pool = thread.getPool();
        local value = "" + data;
        local task = pool.submitPost(name, value, ms);
        _async.taskWaits.append({ task = task, value = value, resolve = resolve, reject = reject });
    });
}

// Poll a ThreadChannel until a message arrives (non-blocking via async_pump).
function asyncReceive(ch) {
    return Promise(function(resolve, reject) {
        if (ch == null) {
            reject("asyncReceive: null channel");
            return;
        }
        _async.waits.append({ ch = ch, resolve = resolve, reject = reject, task = null });
    });
}

// ---- Sequential async (closest to JS async/await) -------------------------
//
// Squirrel has no async/await keywords. Generators' `yield` is statement-only
// here, so `local x = yield promise` is impossible. Use:
//   1) Promise.then chains, or
//   2) asyncSeq([...step factories...]) for linear multi-step flows.
//
// Example:
//   asyncSeq([
//     function() { return asyncSleep(50); },
//     function(_) { return asyncDelay(20, "ok"); },
//     function(msg) { print(msg + "\n"); return msg; },
//   ]).then(function(v) { /* done */ });

function asyncSeq(steps) {
    return Promise(function(resolve, reject) {
        if (steps == null || steps.len() == 0) {
            resolve(null);
            return;
        }
        local i = 0;
        local value = null;
        local function runNext() {
            if (i >= steps.len()) {
                resolve(value);
                return;
            }
            local step = steps[i];
            i += 1;
            local out = null;
            try {
                out = step(value);
            } catch (e) {
                reject(e);
                return;
            }
            if (out != null && typeof out == "instance" && ("then" in out) &&
                typeof out.then == "function") {
                out.then(
                    function(v) {
                        value = v;
                        runNext();
                    },
                    function(e) { reject(e); }
                );
            } else {
                value = out;
                nextTick(runNext);
            }
        }
        runNext();
    });
}

